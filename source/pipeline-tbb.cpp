#define TBB_SUPPRESS_DEPRECATED_MESSAGES 1
#include <tbb/pipeline.h>
#include <tbb/task_scheduler_init.h>
#include <thread>
#include <vector>

extern "C" {
#include "image.h"
#include "filter.h"
#include "pipeline.h"
}

// Filtre de lecture (serial_in_order)
class LoadFilter : public tbb::filter {
    image_dir_t* dir_;
public:
    explicit LoadFilter(image_dir_t* dir)
        : tbb::filter(serial_in_order), dir_(dir) {}
    void* operator()(void*) override {
        return image_dir_load_next(dir_);
    }
};

// Filtre Scale x3 (parallel)
class ScaleFilter : public tbb::filter {
public:
    ScaleFilter() : tbb::filter(parallel) {}
    void* operator()(void* item) override {
        image_t* in = static_cast<image_t*>(item);
        if (!in) return nullptr;
        image_t* out = filter_scale_up(in, 3);
        image_destroy(in);
        return out;
    }
};

// Filtre Flip vertical (parallel)
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

// Filtre de sauvegarde (serial_in_order)
class SaveFilter : public tbb::filter {
    image_dir_t* dir_;
public:
    explicit SaveFilter(image_dir_t* dir)
        : tbb::filter(serial_in_order), dir_(dir) {}
    void* operator()(void* item) override {
        image_t* img = static_cast<image_t*>(item);
        if (!img) return nullptr;
        image_dir_save(dir_, img);
        image_destroy(img);
        return nullptr;
    }
};

// Point d'entrée pipeline TBB
int pipeline_tbb(image_dir_t* image_dir) {
    // Initialiser le runtime TBB avec n threads
    int nthreads = std::max(2u, std::thread::hardware_concurrency());
    tbb::task_scheduler_init init(nthreads);

    // Nombre de tokens "en vol"
    size_t tokens = nthreads * 8;

    // Créer les filtres de base
    LoadFilter  f_load(image_dir);
    SaveFilter  f_save(image_dir);

    // Ajouter au pipeline
    tbb::pipeline pipe;
    pipe.add_filter(f_load);

    // Ajouter plusieurs instances de Scale
    const int scale_instances = std::min(4, nthreads);
    for (int i = 0; i < scale_instances; ++i) {
        pipe.add_filter(*new ScaleFilter());  // ATTENTION : pas géré automatiquement
    }

    // Ajouter plusieurs instances de Flip
    const int flip_instances = std::min(4, nthreads);
    for (int i = 0; i < flip_instances; ++i) {
        pipe.add_filter(*new FlipFilter());
    }

    pipe.add_filter(f_save);

    pipe.run(tokens);
    pipe.clear();

    return 0;
}
