/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "scene.h"
#include <tbb/parallel_for_each.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include "tree.h"
#include "common.h"


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

SCENE::SCENE()
{
    device = rtcNewDevice("start_threads=1,set_affinity=1");

    if (!device)
    {
        printf("error %d: cannot create device\n", rtcGetDeviceError(NULL));
    }

    rtcSetDeviceErrorFunction(device, errorFunction, NULL);
    rtcSetDeviceMemoryMonitorFunction(device, memoryFunction, nullptr);
}

SCENE::~SCENE()
{
    // Embree cleanup
    clear_geometry();
    rtcReleaseDevice(device);

    if (m_shared_data)
    {
        munmap(m_shared_data, m_shm_size);
        m_shared_data = nullptr;
    }
}

void SCENE::load(std::istream &is)
{
    is >> json_scene;
}

void SCENE::update(std::istream &is)
{
    nlohmann::json json_updates;
    is >> json_updates;

    for (const auto &p : json_updates.items())
    {
        json_scene[p.key()] = p.value();
    }

    // Crudely identify some parameters which require a scene rebuild
    nlohmann::json geometry_affecting_parameters = nlohmann::json::array();
    TREE::publish_ui(geometry_affecting_parameters);
    FOREST::publish_ui(geometry_affecting_parameters);

    for (const auto &geo_p : geometry_affecting_parameters)
    {
        for (const auto &key_value : geo_p.items())
        {
            if (key_value.key() == "name" && json_updates.find(key_value.value()) != json_updates.end())
            {
                clear_geometry();
                break;
            }
        }
    }
}

void SCENE::create_geometry()
{
    scene = rtcNewScene(device);

    if (json_scene["forest_levels"] > 0.0F)
    {
        FOREST forest(json_scene);
        forest.embree_geometry(device, scene, inst_shader_index, shader_names);
    }
    else
    {
        TREE tree(json_scene);

        tree.build();
        tree.embree_geometry(device, scene, shader_index, shader_names);
    }

    PLANE plane(json_scene, Imath::V4f(0, 0, 0, 10000.0F));
    plane.embree_geometry(device, scene, shader_index);

    rtcCommitScene(scene);

    printf("Embree memory: %ldMb\n", (ssize_t)s_embree_memory/1000000);

}

void SCENE::clear_geometry()
{
    rtcReleaseScene(scene);
    scene = nullptr;

    shader_index.clear();
    inst_shader_index.clear();
}

static Imath::V3f json_to_vector(const nlohmann::json &vec)
{
    return Imath::V3f(vec[0], vec[1], vec[2]);
}

void SCENE::fill_sample_caches()
{
    // Generate a fixed size tile of random seeds to randomize sampling
    // sequences. I'm using a fixed tile size rather than the tile interface
    // tres since it's useful for it to be a power of 2 and I'd rather the
    // rendered image not change when the tile size changes.
    // {
    Imath::Rand32 pixel_rand(json_scene["sampling_seed"]);
    seeds.resize(16);
    for (auto &seed : seeds)
    {
        seed = pixel_rand.nexti();
    }

    p_hash.resize(p_hash_size * p_hash_size);
    for (auto &h : p_hash)
    {
        h.first = pixel_rand.nexti();
        h.second = pixel_rand.nexti();
    }
    // }

    // {
    float inst_color_variance = 0.2F;
    Imath::Rand32 inst_rand;
    const uint32_t i_hash_bits = 6;
    const uint32_t i_hash_size = (1<<p_hash_bits);
    const uint32_t i_hash_mask = p_hash_size - 1;
    i_hash.resize(i_hash_size);
    for (auto &h : i_hash)
    {
        h[0] = inst_rand.nextf(0, inst_color_variance);
        h[1] = inst_rand.nextf(0, inst_color_variance);
        h[2] = inst_rand.nextf(0, inst_color_variance);
    }
    // }
}


int SCENE::render()
{
    int outpipe_fd = json_scene["outpipe"];
    int inpipe_fd =  json_scene["inpipe"];
    int shm_fd = json_scene["shared_mem"];

    RES res;
    res.xres = json_scene["res"][0];
    res.yres = json_scene["res"][1];
    res.tres = json_scene["tres"];
    res.nthreads = json_scene["nthreads"];
    res.nsamples = json_scene["samples"];

    // Prevent rendering different samples of the same tile in different
    // threads
    res.nthreads = std::min(res.nthreads, res.tile_count());

    if (res.shm_size() != m_shm_size)
    {
        if (m_shared_data)
        {
            munmap(m_shared_data, m_shm_size);
            m_shared_data = nullptr;
        }

        m_shm_size = res.shm_size();
        if (ftruncate(shm_fd, m_shm_size) == -1)
        {
            perror("ftruncate");
            return 1;
        }

        m_shared_data = (uint *)mmap(NULL, m_shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        if (m_shared_data == MAP_FAILED)
        {
            perror("mmap");
            return 1;
        }
    }

    if (write(outpipe_fd, &res, sizeof(RES)) < 0)
    {
        perror("write");
    }

    BRDF::create_shaders(json_scene, shaders, shader_names);

    // NOTE: Needs to be called after the write() above to avoid locking up the
    // UI. Also needs to be called after create_shaders()
    if (!scene)
    {
        create_geometry();
    }

    SUN_SKY_LIGHT light(json_scene);

    pixelcolors.assign(res.xres * res.yres, Imath::C3f(0));

    thread_data.resize(res.nthreads);
    for (int i = 0; i < res.nthreads; i++)
    {
        thread_data[i].rayhits.resize(res.tres * res.tres);
        thread_data[i].occrays.resize(res.tres * res.tres * 2);
        thread_data[i].shadow_test.reserve(res.tres * res.tres * 2);
        rtcInitIntersectContext(&thread_data[i].context);
    }

    fill_sample_caches();

    float aspect = (float)res.yres / (float)res.xres;
    float fov = json_scene["field_of_view"];
    fov = tan(radians(fov)/2.0F);
    fov *= 2.0F;

    Imath::M44f camera_xform;
    camera_xform.scale(Imath::V3f(fov, 1.0, fov*aspect));
    camera_xform *= Imath::M44f().rotate(Imath::V3f(0, -radians(json_scene["camera_roll"]), 0));
    camera_xform *= Imath::M44f().rotate(Imath::V3f(radians(json_scene["camera_pitch"]), 0, 0));
    camera_xform *= Imath::M44f().rotate(Imath::V3f(0, 0, -radians(json_scene["camera_yaw"])));
    camera_xform *= Imath::M44f().translate(json_to_vector(json_scene["camera_pos"]));

    float igamma = 1.0 / (float)json_scene["gamma"];

    int tcount = res.tile_count() * res.nsamples;
    std::atomic<int> tcomplete(0);

    // Note the use of grain size == 1 and simple_partitioner below to ensure
    // we get exactly nthreads tasks
    tbb::parallel_for(tbb::blocked_range<int>(0,res.nthreads,1),
                       [&](tbb::blocked_range<int>)
    {
        while (1)
        {
            TILE tile;
            if (read(inpipe_fd, &tile, sizeof(TILE)) < 0)
            {
                perror("read");
            }

            if (tile.xsize == 0) break;

            render_tile(tile, res, light, camera_xform, igamma);

            if (write(outpipe_fd, &tile, sizeof(TILE)) < 0)
            {
                perror("write");
            }

            tcomplete++;
        }
    }, tbb::simple_partitioner());

    printf("Done %d / %d tiles\n", (int)tcomplete, tcount);

    return 0;
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

void SCENE::render_tile(const TILE &tile, const RES &res, const SUN_SKY_LIGHT &light, const Imath::M44f &camera_xform, float igamma)
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
            auto [h_ioffx,h_ioffy] = p_hash_eval(px, py);
            uint32_t isx = h_ioffx ^ vandercorput(tile.sidx);
            uint32_t isy = h_ioffy ^ sobol2(tile.sidx);
            float sx = sample_to_float(isx, seeds[0]);
            float sy = sample_to_float(isy, seeds[1]);
            float dx = (px + sx) / (float)res.xres;
            float dz = (py + sy) / (float)res.yres;
            dx = ( dx - 0.5F);
            dz = (-dz + 0.5F);
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

                    Imath::V3f bias_org(org);
                    bias_org += ((dir.dot(n) >= 0) ? bias : -bias) * n;

                    init_ray(occrays[shadow_test.size()], bias_org, dir);
                    shadow_test.push_back(test);
                }
                if (l_clr != Imath::V3f(0))
                {
                    // Light sample
                    test.clr = l_clr;
                    dir = l_dir;

                    Imath::V3f bias_org(org);
                    bias_org += ((dir.dot(n) >= 0) ? bias : -bias) * n;

                    init_ray(occrays[shadow_test.size()], bias_org, dir);
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
            m_shared_data[res.tres*res.tres*tile.tid + poff] = val;
        }
    }
}

