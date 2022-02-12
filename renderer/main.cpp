/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include <boost/program_options.hpp>
#include <iostream>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include "scene.h"
#include "terrain.h"
#include "tree.h"
#include "shading.h"
#include "common.h"

// Embree suggested optimization
#include <xmmintrin.h>
#include <pmmintrin.h>

namespace po = boost::program_options;

int main(int argc, char *argv[])
{
    po::options_description desc("Options");
    desc.add_options()
        ("help", "Produce help message")
        ("dump_ui", "Print UI parameter .json")
    ;

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, desc), vm);

    if (vm.count("help"))
    {
        std::cout << desc << "\n";
        return 0;
    }

    try
    {
        po::notify(vm);
    }
    catch(std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    if (vm.count("dump_ui"))
    {
        nlohmann::json json_ui = {
            {
                {"name", "res"},
                {"type", "int"},
                {"vector_size", 2},
                {"default", {800,600}},
                {"min", 1},
                {"max", 10000}
            },
            {
                {"name", "tres"},
                {"type", "int"},
                {"default", 64},
                {"scale", "log"},
                {"min", 1},
                {"max", 1024}
            },
            {
                {"name", "nthreads"},
                {"type", "int"},
                {"default", 4},
                {"scale", "log"},
                {"min", 1},
                {"max", 128}
            },
            {
                {"name", "samples"},
                {"type", "int"},
                {"default", 16},
                {"scale", "log"},
                {"min", 1},
                {"max", 1024}
            },
            {
                {"name", "camera_pos"},
                {"type", "float"},
                {"vector_size", 3},
                {"default", {0, -2.5, 1}},
                {"min", -1000},
                {"max",  1000}
            },
            {
                {"name", "camera_pitch"},
                {"type", "float"},
                {"default", 0},
                {"min", -90.0},
                {"max", 90.0}
            },
            {
                {"name", "camera_yaw"},
                {"type", "float"},
                {"default", 0},
                {"min", -180.0},
                {"max", 180.0}
            },
            {
                {"name", "camera_roll"},
                {"type", "float"},
                {"default", 0},
                {"min", -180.0},
                {"max", 180.0}
            },
            {
                {"name", "field_of_view"},
                {"type", "float"},
                {"default", 60.0},
                {"min", 0.001},
                {"max", 90.0}
            },
            {
                {"name", "sampling_seed"},
                {"type", "int"},
                {"default", 0},
                {"min", 0},
                {"max", 10}
            },
            {
                {"name", "gamma"},
                {"type", "float"},
                {"default", 2.2},
                {"min", 1.0},
                {"max", 2.2}
            },
            {
                {"name", "shading"},
                {"type", "string"},
                {"default", "physical"},
                {"values", {"physical", "geomID", "primID"}}
            },
            {
                {"name", "reflect_limit"},
                {"type", "int"},
                {"default", 2},
                {"min", 1},
                {"max", 10}
            }
        };
        SUN_SKY_LIGHT::publish_ui(json_ui);
        BRDF::publish_ui(json_ui);
        TERRAIN::publish_ui(json_ui);
        TREE::publish_ui(json_ui);
        FOREST::publish_ui(json_ui);
        std::cout << json_ui << std::endl;
        return 0;
    }

    // Embree suggested optimization
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    SCENE scene;
    scene.load(std::cin);

    // Main event loop
    int rval = 0;
    while (!rval)
    {
        rval = scene.render();

        scene.update(std::cin);
    }

    return rval;
}

