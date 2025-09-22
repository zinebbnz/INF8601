// source/pipeline-tbb.cpp
#include <tbb/flow_graph.h>
#include <cstdio>
#include <thread>
#include <algorithm>

extern "C" {
#include "filter.h"
#include "image.h"
#include "pipeline.h"
}

// 1) Load
struct LoadBody {
    explicit LoadBody(image_dir_t* dir) : dir_(dir) {}
    bool operator()(image_t*& out) {
        out = image_dir_load_next(dir_);
        return out != nullptr;
    }
private:
    image_dir_t* dir_;
};

// 2) Scale ×3
struct ScaleBody {
    image_t* operator()(image_t* in) const {
        if (!in) return nullptr;
        image_t* out = filter_scale_up(in, 3);
        image_destroy(in);
        return out;
    }
};

// 3) Flip vertical
struct FlipBody {
    image_t* operator()(image_t* in) const {
        if (!in) return nullptr;
        image_t* out = filter_vertical_flip(in);
        image_destroy(in);
        return out;
    }
};

// 4) Save
struct SaveBody {
    explicit SaveBody(image_dir_t* dir) : dir_(dir) {}
    tbb::flow::continue_msg operator()(image_t* img) const {
        if (img) {
            image_dir_save(dir_, img);
            std::printf(".");
            std::fflush(stdout);
            image_destroy(img);
        }
        return tbb::flow::continue_msg{};
    }
private:
    image_dir_t* dir_;
};

int pipeline_tbb(image_dir_t* image_dir) {
    using namespace tbb::flow;
    graph g;

    unsigned par = std::max(2u, std::thread::hardware_concurrency());

    // Noeuds
    source_node<image_t*> load(g, LoadBody{image_dir}, false);
    function_node<image_t*, image_t*> scale(g, par, ScaleBody{});
    function_node<image_t*, image_t*> flip (g, par, FlipBody{});
    function_node<image_t*, continue_msg> save(g, 1, SaveBody{image_dir});

    // Connexions : types cohérents
    make_edge(load, scale);
    make_edge(scale, flip);
    make_edge(flip, save);

    // Exécution
    load.activate();
    g.wait_for_all();

    std::printf("\n");
    return 0;
}
