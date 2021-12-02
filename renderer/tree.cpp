/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include <vector>
#include "tree.h"

static float radians(float degrees)
{
    return degrees * static_cast<float>(M_PI / 180.0);
}

void POLY_CURVE::embree_geometry(const Imath::M44f &m, RTCDevice device, RTCScene scene) const
{
    RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_ROUND_LINEAR_CURVE);
    Imath::V4f* vertices = (Imath::V4f*) rtcSetNewGeometryBuffer(geom,
                                                       RTC_BUFFER_TYPE_VERTEX,
                                                       0,
                                                       RTC_FORMAT_FLOAT4,
                                                       sizeof(Imath::V4f),
                                                       m_pos_r.size());

    unsigned* indices = (unsigned*) rtcSetNewGeometryBuffer(geom,
                                                            RTC_BUFFER_TYPE_INDEX,
                                                            0,
                                                            RTC_FORMAT_UINT,
                                                            sizeof(unsigned),
                                                            m_pos_r.size());

    for (int i = 0; i < m_pos_r.size(); i++)
    {
        // Assume m has no scaling transform
        auto pos = m_pos_r[i].first * m;
        float r = m_pos_r[i].second;
        vertices[i] = Imath::V4f(pos[0], pos[1], pos[2], r);
        indices[i] = i;
    }

    rtcCommitGeometry(geom);

    rtcAttachGeometry(scene, geom);
    rtcReleaseGeometry(geom);
}

void PLANE::embree_geometry(RTCDevice device, RTCScene scene) const
{
    RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_GRID);

    RTCGrid* grid = (RTCGrid*) rtcSetNewGeometryBuffer(geom,
                                                       RTC_BUFFER_TYPE_GRID,
                                                       0,
                                                       RTC_FORMAT_GRID,
                                                       sizeof(RTCGrid),
                                                       1);
    grid->startVertexID = 0;
    grid->width = 2;
    grid->height = 2;
    grid->stride = 2;

    Imath::V3f* vertices = (Imath::V3f*) rtcSetNewGeometryBuffer(geom,
                                                       RTC_BUFFER_TYPE_VERTEX,
                                                       0,
                                                       RTC_FORMAT_FLOAT3,
                                                       sizeof(Imath::V3f),
                                                       4);

    vertices[0] = m_p - m_u - m_v;
    vertices[1] = m_p - m_u + m_v;
    vertices[2] = m_p + m_u - m_v;
    vertices[3] = m_p + m_u + m_v;

    rtcCommitGeometry(geom);

    rtcAttachGeometry(scene, geom);
    rtcReleaseGeometry(geom);
}

void TREE::publish_ui(nlohmann::json &json_ui)
{
    nlohmann::json tree_ui = {
        {
            {"name", "tree_seed"},
            {"type", "int"},
            {"default", 0},
            {"min", 0},
            {"max", 10}
        },
        {
            {"name", "trunk_radius"},
            {"type", "float"},
            {"default", 0.04},
            {"min", 0.0},
            {"max", 1.0}
        },
        {
            {"name", "levels"},
            {"type", "float"},
            {"default", 5.0},
            {"min", 0.0},
            {"max", 6.0}
        },
        {
            {"name", "tree_height"},
            {"type", "float"},
            {"default", 2.5},
            {"min", 0.0},
            {"max", 10.0}
        },
        {
            {"name", "branch_length_exponent"},
            {"type", "float"},
            {"default", 0.7},
            {"min", 0.0},
            {"max", 1.0}
        },
        {
            {"name", "da_vinci_exponent"},
            {"type", "float"},
            {"default", 2.0},
            {"min", 1.8},
            {"max", 2.3}
        },
        {
            {"name", "branch_ratio"},
            {"type", "float"},
            {"default", 0.4},
            {"min", 0.001},
            {"max", 0.5}
        },
        {
            {"name", "branch_ratio_variance"},
            {"type", "float"},
            {"default", 0.75},
            {"min", 0.0},
            {"max", 1.0}
        },
        {
            {"name", "branch_spread_angle"},
            {"type", "float"},
            {"default", 40.0},
            {"min", 0.0},
            {"max", 90.0}
        },
        {
            {"name", "branch_twist_angle"},
            {"type", "float"},
            {"default", 130.0},
            {"min", 0.0},
            {"max", 180.0}
        },
        {
            {"name", "branch_angle_variance"},
            {"type", "float"},
            {"default", 10.0},
            {"min", 0.0},
            {"max", 90.0}
        }
    };
    json_ui.insert(json_ui.end(), tree_ui.begin(), tree_ui.end());
}

void TREE::build()
{
    // Determine the initial trunk radius of the tree
    float radius = m_parameters["trunk_radius"];
    float height = m_parameters["tree_height"];
    float leaf_count = m_parameters["levels"];
    leaf_count = pow(10.0, leaf_count);

    POLY_CURVE *trunk = new POLY_CURVE;
    float weight;
    float center_of_mass;
    construct(m_root, *trunk, weight, center_of_mass, m_root_seed, radius, leaf_count);
    m_root.add_child(trunk);

    // Scale the whole tree to the desired size
    float trunk_len = 0.0;
    for (int i = 0; i < trunk->m_pos_r.size()-1; i++)
    {
        trunk_len += (trunk->m_pos_r[i+1].first - trunk->m_pos_r[i].first).length();
    }

    m_root.m_xform.setScale(height / trunk_len);
}

void TREE::construct(GROUP_NODE& local, POLY_CURVE &trunk,
                     float &weight, float &center_of_mass,
                     uint32_t seed, float radius, float leaf_count)
{
    Imath::Rand32 lrand(seed);
    float length = 1.0;
    float branch_ratio = m_parameters["branch_ratio"];
    float branch_ratio_variance = m_parameters["branch_ratio_variance"];
    float branch_length_exponent = m_parameters["branch_length_exponent"];

    branch_ratio *= lrand.nextf(1.0F-branch_ratio_variance, 1.0F);

    length *= pow(radius, branch_length_exponent);
    length *= branch_ratio;

    trunk.m_pos_r.push_back(std::make_pair(Imath::V3f(0, 0, 0), radius));
    if (leaf_count <= 1.0F)
    {
        length *= leaf_count;
        trunk.m_pos_r.push_back(std::make_pair(Imath::V3f(0, 0, length), 0.0F));
        weight = 0;
        center_of_mass = 0;
    }
    else
    {
        float da_vinci_exponent = m_parameters["da_vinci_exponent"];
        float angle_var = m_parameters["branch_angle_variance"];
        float area = pow(radius, da_vinci_exponent);

        float spread = m_parameters["branch_spread_angle"];
        float twist = m_parameters["branch_twist_angle"];

        spread += lrand.nextf(-angle_var, angle_var);
        twist += lrand.nextf(-angle_var, angle_var);

        float r[2] = {
            pow(area*branch_ratio, 1.0F/da_vinci_exponent),
            pow(area*(1.0F-branch_ratio), 1.0F/da_vinci_exponent)};
        float l[2] = {leaf_count*branch_ratio, leaf_count*(1.0F-branch_ratio)};
        float w[2];
        float c[2];

        // Treat the larger branch as a continuation of the trunk
        int larger_idx = 1;
        int trunk_idx = trunk.m_pos_r.size();

        GROUP_NODE *children[2];

        // Build the child branches then set angles in a second pass given the
        // known downstream weight and height of center of mass
        for (int i = 0; i < 2; i++)
        {
            // Create transformation nodes for the new subtree
            children[i] = new GROUP_NODE;

            if (i == larger_idx)
            {
                construct(*children[i], trunk, w[i], c[i], lrand.nexti(), r[i], l[i]);
            }
            else
            {
                POLY_CURVE *branch = new POLY_CURVE;
                construct(*children[i], *branch, w[i], c[i], lrand.nexti(), r[i], l[i]);
                children[i]->add_child(branch);
            }
        }

        weight = w[0] + w[1];
        center_of_mass = (w[0]*c[0] + w[1]*c[1]) / weight;

        for (int i = 0; i < 2; i++)
        {
            float angle = spread * w[1-i]*c[1-i] / (w[0]*c[0] + w[1]*c[1]);
            Imath::V3f position(0, 0, length);

            Imath::M44f t;
            t.translate(position);
            t.rotate(Imath::V3f(radians(angle), 0, radians(twist)));
            children[i]->set_transform(t);
            local.add_child(children[i]);

            if (i == larger_idx)
            {
                for (int j = trunk_idx; j < trunk.m_pos_r.size(); j++)
                {
                    trunk.m_pos_r[j].first *= t;
                }
            }

            twist += 180.0F;
        }
    }

    float stem_weight = radius * radius * length;
    center_of_mass = (center_of_mass + length) * weight + length * 0.5F * stem_weight;
    weight += stem_weight;
    center_of_mass /= weight;
}

