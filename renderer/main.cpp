/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include <boost/program_options.hpp>
#include <embree3/rtcore.h>
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

    int shm_fd = shm_open(vm["shared_mem"].as<std::string>().c_str(), O_RDWR, S_IRUSR | S_IWUSR);
    assert(shm_fd >= 0);

    uint *shared_data = (uint *)mmap(NULL, res.tres*res.tres*sizeof(uint), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    assert(shared_data);

    if (!write(outpipe_fd, &res, sizeof(RES)))
    {
        perror("write");
    }

    int tcount = res.tile_count();

    // Embree setup
    RTCDevice device = initializeDevice();
    RTCScene scene = initializeScene(device);

    RTCIntersectContext context;
    rtcInitIntersectContext(&context);

    // Allocate and reuse a tile of RTCRayHit
    std::vector<RTCRayHit> rayhits(res.tres * res.tres);

    for (int i = 0; i < tcount; i++)
    {
        if (!write(outpipe_fd, &ASKTILE, sizeof(ASKTILE)))
        {
            perror("write");
        }

        TILE tile;
        if (!read(inpipe_fd, &tile, sizeof(TILE)))
        {
            perror("read");
        }
        if (tile.xsize == 0)
        {
            break;
        }

        for (int y = 0; y < tile.ysize; y++)
        {
            for (int x = 0; x < tile.xsize; x++)
            {
                float ox = (tile.xoff + x) / (float)res.xres;
                float oy = 0;
                float oz = (tile.yoff + y) / (float)res.yres;
                ox = 2*ox - 1;
                oz = 2*oz;
                RTCRayHit &rayhit = rayhits[y*tile.xsize+x];
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
                const RTCRayHit &rayhit = rayhits[y*tile.xsize+x];
                uint val = 0;
                if (rayhit.hit.geomID != RTC_INVALID_GEOMETRY_ID)
                {
                    Imath::V3f n(rayhit.hit.Ng_x, rayhit.hit.Ng_y, rayhit.hit.Ng_z);
                    n.normalize();
                    n += Imath::V3f(1.0);
                    n *= 0.5;
                    Imath::C3f c(n);
                    val = Imath::rgb2packed(c);
                }
                shared_data[y*tile.xsize + x] = val;
            }
        }

        if (!write(outpipe_fd, &TILEDATA, sizeof(TILEDATA)))
        {
            perror("write");
        }
        if (!write(outpipe_fd, &tile, sizeof(TILE)))
        {
            perror("write");
        }

    }
    printf("done %d tiles\n", tcount);

    // Embree cleanup
    rtcReleaseScene(scene);
    rtcReleaseDevice(device);

    return 0;
}
