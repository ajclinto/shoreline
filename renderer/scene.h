/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef SCENE_H
#define SCENE_H

#include <iostream>
#include <nlohmann/json.hpp>
#include <embree3/rtcore.h>
#include "ImathColor.h"
#include "ImathColorAlgo.h"
#include "tile.h"
#include "shading.h"

class SCENE
{
public:
     SCENE();
    ~SCENE();

    void load(std::istream &is);
    void update(std::istream &is);
    int render();

private:
    void create_geometry();
    void clear_geometry();

    void fill_sample_caches();

    void render_tile(const TILE &tile,
                     const RES &res,
                     const SUN_SKY_LIGHT &light,
                     const Imath::M44f &camera_xform,
                     float igamma);

private:
    nlohmann::json json_scene;

    size_t   m_shm_size = 0;
    uint    *m_shared_data = nullptr;

    RTCDevice device = nullptr;

    // Geometry
    RTCScene scene = nullptr;
    std::vector<int> shader_index; // Indexed by geometry ID
    std::vector<int> inst_shader_index; // Indexed by instance geometry ID

    // These vectors are aligned
    std::vector<BRDF> shaders;
    std::vector<std::string> shader_names;

    // Rendered image
    std::vector<Imath::C3f> pixelcolors;

    // Cached per-thread data
    // {
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
    std::vector<THREAD_DATA> thread_data;
    // }

    // Sampling data
    // {
    std::vector<uint32_t> seeds;

    const uint32_t p_hash_bits = 6;
    const uint32_t p_hash_size = (1<<p_hash_bits);
    const uint32_t p_hash_mask = p_hash_size - 1;
    std::vector<std::pair<uint32_t, uint32_t>> p_hash;

    const std::pair<uint32_t, uint32_t> &p_hash_eval(int x, int y) const
    {
        return p_hash[(y&p_hash_mask)*p_hash_size + (x&p_hash_mask)];
    };

    const uint32_t i_hash_bits = 6;
    const uint32_t i_hash_size = (1<<p_hash_bits);
    const uint32_t i_hash_mask = p_hash_size - 1;
    std::vector<Imath::C3f> i_hash;

    const Imath::C3f &i_hash_eval(int inst) const
    {
        return i_hash[(inst&i_hash_mask)];
    };
    // }
};

#endif // SCENE_H
