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

    void sample(Imath::C3f &clr, float &pdf, Imath::V3f &dir, float sx, float sy) const;
    void evaluate(Imath::C3f &clr, float &pdf, const Imath::V3f &dir) const;

private:
    Imath::C3f m_sun_clr;
    Imath::C3f m_sky_clr;
    Imath::C3f m_sun_dir;
    float m_sun_angle;
    float m_sun_h;
    float m_sun_ratio;

    Imath::C3f m_sun_u;
    Imath::C3f m_sun_v;
};

class BRDF
{
public:
    BRDF(const nlohmann::json &parameters);

    void sample(Imath::C3f &clr, float &pdf, Imath::V3f &dir, const Imath::V3f &n, float sx, float sy) const;
    void evaluate(Imath::C3f &clr, float &pdf, const Imath::V3f &dir, const Imath::V3f &n) const;

    void mis_sample(const SUN_SKY_LIGHT &light,
                    Imath::C3f &b_clr, Imath::V3f &b_dir,
                    Imath::C3f &l_clr, Imath::V3f &l_dir,
                    const Imath::V3f &n,
                    float bsx, float bsy,
                    float lsx, float lsy) const;

private:
    Imath::C3f m_clr;
};

#endif // SHADING_H
