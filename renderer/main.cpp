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

namespace po = boost::program_options;

void errorFunction(void* userPtr, enum RTCError error, const char* str)
{
    printf("error %d: %s\n", error, str);
}

RTCDevice initializeDevice()
{
    RTCDevice device = rtcNewDevice(NULL);

    if (!device)
    {
        printf("error %d: cannot create device\n", rtcGetDeviceError(NULL));
    }

    rtcSetDeviceErrorFunction(device, errorFunction, NULL);
    return device;
}

RTCScene initializeScene(RTCDevice device)
{
    RTCScene scene = rtcNewScene(device);

    TREE tree(4, 0, 0.01, 1.2, 0.2,
            {0, 50, 15, 15, 2, 2}, {40, 90, 60, 60, 0},
            {8, 16, 12, 1, 1},
            {2.0, 0.3, 1, 0.3, 0.5}, {0, 0.1, 0, 0, 0},
            {0, 40, 30, 30, 20, 20}, {0, 10, 10, 10, 10, 10},
            {0, 140, 140, 140, 140, 140}, {0, 20, 20, 20, 20, 20},
            nullptr, 1);

    tree.build();
    tree.embree_geometry(device, scene);

    PLANE plane(Imath::V3f(0, 0, 0), Imath::V3f(1, 0, 0), Imath::V3f(0, 1, 0));
    plane.embree_geometry(device, scene);

    rtcCommitScene(scene);

    return scene;
}

inline float sobol2(unsigned int n, unsigned int seed)
{
    for (unsigned int v = 1 << 31; n != 0; n >>= 1, v ^= v >> 1)
    {
        if (n & 0x1) seed ^= v;
    }
    return (float)seed / (float)0x100000000LL;
}

inline float vandercorput(unsigned int n, unsigned int seed)
{
    n = (n << 16) | (n >> 16);
    n = ((n & 0x00ff00ff) << 8) | ((n & 0xff00ff00) >> 8);
    n = ((n & 0x0f0f0f0f) << 4) | ((n & 0xf0f0f0f0) >> 4);
    n = ((n & 0x33333333) << 2) | ((n & 0xcccccccc) >> 2);
    n = ((n & 0x55555555) << 1) | ((n & 0xaaaaaaaa) >> 1);
    n ^= seed;
    return (float)n / (float)0x100000000LL;
}

inline void sobol02(unsigned int n,
                    unsigned int seedx, unsigned int seedy,
                    float &sx, float &sy)
{
    sx = vandercorput(n, seedx);
    sy = sobol2(n, seedy);
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

static std::mutex s_tile_mutex;

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
                {"name", "xres"},
                {"type", "int"},
                {"default", 800},
                {"min", 1},
                {"max", 3200}
            },
            {
                {"name", "yres"},
                {"type", "int"},
                {"default", 600},
                {"min", 1},
                {"max", 2400}
            },
            {
                {"name", "tres"},
                {"type", "int"},
                {"default", 64},
                {"min", 1},
                {"max", 256}
            },
            {
                {"name", "samples"},
                {"type", "int"},
                {"default", 16},
                {"min", 1},
                {"max", 256}
            },
            {
                {"name", "sun_elevation"},
                {"type", "float"},
                {"default", 30.0},
                {"min", 0.0},
                {"max", 90.0}
            },
            {
                {"name", "sun_azimuth"},
                {"type", "float"},
                {"default", 0.0},
                {"min", 0.0},
                {"max", 360.0}
            }
        };
        std::cout << json_ui;
        return 0;
    }

    nlohmann::json json_scene;
    std::cin >> json_scene;

    int outpipe_fd = json_scene["outpipe"];
    int inpipe_fd =  json_scene["inpipe"];

    RES res;
    res.xres = json_scene["xres"];
    res.yres = json_scene["yres"];
    res.tres = json_scene["tres"];
    res.nsamples = json_scene["samples"];
    res.nthreads = 4;

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

    int tcount = res.tile_count();

    // Embree setup
    RTCDevice device = initializeDevice();
    RTCScene scene = initializeScene(device);

    BRDF brdf(json_scene);
    SUN_SKY_LIGHT light(json_scene);

    std::vector<Imath::C3f> pixelcolors(res.xres * res.yres);

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
    std::vector<TILE> tiles(res.nthreads);

    for (int i = 0; i < res.nthreads; i++)
    {
        TILE &tile = tiles[i];
        if (read(inpipe_fd, &tile, sizeof(TILE)) < 0)
        {
            perror("read");
        }

        thread_data[i].rayhits.resize(res.tres * res.tres);
        thread_data[i].occrays.resize(res.tres * res.tres * 2);
        rtcInitIntersectContext(&thread_data[i].context);
    }

    // Generate a fixed size tile of random seeds to randomize sampling
    // sequences. I'm using a fixed tile size rather than the tile interface
    // tres since it's useful for it to be a power of 2 and I'd rather the
    // rendered image not change when the tile size changes.
    // {
    Imath::Rand32 pixel_rand;
    std::vector<uint32_t> seeds(4);
    for (auto &seed : seeds)
    {
        seed = pixel_rand.nexti();
    }

    const uint32_t p_hash_bits = 5;
    const uint32_t p_hash_size = (1<<p_hash_bits);
    const uint32_t p_hash_mask = p_hash_size - 1;
    std::vector<uint32_t> p_hash(p_hash_size * p_hash_size);
    for (auto &h : p_hash)
    {
        h = pixel_rand.nexti();
    }

    auto p_hash_eval = [&](int x, int y)
    {
        return p_hash[(y&p_hash_mask)*p_hash_size + (x&p_hash_mask)];
    };
    // }

    int tiles_pushed = tiles.size();
    tbb::parallel_for_each(tiles.begin(), tiles.end(), [&](TILE &tile, tbb::feeder<TILE>& feeder)
    {
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
                float sx, sy;
                uint32_t h_ioff = p_hash_eval(px, py);
                sobol02(tile.sidx, h_ioff^seeds[0], h_ioff^seeds[1], sx, sy);
                float dx = (tile.xoff + x + sx) / (float)res.xres;
                float dz = (tile.yoff + y + sy) / (float)res.yres;
                dx = dx - 0.5F;
                dz = dz - 0.5F;
                Imath::V3f org(0, -2.5, 1);
                Imath::V3f dir(dx, 1, dz);
                RTCRayHit &rayhit = rayhits[poff];
                init_rayhit(rayhit, org, dir);
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
                if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
                {
                    Imath::V3f org(rayhit.ray.org_x, rayhit.ray.org_y, rayhit.ray.org_z);
                    Imath::V3f dir(rayhit.ray.dir_x, rayhit.ray.dir_y, rayhit.ray.dir_z);
                    org += dir*rayhit.ray.tfar;

                    Imath::V3f n(rayhit.hit.Ng_x, rayhit.hit.Ng_y, rayhit.hit.Ng_z);
                    n.normalize();
                    if (n.dot(dir) > 0)
                    {
                        n = -n;
                    }

                    const float bias = 0.001F;
                    org += bias*n;

                    int px = x + tile.xoff;
                    int py = y + tile.yoff;
                    uint32_t h_ioff = p_hash_eval(px, py);

                    SHADOW_TEST test;
                    test.ioff = ioff;

                    float sx, sy;
                    sobol02(tile.sidx, h_ioff^seeds[2], h_ioff^seeds[3], sx, sy);

                    Imath::C3f b_clr;
                    Imath::V3f b_dir;
                    Imath::C3f l_clr;
                    Imath::V3f l_dir;
                    brdf.mis_sample(light, b_clr, b_dir, l_clr, l_dir, n, sx, sy);

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
                clr[0] = std::min(powf(clr[0], 1.0F/2.2F), 1.0F);
                clr[1] = std::min(powf(clr[1], 1.0F/2.2F), 1.0F);
                clr[2] = std::min(powf(clr[2], 1.0F/2.2F), 1.0F);
                uint32_t val = Imath::rgb2packed(clr);
                shared_data[res.tres*res.tres*tile.tid + poff] = val;
            }
        }

        std::lock_guard<std::mutex> lock(s_tile_mutex);

        if (write(outpipe_fd, &tile, sizeof(TILE)) < 0)
        {
            perror("write");
        }

        if (tiles_pushed++ < tcount)
        {
            if (read(inpipe_fd, &tile, sizeof(TILE)) < 0)
            {
                perror("read");
            }
            feeder.add(tile);
        }
    });
    printf("done %d tiles\n", tcount);

    // Embree cleanup
    rtcReleaseScene(scene);
    rtcReleaseDevice(device);

    shm_unlink(shm_name.c_str());

    return 0;
}
