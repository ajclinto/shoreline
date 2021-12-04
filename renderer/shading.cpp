/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include "shading.h"

static float radians(float degrees)
{
    return degrees * static_cast<float>(M_PI / 180.0);
}

static void get_basis(const Imath::V3f &n, Imath::V3f &u, Imath::V3f &v)
{
    if (n[1] || n[2])
    {
        u = Imath::V3f(0, n[2], -n[1]);
        u.normalize();
        v = u.cross(n);
    }
    else
    {
        u = Imath::V3f(0, 1, 0);
        v = Imath::V3f(0, 0, 1);
    }
}

static Imath::C3f json_to_color(const nlohmann::json &clr)
{
    return Imath::C3f(pow(clr[0], 2.2), pow(clr[1], 2.2), pow(clr[2], 2.2));
}

void SUN_SKY_LIGHT::publish_ui(nlohmann::json &json_ui)
{
    nlohmann::json sun_sky_ui = {
        {
            {"name", "sun_elevation"},
            {"type", "float"},
            {"default", 50.0},
            {"min", -90.0},
            {"max", 90.0}
        },
        {
            {"name", "sun_azimuth"},
            {"type", "float"},
            {"default", 340.0},
            {"min", 0.0},
            {"max", 360.0}
        },
        {
            {"name", "sun_intensity"},
            {"type", "float"},
            {"default", 2.0},
            {"min", 0.0},
            {"max", 4.0}
        },
        {
            {"name", "sun_color"},
            {"type", "color"},
            {"default", {1.0, 0.83, 0.78}}
        },
        {
            {"name", "sky_1_color"},
            {"type", "color"},
            {"default", {0.25, 0.56, 1.0}}
        },
        {
            {"name", "sky_2_color"},
            {"type", "color"},
            {"default", {0.47, 0.77, 1.0}}
        },
        {
            {"name", "sky_3_color"},
            {"type", "color"},
            {"default", {0.7, 0.9, 1.0}}
        }
    };
    json_ui.insert(json_ui.end(), sun_sky_ui.begin(), sun_sky_ui.end());
}

SUN_SKY_LIGHT::SUN_SKY_LIGHT(const nlohmann::json &parameters)
{
    m_sun_clr = json_to_color(parameters["sun_color"]);
    m_sky_1_clr = json_to_color(parameters["sky_1_color"]);
    m_sky_2_clr = json_to_color(parameters["sky_2_color"]);
    m_sky_3_clr = json_to_color(parameters["sky_3_color"]);
    m_sun_dir = Imath::V3f(1.0, 0.0, 0.0);
    Imath::M44f r;
    r.rotate(Imath::V3f(0.0F, -radians(parameters["sun_elevation"]), -radians(parameters["sun_azimuth"])));
    m_sun_dir *= r;
    m_sun_dir.normalize();
    m_sun_angle = radians(32.0F / 60.0F);
    m_sun_h = 1.0-cos(m_sun_angle);
    m_sun_intensity = parameters["sun_intensity"];

    // Precompute a basis to sample the sun
    get_basis(m_sun_dir, m_sun_u, m_sun_v);
}

void SUN_SKY_LIGHT::sample(Imath::C3f &clr, float &pdf, Imath::V3f &dir, float sx, float sy) const
{
    // Only sample the sun - the constant sky is sampled more effectively with
    // the BRDF

    pdf = 1.0F / m_sun_h;
    clr = m_sun_intensity * m_sun_clr * pdf;

    float h = sy * m_sun_h;
    float a = sx * 2.0F * static_cast<float>(M_PI);
    float rz = 1.0F-h;
    dir = (m_sun_u*cos(a) + m_sun_v*sin(a))*sqrtf(1-rz*rz) + m_sun_dir*rz;
}

void SUN_SKY_LIGHT::evaluate(Imath::C3f &clr, float &pdf, const Imath::V3f &dir) const
{
    if (dir.dot(m_sun_dir) > 1.0F-m_sun_h)
    {
        pdf = 1.0F / m_sun_h;
        clr = m_sun_intensity * m_sun_clr * pdf;
    }
    else
    {
        pdf = 0.0F; // sample() doesn't sample the sky
        float ratio1 = (1.0F - dir[2]) * (1.0F - dir[2]);
        float ratio2 = ratio1 * ratio1 * ratio1;
        clr = (1.0F - ratio2) * m_sky_2_clr + ratio2 * m_sky_3_clr;
        clr = (1.0F - ratio1) * m_sky_1_clr + ratio1 * clr;
    }
}

BRDF::BRDF(const nlohmann::json &color)
{
    m_clr = json_to_color(color);
}

void BRDF::sample(Imath::C3f &clr, float &pdf, Imath::V3f &dir, const Imath::V3f &n, float sx, float sy) const
{
    Imath::V3f u, v;
    get_basis(n, u, v);

    float a = sx * 2 * static_cast<float>(M_PI);
    float r = sqrtf(sy);
    u *= cos(a) * r;
    v *= sin(a) * r;
    float rz = sqrtf(1-sy);
    dir = u + v + n*rz;

    pdf = 2 * rz;

    clr = m_clr * pdf;
}

void BRDF::evaluate(Imath::C3f &clr, float &pdf, const Imath::V3f &dir, const Imath::V3f &n) const
{
    float rz = n.dot(dir);
    if (rz > 0)
    {
        pdf = 2 * rz;
        clr = m_clr * pdf;
    }
    else
    {
        pdf = 0;
        clr = Imath::C3f(0);
    }
}

void BRDF::mis_sample(const SUN_SKY_LIGHT &light,
                      Imath::C3f &b_clr, Imath::V3f &b_dir,
                      Imath::C3f &l_clr, Imath::V3f &l_dir,
                      const Imath::V3f &n,
                      float bsx, float bsy,
                      float lsx, float lsy) const
{
    float b_pdf;
    float l_pdf;

    sample(b_clr, b_pdf, b_dir, n, bsx, bsy);
    light.sample(l_clr, l_pdf, l_dir, lsx, lsy);

    Imath::C3f bl_clr;
    Imath::C3f lb_clr;
    float bl_pdf;
    float lb_pdf;

    evaluate(bl_clr, bl_pdf, l_dir, n);
    light.evaluate(lb_clr, lb_pdf, b_dir);

    // Power heuristic - note I'm leaving out one factor of
    // x_pdf since it would cancel below
    float wb = b_pdf / (b_pdf * b_pdf + lb_pdf * lb_pdf);
    float wl = l_pdf / (l_pdf * l_pdf + bl_pdf * bl_pdf);

    b_clr *= lb_clr * wb;
    l_clr *= bl_clr * wl;
}
