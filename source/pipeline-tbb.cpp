#define TBB_SUPPRESS_DEPRECATED_MESSAGES 1
#include <tbb/pipeline.h>
#include <tbb/task_scheduler_init.h>
#include <thread>
#include <cstdio>

extern "C"
{
#include "image.h"
#include "filter.h"
#include "pipeline.h"
}

// 1) Lecture (serial_in_order)
class LoadFilter : public tbb::filter
{
public:
    explicit LoadFilter(image_dir_t *dir)
        : tbb::filter(tbb::filter::serial_in_order), dir_(dir) {}
    void *operator()(void *) override
    {
        return image_dir_load_next(dir_); // nullptr => fin
    }

private:
    image_dir_t *dir_;
};

// 2) Scale x3 (parallel)
class ScaleFilter : public tbb::filter
{
public:
    ScaleFilter() : tbb::filter(tbb::filter::parallel) {}
    void *operator()(void *item) override
    {
        image_t *in = static_cast<image_t *>(item);
        if (!in)
            return nullptr;
        image_t *out = filter_scale_up(in, 3);
        image_destroy(in);
        return out;
    }
};

// 3) Flip vertical (parallel)
class FlipFilter : public tbb::filter
{
public:
    FlipFilter() : tbb::filter(tbb::filter::parallel) {}
    void *operator()(void *item) override
    {
        image_t *in = static_cast<image_t *>(item);
        if (!in)
            return nullptr;
        image_t *out = filter_vertical_flip(in);
        image_destroy(in);
        return out;
    }
};

// 4) Sauvegarde (serial_in_order)
class SaveFilter : public tbb::filter
{
public:
    explicit SaveFilter(image_dir_t *dir)
        : tbb::filter(tbb::filter::serial_in_order), dir_(dir) {}
    void *operator()(void *item) override
    {
        image_t *img = static_cast<image_t *>(item);
        if (!img)
            return nullptr;
        image_dir_save(dir_, img);
        std::fputc('.', stdout);
        std::fflush(stdout);
        image_destroy(img);
        return nullptr;
    }

private:
    image_dir_t *dir_;
};

int pipeline_tbb(image_dir_t *image_dir)
{
    // Threads et arena explicites pour éviter un runtime à 1 thread
    int hw = (int)std::max(2u, std::thread::hardware_concurrency());
    tbb::task_scheduler_init init(hw);

    try
    {
        LoadFilter f_load(image_dir);
        ScaleFilter f_scale;
        FlipFilter f_flip;
        SaveFilter f_save(image_dir);

        tbb::pipeline pipe;
        pipe.add_filter(f_load);
        pipe.add_filter(f_scale);
        pipe.add_filter(f_flip);
        pipe.add_filter(f_save);

        // Beaucoup de tokens pour remplir les étages parallèles
        const size_t tokens = (size_t)hw * 12;

        pipe.run(tokens); // ← nécessaire pour lancer le pipeline
        pipe.clear();

        std::puts("");
        return 0;
    }
    catch (...)
    {
        return -1;
    }
}
