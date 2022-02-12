/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef SHADING_H
#define SHADING_H

#include "ImathVec.h"
#include "ImathColor.h"
#include "ImathColorAlgo.h"
#include "ImathMatrix.h"
#include <nlohmann/json.hpp>

class SUN_SKY_LIGHT
{
public:
    SUN_SKY_LIGHT(const nlohmann::json &parameters);

    static void publish_ui(nlohmann::json &json_ui);

    void sample(Imath::C3f &clr, float &pdf, Imath::V3f &dir, float sx, float sy) const;
    void evaluate(Imath::C3f &clr, float &pdf, const Imath::V3f &dir) const;

private:
    Imath::C3f m_sun_clr;
    Imath::C3f m_sky_1_clr;
    Imath::C3f m_sky_2_clr;
    Imath::C3f m_sky_3_clr;
    Imath::C3f m_sun_dir;
    float m_sun_angle;
    float m_sun_h;
    float m_sun_intensity;

    Imath::C3f m_sun_u;
    Imath::C3f m_sun_v;
};

class BRDF
{
public:
    BRDF() {}
    BRDF(const nlohmann::json &color);

    static void publish_ui(nlohmann::json &json_ui);

    static void create_shaders(const nlohmann::json &parameters,
                               std::vector<BRDF> &shaders,
                               std::vector<std::string> &shader_names);

    static int find_shader(const std::vector<std::string> &shader_names, const std::string &s)
    {
        auto it = std::find(shader_names.begin(), shader_names.end(), s);
        return it != shader_names.end() ? std::distance(shader_names.begin(), it) : 0;
    }
    static void set_shader_index(std::vector<int> &shader_index, unsigned int id, int shader_id)
    {
        if (shader_index.size() <= id)
        {
            shader_index.resize(id+1, -1);
        }
        shader_index[id] = shader_id;
    }

    void set_transmit_ratio(float ratio) { m_transmit_ratio = ratio; }

    // Controls whether reflection rays are shaded
    void set_reflective() { m_reflect = true; }
    bool is_reflective() const { return m_reflect; }

    // Controls whether smooth normals are interpolated (The normal vertex
    // buffer in slot 0 must be available on the geometry)
    void set_smooth_N() { m_smooth_N = true; }
    bool is_smooth_N() const { return m_smooth_N; }

    void sample(Imath::C3f &clr, float &pdf, Imath::V3f &dir, const Imath::V3f &n, const Imath::V3f &d, float sx, float sy) const;
    void evaluate(Imath::C3f &clr, float &pdf, const Imath::V3f &dir, const Imath::V3f &n, const Imath::V3f &d) const;

    void mis_sample(const SUN_SKY_LIGHT &light,
                    Imath::C3f &b_clr, Imath::V3f &b_dir,
                    Imath::C3f &l_clr, Imath::V3f &l_dir,
                    const Imath::V3f &n,
                    const Imath::V3f &d,
                    float bsx, float bsy,
                    float lsx, float lsy) const;

    void modulate_color(const Imath::C3f &offset)
    {
        m_clr += offset;
    }

private:
    Imath::C3f m_clr;
    float m_transmit_ratio = 0;
    bool m_reflect = false;
    bool m_smooth_N = false;
};

#endif // SHADING_H
