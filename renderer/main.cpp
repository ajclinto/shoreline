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
#include "tile.h"
#include "ImathColor.h"
#include "ImathColorAlgo.h"
#include "tree.h"

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

static std::mutex s_tile_mutex;

int main(int argc, char *argv[])
{
    po::options_description desc("Options");
    desc.add_options()
        ("help", "produce help message")
        ("shared_mem", po::value<std::string>()->required(), "shared memory file")
        ("outpipe", po::value<int>()->required(), "output pipe")
        ("inpipe", po::value<int>()->required(), "input pipe")
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

    int outpipe_fd = vm["outpipe"].as<int>();
    int inpipe_fd = vm["inpipe"].as<int>();

    RES res;
    res.xres = 800;
    res.yres = 600;
    res.tres = 64;
    res.nsamples = 16;
    res.nthreads = 4;

    int shm_fd = shm_open(vm["shared_mem"].as<std::string>().c_str(),
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

    std::vector<Imath::C3f> pixelcolors(res.xres * res.yres);

    struct THREAD_DATA
    {
        std::vector<RTCRayHit> rayhits;
        RTCIntersectContext context;
        Imath::Rand32 pixel_rand;
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
        rtcInitIntersectContext(&thread_data[i].context);
    }

    int tiles_pushed = tiles.size();
    tbb::parallel_for_each(tiles.begin(), tiles.end(), [&](TILE &tile, tbb::feeder<TILE>& feeder)
    {
        auto &context = thread_data[tile.tid].context;
        auto &rayhits = thread_data[tile.tid].rayhits;
        for (int y = 0; y < tile.ysize; y++)
        {
            for (int x = 0; x < tile.xsize; x++)
            {
                float sx, sy;
                sobol02(tile.sidx, tile.xoff + x, tile.yoff + y, sx, sy);
                int poff = y*tile.xsize+x;
                float ox = (tile.xoff + x + sx) / (float)res.xres;
                float oy = 0;
                float oz = (tile.yoff + y + sy) / (float)res.yres;
                ox = 2*ox - 1;
                oz = 2*oz;
                RTCRayHit &rayhit = rayhits[poff];
                rayhit.ray.org_x = ox;
                rayhit.ray.org_y = -1;
                rayhit.ray.org_z = oz;
                rayhit.ray.dir_x = 0;
                rayhit.ray.dir_y = 1;
                rayhit.ray.dir_z = 0;
                rayhit.ray.tnear = 0;
                rayhit.ray.tfar = std::numeric_limits<float>::infinity();
                rayhit.ray.mask = -1;
                rayhit.ray.flags = 0;
                rayhit.hit.geomID = RTC_INVALID_GEOMETRY_ID;
                rayhit.hit.instID[0] = RTC_INVALID_GEOMETRY_ID;
            }
        }

        rtcIntersect1M(scene, &context, rayhits.data(), tile.ysize * tile.xsize, sizeof(RTCRayHit));

        for (int y = 0; y < tile.ysize; y++)
        {
            for (int x = 0; x < tile.xsize; x++)
            {
                int poff = y*tile.xsize+x;
                const RTCRayHit &rayhit = rayhits[poff];
                if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
                {
                    Imath::V3f n(rayhit.hit.Ng_x, rayhit.hit.Ng_y, rayhit.hit.Ng_z);
                    n.normalize();
                    n += Imath::V3f(1.0);
                    n *= 0.5;

                    int ioff = (y + tile.yoff) * res.xres + x + tile.xoff;
                    pixelcolors[ioff] += n;
                }
            }
        }

        for (int y = 0; y < tile.ysize; y++)
        {
            for (int x = 0; x < tile.xsize; x++)
            {
                int poff = y*tile.xsize+x;
                int ioff = (y + tile.yoff) * res.xres + x + tile.xoff;
                uint32_t val = Imath::rgb2packed(pixelcolors[ioff] / (tile.sidx+1));
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

    return 0;
}
