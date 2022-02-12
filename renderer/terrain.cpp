/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include <vector>
#include "terrain.h"
#include "common.h"

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
            {"max", 6.0}
        },
        {
            {"name", "enable_water"},
            {"type", "bool"},
            {"default", false}
        }
    };
    json_ui.insert(json_ui.end(), terrain_ui.begin(), terrain_ui.end());
}

void TERRAIN::embree_geometry(RTCDevice device, RTCScene scene,
                              std::vector<int> &shader_index,
                              const std::vector<std::string> &shader_names) const
{
    float terrain_size = m_parameters["terrain_size"];
    Imath::V4f terrain_pos(m_parameters["terrain_pos"][0], m_parameters["terrain_pos"][1], 0, terrain_size);

    // Ground
    {
        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_QUAD);

        float terrain_near_clip = m_parameters["terrain_near_clip"];
        float terrain_fov = m_parameters["terrain_field_of_view"];
        float xscale = tan(radians(terrain_fov)*0.5);

        float terrain_levels = m_parameters["terrain_levels"];
        int res = std::max((int)pow(10.0, 0.5*terrain_levels), 2);
        int xres = res;
        int yres = (int)(res/xscale);

        int point_count = xres*yres;
        Imath::V3f* vertices = (Imath::V3f*) rtcSetNewGeometryBuffer(geom,
                                                           RTC_BUFFER_TYPE_VERTEX,
                                                           0,
                                                           RTC_FORMAT_FLOAT3,
                                                           sizeof(Imath::V3f),
                                                           point_count);
        // Normal seems to be counted as a vertex attribute
        rtcSetGeometryVertexAttributeCount(geom, 1);
        Imath::V3f* normals = (Imath::V3f*) rtcSetNewGeometryBuffer(geom,
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
                // TODO
                vertices[voff][2] = 0.5*(sin(xpos) + sin(ypos));
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

        // Calculate smooth normals
        memset(normals, 0, point_count*sizeof(Imath::V3f));
        for (int y = 0; y < yres-1; y++)
        {
            for (int x = 0; x < xres-1; x++)
            {
                voff = y*xres + x;
                Imath::V3f nml =
                    (vertices[voff+1]-vertices[voff]).cross(vertices[voff+xres]-vertices[voff]) +
                    (vertices[voff+xres+1]-vertices[voff+xres]).cross(vertices[voff+xres+1]-vertices[voff+1]);
                normals[y*xres+x] += nml;
                normals[y*xres+x+1] += nml;
                normals[(y+1)*xres+x] += nml;
                normals[(y+1)*xres+x+1] += nml;
            }
        }

        rtcCommitGeometry(geom);

        unsigned int id = rtcAttachGeometry(scene, geom);
        int shader_id = BRDF::find_shader(shader_names, "default");
        BRDF::set_shader_index(shader_index, id, shader_id);

        rtcReleaseGeometry(geom);
    }

    // Water
    if (m_parameters["enable_water"])
    {
        // A single large disc with +z normal. Renders with fewer artifacts than a
        // large grid
        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_ORIENTED_DISC_POINT);
        Imath::V4f* vertices = (Imath::V4f*) rtcSetNewGeometryBuffer(geom,
                                                           RTC_BUFFER_TYPE_VERTEX,
                                                           0,
                                                           RTC_FORMAT_FLOAT4,
                                                           sizeof(Imath::V4f),
                                                           1);
        Imath::V3f* normals = (Imath::V3f*) rtcSetNewGeometryBuffer(geom,
                                                           RTC_BUFFER_TYPE_NORMAL,
                                                           0,
                                                           RTC_FORMAT_FLOAT3,
                                                           sizeof(Imath::V3f),
                                                           1);

        vertices[0] = terrain_pos;
        normals[0] = Imath::V3f(0, 0, 1);

        rtcCommitGeometry(geom);

        unsigned int id = rtcAttachGeometry(scene, geom);
        int shader_id = BRDF::find_shader(shader_names, "water");
        BRDF::set_shader_index(shader_index, id, shader_id);

        rtcReleaseGeometry(geom);
    }
}

