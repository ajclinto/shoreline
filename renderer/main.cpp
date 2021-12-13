/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include <boost/program_options.hpp>
#include <embree3/rtcore.h>
#include <tbb/parallel_for_each.h>
#include <iostream>
#include <sys/mman.h>
#include <fcntl.h>
#include <nlohmann/json.hpp>
#include "tile.h"
#include "ImathColor.h"
#include "ImathColorAlgo.h"
#include "tree.h"
#include "shading.h"
#include "common.h"

// Embree suggested optimization
#include <xmmintrin.h>
#include <pmmintrin.h>

namespace po = boost::program_options;

void errorFunction(void* userPtr, enum RTCError error, const char* str)
{
    printf("error %d: %s\n", error, str);
}

std::atomic<ssize_t> s_embree_memory(0);

bool memoryFunction(void* userPtr, ssize_t bytes, bool post)
{
    s_embree_memory += bytes;
    return true;
}

RTCDevice initializeDevice()
{
    RTCDevice device = rtcNewDevice("start_threads=1,set_affinity=1");

    if (!device)
    {
        printf("error %d: cannot create device\n", rtcGetDeviceError(NULL));
    }

    rtcSetDeviceErrorFunction(device, errorFunction, NULL);
    rtcSetDeviceMemoryMonitorFunction(device, memoryFunction, nullptr);
    return device;
}

RTCScene initializeScene(RTCDevice device, const nlohmann::json &json_scene,
                         std::vector<int> &shader_index,
                         std::vector<int> &inst_shader_index,
                         std::vector<BRDF> &shaders)
{
    RTCScene scene = rtcNewScene(device);

    if (json_scene["forest_levels"] > 0.0F)
    {
        FOREST forest(json_scene);
        forest.embree_geometry(device, scene, inst_shader_index, shaders);
    }
    else
    {
        TREE tree(json_scene);

        tree.build();
        tree.embree_geometry(device, scene, shader_index, shaders);
    }

    PLANE plane(json_scene, Imath::V4f(0, 0, 0, 10000.0F));
    plane.embree_geometry(device, scene, shader_index, shaders);

    rtcCommitScene(scene);

    return scene;
}

inline float sample_to_float(uint32_t n, uint32_t seed)
{
    n ^= seed;
    return (float)n / (float)0x100000000LL;
}

inline uint32_t sobol2(uint32_t n)
{
    uint32_t x = 0;
    for (uint32_t v = 1 << 31; n != 0; n >>= 1, v ^= v >> 1)
    {
        if (n & 0x1) x ^= v;
    }
    return x;
}

inline uint32_t vandercorput(uint32_t n)
{
    n = (n << 16) | (n >> 16);
    n = ((n & 0x00ff00ff) << 8) | ((n & 0xff00ff00) >> 8);
    n = ((n & 0x0f0f0f0f) << 4) | ((n & 0xf0f0f0f0) >> 4);
    n = ((n & 0x33333333) << 2) | ((n & 0xcccccccc) >> 2);
    n = ((n & 0x55555555) << 1) | ((n & 0xaaaaaaaa) >> 1);
    return n;
}

static inline void init_ray(RTCRay &ray, const Imath::V3f &org, const Imath::V3f &dir)
{
    ray.org_x = org[0];
    ray.org_y = org[1];
    ray.org_z = org[2];
    ray.dir_x = dir[0];
    ray.dir_y = dir[1];
    ray.dir_z = dir[2];
    ray.tnear = 0;
    ray.tfar = std::numeric_limits<float>::infinity();
    ray.mask = -1;
    ray.flags = 0;
}

static inline void init_rayhit(RTCRayHit &rayhit, const Imath::V3f &org, const Imath::V3f &dir)
{
    init_ray(rayhit.ray, org, dir);
    rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
    rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
}

static Imath::V3f json_to_vector(const nlohmann::json &vec)
{
    return Imath::V3f(vec[0], vec[1], vec[2]);
}

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
                {"default", 60.0F},
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
                {"name", "diffuse_color"},
                {"type", "color"},
                {"default", {0.5, 0.5, 0.5}}
            }
        };
        SUN_SKY_LIGHT::publish_ui(json_ui);
        TREE::publish_ui(json_ui);
        FOREST::publish_ui(json_ui);
        std::cout << json_ui << std::endl;
        return 0;
    }

    // Embree suggested optimization
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);

    nlohmann::json json_scene;
    std::cin >> json_scene;

    int outpipe_fd = json_scene["outpipe"];
    int inpipe_fd =  json_scene["inpipe"];

    RES res;
    res.xres = json_scene["res"][0];
    res.yres = json_scene["res"][1];
    res.tres = json_scene["tres"];
    res.nthreads = json_scene["nthreads"];
    res.nsamples = json_scene["samples"];

    // Prevent rendering different samples of the same tile in different
    // threads
    res.nthreads = std::min(res.nthreads, res.tile_count());

    std::string shm_name = json_scene["shared_mem"];
    int shm_fd = shm_open(shm_name.c_str(),
                          O_CREAT | O_RDWR,
                          S_IRUSR | S_IWUSR);
    if (shm_fd < 0)
    {
        perror("shm_open");
        return 1;
    }

    size_t shm_size = res.shm_size();
    if (ftruncate(shm_fd, shm_size) == -1)
    {
        perror("ftruncate");
        return 1;
    }

    uint *shared_data = (uint *)mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shared_data == MAP_FAILED)
    {
        perror("mmap");
        return 1;
    }

    if (write(outpipe_fd, &res, sizeof(RES)) < 0)
    {
        perror("write");
    }

    int tcount = res.tile_count() * res.nsamples;

    // Embree setup
    std::vector<int> shader_index;
    std::vector<int> inst_shader_index;
    std::vector<BRDF> shaders;
    RTCDevice device = initializeDevice();
    RTCScene scene = initializeScene(device, json_scene, shader_index, inst_shader_index, shaders);

    SUN_SKY_LIGHT light(json_scene);

    std::vector<Imath::C3f> pixelcolors(res.xres * res.yres, Imath::C3f(0));

    struct SHADOW_TEST
    {
        Imath::C3f  clr;
        int         ioff;
    };
    struct THREAD_DATA
    {
        std::vector<RTCRayHit> rayhits;
        std::vector<RTCRay> occrays;
        std::vector<SHADOW_TEST> shadow_test;
        RTCIntersectContext context;
    };

    std::vector<THREAD_DATA> thread_data(res.nthreads);

    for (int i = 0; i < res.nthreads; i++)
    {
        thread_data[i].rayhits.resize(res.tres * res.tres);
        thread_data[i].occrays.resize(res.tres * res.tres * 2);
        thread_data[i].shadow_test.reserve(res.tres * res.tres * 2);
        rtcInitIntersectContext(&thread_data[i].context);
    }

    // Generate a fixed size tile of random seeds to randomize sampling
    // sequences. I'm using a fixed tile size rather than the tile interface
    // tres since it's useful for it to be a power of 2 and I'd rather the
    // rendered image not change when the tile size changes.
    // {
    Imath::Rand32 pixel_rand(json_scene["sampling_seed"]);
    std::vector<uint32_t> seeds(16);
    for (auto &seed : seeds)
    {
        seed = pixel_rand.nexti();
    }

    const uint32_t p_hash_bits = 6;
    const uint32_t p_hash_size = (1<<p_hash_bits);
    const uint32_t p_hash_mask = p_hash_size - 1;
    std::vector<std::pair<uint32_t, uint32_t>> p_hash(p_hash_size * p_hash_size);
    for (auto &h : p_hash)
    {
        h.first = pixel_rand.nexti();
        h.second = pixel_rand.nexti();
    }

    auto p_hash_eval = [&](int x, int y)
    {
        return p_hash[(y&p_hash_mask)*p_hash_size + (x&p_hash_mask)];
    };
    // }

    // {
    float inst_color_variance = 0.2F;
    Imath::Rand32 inst_rand;
    const uint32_t i_hash_bits = 6;
    const uint32_t i_hash_size = (1<<p_hash_bits);
    const uint32_t i_hash_mask = p_hash_size - 1;
    std::vector<Imath::C3f> i_hash(i_hash_size);
    for (auto &h : i_hash)
    {
        h[0] = inst_rand.nextf(0, inst_color_variance);
        h[1] = inst_rand.nextf(0, inst_color_variance);
        h[2] = inst_rand.nextf(0, inst_color_variance);
    }

    auto i_hash_eval = [&](int inst)
    {
        return i_hash[(inst&i_hash_mask)];
    };
    // }

    float aspect = (float)res.yres / (float)res.xres;
    float fov = json_scene["field_of_view"];
    fov = tan(radians(fov)/2.0F);
    fov *= 2.0F;

    Imath::M44f camera_xform;
    camera_xform *= Imath::M44f().rotate(Imath::V3f(0, -radians(json_scene["camera_roll"]), 0));
    camera_xform *= Imath::M44f().rotate(Imath::V3f(radians(json_scene["camera_pitch"]), 0, 0));
    camera_xform *= Imath::M44f().rotate(Imath::V3f(0, 0, -radians(json_scene["camera_yaw"])));
    camera_xform *= Imath::M44f().translate(json_to_vector(json_scene["camera_pos"]));

    float gamma = json_scene["gamma"];
    float igamma = 1.0/gamma;

    // Note the use of grain size == 1 and simple_partitioner below to ensure
    // we get exactly tcount tasks
    tbb::parallel_for(tbb::blocked_range<int>(0,tcount,1),
                       [&](tbb::blocked_range<int>)
    {
        TILE tile;
        if (read(inpipe_fd, &tile, sizeof(TILE)) < 0)
        {
            perror("read");
        }

        auto &context = thread_data[tile.tid].context;
        auto &rayhits = thread_data[tile.tid].rayhits;
        auto &occrays = thread_data[tile.tid].occrays;
        auto &shadow_test = thread_data[tile.tid].shadow_test;

        // Primary rays
        for (int y = 0; y < tile.ysize; y++)
        {
            for (int x = 0; x < tile.xsize; x++)
            {
                int poff = y*tile.xsize+x;
                int px = x + tile.xoff;
                int py = y + tile.yoff;
                int ioff = py * res.xres + px;
                auto [h_ioffx,h_ioffy] = p_hash_eval(px, py);
                uint32_t isx = h_ioffx ^ vandercorput(tile.sidx);
                uint32_t isy = h_ioffy ^ sobol2(tile.sidx);
                float sx = sample_to_float(isx, seeds[0]);
                float sy = sample_to_float(isy, seeds[1]);
                float dx = (px + sx) / (float)res.xres;
                float dz = (py + sy) / (float)res.yres;
                dx = ( dx - 0.5F) * fov;
                dz = (-dz + 0.5F) * fov * aspect;
                Imath::V3f dir;
                camera_xform.multDirMatrix(Imath::V3f(dx, 1, dz), dir);
                RTCRayHit &rayhit = rayhits[poff];
                init_rayhit(rayhit, camera_xform.translation(), dir);
            }
        }

        rtcIntersect1M(scene, &context, rayhits.data(), tile.ysize * tile.xsize, sizeof(RTCRayHit));

        // Shadow rays
        shadow_test.clear();
        for (int y = 0; y < tile.ysize; y++)
        {
            for (int x = 0; x < tile.xsize; x++)
            {
                int poff = y*tile.xsize+x;
                int ioff = (y + tile.yoff) * res.xres + x + tile.xoff;
                const RTCRayHit &rayhit = rayhits[poff];
                Imath::V3f dir(rayhit.ray.dir_x, rayhit.ray.dir_y, rayhit.ray.dir_z);
                if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
                {
                    Imath::V3f org(rayhit.ray.org_x, rayhit.ray.org_y, rayhit.ray.org_z);
                    org += dir*rayhit.ray.tfar;

                    Imath::V3f n(rayhit.hit.Ng_x, rayhit.hit.Ng_y, rayhit.hit.Ng_z);

                    if (rayhit.hit.instID[0] != RTC_INVALID_GEOMETRY_ID)
                    {
                        auto geo = rtcGetGeometry(scene, rayhit.hit.instID[0]);
                        Imath::M44f xform;
                        rtcGetGeometryTransform(geo, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR, &xform);
                        auto tmp = n;
                        xform.multDirMatrix(tmp, n);
                    }

                    n.normalize();
                    if (n.dot(dir) > 0)
                    {
                        n = -n;
                    }

                    const float bias = 0.001F;
                    org += bias*n;

                    int px = x + tile.xoff;
                    int py = y + tile.yoff;
                    auto [h_ioffx,h_ioffy] = p_hash_eval(px, py);

                    SHADOW_TEST test;
                    test.ioff = ioff;

                    uint32_t isx = h_ioffx ^ vandercorput(tile.sidx);
                    uint32_t isy = h_ioffy ^ sobol2(tile.sidx);
                    float bsx = sample_to_float(isx, seeds[2]);
                    float bsy = sample_to_float(isy, seeds[3]);
                    float lsx = sample_to_float(isx, seeds[4]);
                    float lsy = sample_to_float(isy, seeds[5]);

                    Imath::C3f b_clr;
                    Imath::V3f b_dir;
                    Imath::C3f l_clr;
                    Imath::V3f l_dir;

                    BRDF brdf;
                    if (rayhit.hit.instID[0] != RTC_INVALID_GEOMETRY_ID)
                    {
                        brdf = shaders[inst_shader_index[rayhit.hit.geomID]];
                        brdf.modulate_color(i_hash_eval(rayhit.hit.instID[0]));
                    }
                    else
                    {
                        brdf = shaders[shader_index[rayhit.hit.geomID]];
                    }

                    brdf.mis_sample(light, b_clr, b_dir, l_clr, l_dir, n, bsx, bsy, lsx, lsy);

                    if (b_clr != Imath::V3f(0))
                    {
                        // BRDF sample
                        test.clr = b_clr;
                        dir = b_dir;

                        init_ray(occrays[shadow_test.size()], org, dir);
                        shadow_test.push_back(test);
                    }
                    if (l_clr != Imath::V3f(0))
                    {
                        // Light sample
                        test.clr = l_clr;
                        dir = l_dir;

                        init_ray(occrays[shadow_test.size()], org, dir);
                        shadow_test.push_back(test);
                    }
                }
                else
                {
                    Imath::C3f clr;
                    float pdf;
                    dir.normalize();
                    light.evaluate(clr, pdf, dir);
                    pixelcolors[ioff] += clr;
                }
            }
        }

        rtcOccluded1M(scene, &context, occrays.data(), shadow_test.size(), sizeof(RTCRay));

        // Add unshadowed lighting
        for (int i = 0; i < shadow_test.size(); i++)
        {
            const RTCRay &ray = occrays[i];
            if (ray.tfar >= 0)
            {
                int ioff = shadow_test[i].ioff;
                pixelcolors[ioff] += shadow_test[i].clr;
            }
        }

        // Finalize the tile
        for (int y = 0; y < tile.ysize; y++)
        {
            for (int x = 0; x < tile.xsize; x++)
            {
                int poff = y*tile.xsize+x;
                int ioff = (y + tile.yoff) * res.xres + x + tile.xoff;
                auto clr = pixelcolors[ioff] / (tile.sidx+1);
                // Gamma correction
                clr[0] = std::min(powf(std::max(clr[0], 0.0F), igamma), 1.0F);
                clr[1] = std::min(powf(std::max(clr[1], 0.0F), igamma), 1.0F);
                clr[2] = std::min(powf(std::max(clr[2], 0.0F), igamma), 1.0F);
                uint32_t val = Imath::rgb2packed(clr);
                shared_data[res.tres*res.tres*tile.tid + poff] = val;
            }
        }

        if (write(outpipe_fd, &tile, sizeof(TILE)) < 0)
        {
            perror("write");
        }

    }, tbb::simple_partitioner());

    printf("Done %d tiles. Embree memory: %ldMb\n", tcount, (ssize_t)s_embree_memory/1000000);

    // Embree cleanup
    rtcReleaseScene(scene);
    rtcReleaseDevice(device);

    shm_unlink(shm_name.c_str());

    return 0;
}
