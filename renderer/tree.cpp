/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#include <vector>
#include "tree.h"
#include "common.h"

// Maximum number of branches
static const int s_max_branch = 10;

static void set_shader_index(std::vector<int> &shader_index, unsigned int id, int shader_id)
{
    if (shader_index.size() <= id)
    {
        shader_index.resize(id+1, -1);
    }
    shader_index[id] = shader_id;
}

void POLY_CURVE::branch_geometry(const Imath::M44f &m, Imath::V4f *vertices, int &vidx, unsigned *indices, int &iidx) const
{
    for (int i = 0; i < m_pos_r.size(); i++)
    {
        // Assume m has no scaling transform
        auto pos = m_pos_r[i].first * m;
        float r = m_pos_r[i].second;
        if (i < m_pos_r.size()-1)
        {
            indices[iidx++] = vidx;
        }
        vertices[vidx++] = Imath::V4f(pos[0], pos[1], pos[2], r);
    }
}

void POLY_CURVE::leaf_geometry(const Imath::M44f &m, int &idx, Imath::V4f *vertices, Imath::V3f *normals) const
{
    auto pos = m_pos_r.back().first * m;
    auto ppos = m_pos_r[m_pos_r.size()-2].first * m;

    vertices[idx] = Imath::V4f(pos[0], pos[1], pos[2], m_leaf_radius);
    normals[idx] = pos - ppos;
    idx++;
}

void PLANE::embree_geometry(RTCDevice device, RTCScene scene,
                            std::vector<int> &shader_index,
                            std::vector<BRDF> &shaders) const
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

    vertices[0] = m_p;
    normals[0] = Imath::V3f(0, 0, 1);

    rtcCommitGeometry(geom);

    unsigned int id = rtcAttachGeometry(scene, geom);
    int shader_id = shaders.size();
    shaders.push_back(BRDF(m_parameters["diffuse_color"]));
    set_shader_index(shader_index, id, shader_id);

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
            {"name", "levels"},
            {"type", "float"},
            {"default", 5.5},
            {"min", 0.0},
            {"max", 7.0}
        },
        {
            {"name", "tree_height"},
            {"type", "float"},
            {"default", 2.5},
            {"min", 0.0},
            {"max", 10.0}
        },
        {
            {"name", "trunk_radius_ratio"},
            {"type", "float"},
            {"default", 0.02},
            {"min", 0.0},
            {"max", 0.1}
        },
        {
            {"name", "leaf_area_ratio"},
            {"type", "float"},
            {"default", 1.0},
            {"min", 0.0},
            {"max", 5.0}
        },
        {
            {"name", "branching_type"},
            {"type", "string"},
            {"default", "alternate"},
            {"values", {"alternate", "opposite", "whorls"}},
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
        },
        {
            {"name", "branch_color"},
            {"type", "color"},
            {"default", {0.25, 0.25, 0.25}}
        },
        {
            {"name", "leaf_color"},
            {"type", "color"},
            {"default", {0.6, 0.75, 0.54}}
        },
        {
            {"name", "enable_leaves"},
            {"type", "bool"},
            {"default", true}
        }
    };
    json_ui.insert(json_ui.end(), tree_ui.begin(), tree_ui.end());
}

void TREE::build()
{
    // Determine the initial trunk radius of the tree
    float height = m_parameters["tree_height"];
    float radius = m_parameters["trunk_radius_ratio"];
    radius *= height;
    float leaf_count = m_parameters["levels"];
    leaf_count = pow(10.0, leaf_count);

    m_leaf_radius = m_parameters["leaf_area_ratio"];
    m_leaf_radius = sqrt(m_leaf_radius / leaf_count);
    m_leaf_radius *= height;

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

    weight = 0;
    center_of_mass = 0;

    trunk.m_pos_r.push_back(std::make_pair(Imath::V3f(0, 0, 0), radius));
    if (branch_ratio * leaf_count <= 1.0F)
    {
        length *= leaf_count;
        trunk.m_pos_r.push_back(std::make_pair(Imath::V3f(0, 0, length), radius));
        trunk.m_leaf_radius = m_leaf_radius;
        trunk.m_leaf_radius *= lrand.nextf(0.5F, 1.25F);
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

void TREE::embree_geometry(RTCDevice device, RTCScene scene,
                           std::vector<int> &shader_index,
                           std::vector<BRDF> &shaders) const
{
    int curve_count = 0;
    int point_count = 0;
    m_root.geometry_size(curve_count, point_count);

    {
        int branch_shader = (int)shaders.size();
        shaders.push_back(BRDF(m_parameters["branch_color"]));

        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_ROUND_LINEAR_CURVE);
        Imath::V4f* vertices = (Imath::V4f*) rtcSetNewGeometryBuffer(geom,
                                                           RTC_BUFFER_TYPE_VERTEX,
                                                           0,
                                                           RTC_FORMAT_FLOAT4,
                                                           sizeof(Imath::V4f),
                                                           point_count);

        unsigned* indices = (unsigned*) rtcSetNewGeometryBuffer(geom,
                                                                RTC_BUFFER_TYPE_INDEX,
                                                                0,
                                                                RTC_FORMAT_UINT,
                                                                sizeof(unsigned),
                                                                point_count - curve_count);

        int vidx = 0;
        int iidx = 0;
        m_root.branch_geometry(Imath::M44f(), vertices, vidx, indices, iidx);
        assert(vidx == point_count);
        assert(iidx == point_count - curve_count);

        rtcCommitGeometry(geom);

        unsigned int branch_geometry_id = rtcAttachGeometry(scene, geom);
        set_shader_index(shader_index, branch_geometry_id, branch_shader);
        rtcReleaseGeometry(geom);
    }

    if (m_parameters["enable_leaves"])
    {
        int leaf_shader = (int)shaders.size();
        shaders.push_back(BRDF(m_parameters["leaf_color"]));

        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_ORIENTED_DISC_POINT);
        Imath::V4f* vertices = (Imath::V4f*) rtcSetNewGeometryBuffer(geom,
                                                           RTC_BUFFER_TYPE_VERTEX,
                                                           0,
                                                           RTC_FORMAT_FLOAT4,
                                                           sizeof(Imath::V4f),
                                                           curve_count);
        Imath::V3f* normals = (Imath::V3f*) rtcSetNewGeometryBuffer(geom,
                                                           RTC_BUFFER_TYPE_NORMAL,
                                                           0,
                                                           RTC_FORMAT_FLOAT3,
                                                           sizeof(Imath::V3f),
                                                           curve_count);

        int idx = 0;
        m_root.leaf_geometry(Imath::M44f(), idx, vertices, normals);
        assert(idx == curve_count);

        rtcCommitGeometry(geom);

        unsigned int leaf_geometry_id = rtcAttachGeometry(scene, geom);
        set_shader_index(shader_index, leaf_geometry_id, leaf_shader);
        rtcReleaseGeometry(geom);
    }
}

void FOREST::publish_ui(nlohmann::json &json_ui)
{
    nlohmann::json tree_ui = {
        {
            {"name", "tree_count"},
            {"type", "int"},
            {"default", 1},
            {"min", 1},
            {"max", 1000000}
        }
    };
    json_ui.insert(json_ui.end(), tree_ui.begin(), tree_ui.end());
}

void FOREST::embree_geometry(RTCDevice device, RTCScene scene,
                           std::vector<int> &shader_index,
                           std::vector<BRDF> &shaders) const
{
    RTCScene tree_scene = rtcNewScene(device);

    TREE tree(m_parameters);
    tree.build();
    tree.embree_geometry(device, tree_scene, shader_index, shaders);

    rtcCommitScene(tree_scene);

    Imath::Rand48 lrand(m_parameters["tree_seed"]);
    int count = m_parameters["tree_count"];
    for (int i = 0; i < count; i++)
    {
        float x = 0;
        float y = 0;
        float twist = 0;
        if (count > 1)
        {
            x = lrand.nextf(-1000.0F, 1000.0F);
            y = lrand.nextf(-1000.0F, 1000.0F);
            twist = lrand.nextf(0, 360.0F);
        }

        RTCGeometry geom = rtcNewGeometry(device, RTC_GEOMETRY_TYPE_INSTANCE);
        rtcSetGeometryInstancedScene(geom, tree_scene);

        Imath::M44f xform;
        xform.translate(Imath::V3f(x, y, 0));
        xform.rotate(Imath::V3f(0, 0, radians(twist)));

        //float xform[4][3] = {{1,0,0},{0,1,0},{0,0,1},{x,y,0}};
        rtcSetGeometryTransform(geom, 0, RTC_FORMAT_FLOAT4X4_COLUMN_MAJOR, &xform);
        rtcCommitGeometry(geom);

        rtcAttachGeometry(scene, geom);
        rtcReleaseGeometry(geom);
    }
}

