#include <tbb/pipeline.h>
#include <tbb/task_scheduler_init.h>
#include <cstdio>

extern "C" {
#include "image.h"     // image_t, image_dir_*
#include "filter.h"    // filter_scale_up, filter_vertical_flip
#include "pipeline.h"  // int pipeline_tbb(image_dir_t*)
}

// 1) Lecture : serial_in_order ⇒ pas besoin de mutex
class LoadFilter : public tbb::filter {
public:
    explicit LoadFilter(image_dir_t* dir)
        : tbb::filter(serial_in_order), dir_(dir) {}

    void* operator()(void*) override {
        // nullptr => fin du pipeline
        return image_dir_load_next(dir_);
    }

private:
    image_dir_t* dir_;
};

// 2) Mise à l’échelle ×3 : parallèle
class ScaleFilter : public tbb::filter {
public:
    ScaleFilter() : tbb::filter(parallel) {}

    void* operator()(void* item) override {
        image_t* in = static_cast<image_t*>(item);
        if (!in) return nullptr;
        image_t* out = filter_scale_up(in, 3); // <<< facteur 3
        image_destroy(in);                     // filtres allouent => on libère l’entrée
        return out;
    }
};

// 3) Inversion verticale : parallèle
class FlipFilter : public tbb::filter {
public:
    FlipFilter() : tbb::filter(parallel) {}

    void* operator()(void* item) override {
        image_t* in = static_cast<image_t*>(item);
        if (!in) return nullptr;
        image_t* out = filter_vertical_flip(in);
        image_destroy(in);
        return out;
    }
};

// 4) Sauvegarde : serial_in_order pour I/O propre + affichage “.”
class SaveFilter : public tbb::filter {
public:
    explicit SaveFilter(image_dir_t* dir)
        : tbb::filter(serial_in_order), dir_(dir) {}

    void* operator()(void* item) override {
        image_t* img = static_cast<image_t*>(item);
        if (!img) return nullptr;
        image_dir_save(dir_, img);
        std::fputc('.', stdout);
        std::fflush(stdout);
        image_destroy(img);
        return nullptr;  // fin pour cet élément
    }

private:
    image_dir_t* dir_;
};

int pipeline_tbb(image_dir_t* image_dir) {
    // Initialisation du scheduler TBB “classique”
    tbb::task_scheduler_init init(tbb::task_scheduler_init::automatic);

    try {
        LoadFilter  f_load(image_dir);
        ScaleFilter f_scale;
        FlipFilter  f_flip;
        SaveFilter  f_save(image_dir);

        tbb::pipeline pipe;
        pipe.add_filter(f_load);   // 1) lire
        pipe.add_filter(f_scale);  // 2) scale ×3
        pipe.add_filter(f_flip);   // 3) flip vertical
        pipe.add_filter(f_save);   // 4) enregistrer

        const int nthreads  = tbb::task_scheduler_init::default_num_threads();
        const size_t tokens = (nthreads > 0 ? static_cast<size_t>(nthreads) * 4 : 16);

        pipe.run(tokens);
        std::puts("");  // retour à la ligne après les “.”
        return 0;
    } catch (...) {
        return -1;
    }
}
