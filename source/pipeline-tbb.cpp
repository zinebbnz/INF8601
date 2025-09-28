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

// 1) Lecture
class LoadFilter : public tbb::filter {
public:
    explicit LoadFilter(image_dir_t* dir)
        : tbb::filter(serial_in_order), dir_(dir) {}
    void* operator()(void*) override {
        return image_dir_load_next(dir_);
    }
private:
    image_dir_t* dir_;
};

// 2) Scale ×3 (parallèle sur lignes, sans parallel_for)
class ScaleFilter : public tbb::filter {
public:
    ScaleFilter() : tbb::filter(parallel) {}
    void* operator()(void* item) override {
        image_t* in = static_cast<image_t*>(item);
        if (!in) return nullptr;

        size_t sw = in->width;
        size_t sh = in->height;
        size_t dw = sw * 3;
        size_t dh = sh * 3;

        image_t* out = image_create(in->id, dw, dh);
        if (!out) return nullptr;

        // Parallélisme simple sur lignes
        std::vector<std::thread> threads;
        unsigned nthreads = std::thread::hardware_concurrency();
        size_t chunk = dh / nthreads;

        for (unsigned t = 0; t < nthreads; ++t) {
            size_t y_start = t * chunk;
            size_t y_end = (t == nthreads - 1) ? dh : y_start + chunk;
            threads.emplace_back([=]() {
                for (size_t y = y_start; y < y_end; ++y) {
                    size_t sy = y / 3;
                    for (size_t x = 0; x < dw; ++x) {
                        size_t sx = x / 3;
                        out->pixels[y * dw + x] = in->pixels[sy * sw + sx];
                    }
                }
            });
        }

        for (auto& th : threads) th.join();
        image_destroy(in);
        return out;
    }
};

// 3) Flip vertical (parallèle sur lignes, sans parallel_for)
class FlipFilter : public tbb::filter {
public:
    FlipFilter() : tbb::filter(parallel) {}
    void* operator()(void* item) override {
        image_t* in = static_cast<image_t*>(item);
        if (!in) return nullptr;

        size_t w = in->width;
        size_t h = in->height;

        image_t* out = image_create(in->id, w, h);
        if (!out) return nullptr;

        std::vector<std::thread> threads;
        unsigned nthreads = std::thread::hardware_concurrency();
        size_t chunk = h / nthreads;

        for (unsigned t = 0; t < nthreads; ++t) {
            size_t y_start = t * chunk;
            size_t y_end = (t == nthreads - 1) ? h : y_start + chunk;
            threads.emplace_back([=]() {
                for (size_t y = y_start; y < y_end; ++y) {
                    size_t dy = h - 1 - y;
                    for (size_t x = 0; x < w; ++x) {
                        out->pixels[dy * w + x] = in->pixels[y * w + x];
                    }
                }
            });
        }

        for (auto& th : threads) th.join();
        image_destroy(in);
        return out;
    }
};

// 4) Sauvegarde
class SaveFilter : public tbb::filter {
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
private:
    image_dir_t* dir_;
};

// Point d’entrée
int pipeline_tbb(image_dir_t* image_dir) {
    int nthreads = std::max(2u, std::thread::hardware_concurrency());
    tbb::task_scheduler_init init(nthreads);
    size_t tokens = nthreads * 4;

    LoadFilter  f_load(image_dir);
    ScaleFilter f_scale;
    FlipFilter  f_flip;
    SaveFilter  f_save(image_dir);

    tbb::pipeline pipe;
    pipe.add_filter(f_load);
    pipe.add_filter(f_scale);
    pipe.add_filter(f_flip);
    pipe.add_filter(f_save);

    pipe.run(tokens);
    pipe.clear();
    return 0;
}
