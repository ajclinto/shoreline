/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include <vector>
#include "terrain.h"
#include "common.h"
#include "ImathRandom.h"

void TERRAIN::publish_ui(nlohmann::json &json_ui)
{
    nlohmann::json terrain_ui = {
        {
            {"name", "terrain_pos"},
            {"type", "float"},
            {"vector_size", 2},
            {"default", {0, -2.5}},
            {"min", -1000},
            {"max",  1000}
        },
        {
            {"name", "terrain_size"},
            {"type", "float"},
            {"default", 10000},
            {"min", 1.0},
            {"max", 100000.0}
        },
        {
            {"name", "terrain_near_clip"},
            {"type", "float"},
            {"default", 1.0},
            {"min", 0.001},
            {"max", 100.0}
        },
        {
            {"name", "terrain_field_of_view"},
            {"type", "float"},
            {"default", 60.0},
            {"min", 0.001},
            {"max", 90.0}
        },
        {
            {"name", "terrain_levels"},
            {"type", "float"},
            {"default", 1.0},
            {"min", 0.0},
            {"max", 7.0}
        },
        {
            {"name", "wave_filter_width"},
            {"type", "float"},
            {"default", 1.0},
            {"min", 0.0},
            {"max", 10.0}
        },
        {
            {"name", "wave_octaves"},
            {"type", "int"},
            {"default", 15},
            {"min", 0},
            {"max", 100}
        },
        {
            {"name", "wave_amplitude"},
            {"type", "float"},
            {"default", 0.01},
            {"min", 0.0},
            {"max", 1.0}
        },
        {
            {"name", "wave_frequency"},
            {"type", "float"},
            {"default", 1.0},
            {"min", 0.1},
            {"max", 10.0}
        },
        {
            {"name", "wave_roughness"},
            {"type", "float"},
            {"default", 1.0},
            {"min", 1.0},
            {"max", 2.0}
        },
        {
            {"name", "wave_frequency_scale"},
            {"type", "float"},
            {"default", 1.5},
            {"min", 1.0},
            {"max", 2.0}
        },
        {
            {"name", "enable_terrain"},
            {"type", "bool"},
            {"default", true}
        },
        {
            {"name", "enable_water"},
            {"type", "bool"},
            {"default", false}
        }
    };
    json_ui.insert(json_ui.end(), terrain_ui.begin(), terrain_ui.end());
}

RTCGeometry TERRAIN::create_terrain_grid(RTCDevice device,
                                         int &xres, int &yres,
                                         Imath::V3f *&vertices,
                                         Imath::V3f *&normals) const
{
    RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_QUAD);

    float terrain_size = m_parameters["terrain_size"];
    Imath::V4f terrain_pos(m_parameters["terrain_pos"][0], m_parameters["terrain_pos"][1], 0, terrain_size);

    float terrain_near_clip = m_parameters["terrain_near_clip"];
    float terrain_fov = m_parameters["terrain_field_of_view"];
    float xscale = tan(radians(terrain_fov)*0.5);

    float terrain_levels = m_parameters["terrain_levels"];
    int res = std::max((int)pow(10.0, 0.5*terrain_levels), 2);
    xres = res;
    yres = (int)(res/xscale);

    int point_count = xres*yres;
    vertices = (Imath::V3f*) rtcSetNewGeometryBuffer(geom,
                                                       RTC_BUFFER_TYPE_VERTEX,
                                                       0,
                                                       RTC_FORMAT_FLOAT3,
                                                       sizeof(Imath::V3f),
                                                       point_count);
    // Normal seems to be counted as a vertex attribute
    rtcSetGeometryVertexAttributeCount(geom, 1);
    normals = (Imath::V3f*) rtcSetNewGeometryBuffer(geom,
                                                       RTC_BUFFER_TYPE_NORMAL,
                                                       0,
                                                       RTC_FORMAT_FLOAT3,
                                                       sizeof(Imath::V3f),
                                                       point_count);
    unsigned* indices = (unsigned*) rtcSetNewGeometryBuffer(geom,
                                                            RTC_BUFFER_TYPE_INDEX,
                                                            0,
                                                            RTC_FORMAT_UINT4,
                                                            4*sizeof(unsigned),
                                                            (xres-1)*(yres-1));

    int voff = 0;
    int ioff = 0;
    for (int y = 0; y < yres; y++)
    {
        float ypos = exp(Imath::lerp(log(terrain_near_clip),
                                     log(terrain_size),
                                     y/(float)(yres-1)));
        for (int x = 0; x < xres; x++, voff++)
        {
            float xpos = ypos * (x/(float)(xres-1) - 0.5F) * xscale * 2;
            vertices[voff][0] = xpos + terrain_pos[0];
            vertices[voff][1] = ypos + terrain_pos[1];
            vertices[voff][2] = 0; // To be filled out by the caller
            if (y < yres-1 && x < xres-1)
            {
                indices[4*ioff+0] = y*xres+x;
                indices[4*ioff+1] = y*xres+x+1;
                indices[4*ioff+2] = (y+1)*xres+x+1;
                indices[4*ioff+3] = (y+1)*xres+x;
                ioff++;
            }
        }
    }

    return geom;
}

static void calculate_normals(Imath::V3f* normals, const Imath::V3f* vertices, int xres, int yres)
{
    // Calculate smooth normals
    memset(normals, 0, xres*yres*sizeof(Imath::V3f));
    for (int y = 0; y < yres-1; y++)
    {
        for (int x = 0; x < xres-1; x++)
        {
            int voff = y*xres + x;
            Imath::V3f u0 = vertices[voff+1]-vertices[voff];
            Imath::V3f v0 = vertices[voff+xres]-vertices[voff];
            Imath::V3f u1 = vertices[voff+xres+1]-vertices[voff+xres];
            Imath::V3f v1 = vertices[voff+xres+1]-vertices[voff+1];
            Imath::V3f n00 = u0.cross(v0);
            Imath::V3f n01 = u0.cross(v1);
            Imath::V3f n10 = u1.cross(v0);
            Imath::V3f n11 = u1.cross(v1);
            normals[y*xres+x] += n00*0.25;
            normals[y*xres+x+1] += n01*0.25;
            normals[(y+1)*xres+x] += n10*0.25;
            normals[(y+1)*xres+x+1] += n11*0.25;
        }
    }

    // Consistently scale all normals
    for (int y = 0; y < yres; y++)
    {
        normals[y*xres] *= 2;
        normals[y*xres+xres-1] *= 2;
    }
    for (int x = 0; x < xres; x++)
    {
        normals[x] *= 2;
        normals[(yres-1)*xres+x] *= 2;
    }
}

static inline float filtered_sin(float x, float fw)
{
    if (fw == 0) return sin(x);
    return (cos(x-0.5*fw)-cos(x+0.5*fw)) / fw;
}

void TERRAIN::embree_geometry(RTCDevice device, RTCScene scene,
                              std::vector<int> &shader_index,
                              const std::vector<std::string> &shader_names) const
{
    // Ground
    if (m_parameters["enable_terrain"])
    {
        int xres, yres;
        Imath::V3f *vertices;
        Imath::V3f *normals;
        RTCGeometry geom = create_terrain_grid(device, xres, yres, vertices, normals);

        int voff = 0;
        for (int y = 0; y < yres; y++)
        {
            for (int x = 0; x < xres; x++, voff++)
            {
                vertices[voff][2] = 0.5*(sin(vertices[voff][0]) + sin(vertices[voff][1]));
            }
        }

        calculate_normals(normals, vertices, xres, yres);

        rtcCommitGeometry(geom);

        unsigned int id = rtcAttachGeometry(scene, geom);
        int shader_id = BRDF::find_shader(shader_names, "default");
        BRDF::set_shader_index(shader_index, id, shader_id);

        rtcReleaseGeometry(geom);
    }

    // Water
    if (m_parameters["enable_water"])
    {
        int xres, yres;
        Imath::V3f *vertices;
        Imath::V3f *normals;
        RTCGeometry geom = create_terrain_grid(device, xres, yres, vertices, normals);

        struct WAVE {
            Imath::V2f dir;
            float freq;
            float amp;
        };

        std::vector<WAVE> spectrum;

        const float filter_width = m_parameters["wave_filter_width"];
        const int octaves = m_parameters["wave_octaves"];
        const float base_amp = m_parameters["wave_amplitude"];
        const float base_freq = m_parameters["wave_frequency"];
        const float freq_scale = m_parameters["wave_frequency_scale"];
        const float roughness = m_parameters["wave_roughness"];
        float amp = base_amp;
        float freq = base_freq;
        float amp_scale = roughness/freq_scale;
        Imath::Rand32 lrand(0);
        for (int i = 0; i < octaves; i++)
        {
            Imath::V2f dir;
            dir[0] = lrand.nextf(-1, 1);
            dir[1] = lrand.nextf(-1, 1);
            dir.normalize();
            spectrum.push_back(WAVE{dir,freq,amp});

            freq *= freq_scale;
            amp *= amp_scale;
        }

        // Calculate normals to find vertex filter area
        calculate_normals(normals, vertices, xres, yres);

        int voff = 0;
        for (int y = 0; y < yres; y++)
        {
            for (int x = 0; x < xres; x++, voff++)
            {
                float width = sqrt(normals[voff].length()) * filter_width;
                for (const WAVE &w : spectrum)
                {
                    float fwidth = width * w.freq;
                    if (fwidth > 2*M_PI) break;
                    vertices[voff][2] += w.amp*filtered_sin(w.freq*Imath::V2f(vertices[voff][0], vertices[voff][1]).dot(w.dir), fwidth);
                }
            }
        }

        calculate_normals(normals, vertices, xres, yres);

        rtcCommitGeometry(geom);

        unsigned int id = rtcAttachGeometry(scene, geom);
        int shader_id = BRDF::find_shader(shader_names, "water");
        BRDF::set_shader_index(shader_index, id, shader_id);

        rtcReleaseGeometry(geom);
    }
}

