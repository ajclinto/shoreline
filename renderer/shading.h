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

    void set_transmit_ratio(float ratio) { m_transmit_ratio = ratio; }

    void sample(Imath::C3f &clr, float &pdf, Imath::V3f &dir, const Imath::V3f &n, float sx, float sy) const;
    void evaluate(Imath::C3f &clr, float &pdf, const Imath::V3f &dir, const Imath::V3f &n) const;

    void mis_sample(const SUN_SKY_LIGHT &light,
                    Imath::C3f &b_clr, Imath::V3f &b_dir,
                    Imath::C3f &l_clr, Imath::V3f &l_dir,
                    const Imath::V3f &n,
                    float bsx, float bsy,
                    float lsx, float lsy) const;

    void modulate_color(const Imath::C3f &offset)
    {
        m_clr += offset;
    }

private:
    Imath::C3f m_clr;
    float m_transmit_ratio = 0;
};

#endif // SHADING_H
