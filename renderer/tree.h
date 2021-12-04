/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef TREE_H
#define TREE_H

#include <stdio.h>
#include <vector>
#include <embree3/rtcore.h>
#include "assert.h"
#include "ImathRandom.h"
#include "ImathVec.h"
#include "ImathMatrix.h"
#include <nlohmann/json.hpp>
#include "shading.h"


// Base class for scene nodes
class NODE
{
public:
    // c == curves, p == points
    virtual void geometry_size(int &c, int &p) const = 0;
    virtual void branch_geometry(const Imath::M44f &m, Imath::V4f *vertices, int &vidx, unsigned *indices, int &iidx) const = 0;
    virtual void leaf_geometry(const Imath::M44f &m, int &idx, Imath::V4f *vertices, Imath::V3f *normals) const = 0;
};

// A transform and a list of nodes
class GROUP_NODE : public NODE
{
public:
    void add_child(NODE *n) { m_nodes.push_back(std::unique_ptr<NODE>(n)); }
    void set_transform(const Imath::M44f &x) { m_xform = x; }

    virtual void geometry_size(int &c, int &p) const override
    {
        for (const auto &n : m_nodes)
        {
            n->geometry_size(c, p);
        }
    }

    virtual void branch_geometry(const Imath::M44f &m, Imath::V4f *vertices, int &vidx, unsigned *indices, int &iidx) const override
    {
        auto xform = m_xform * m;
        for (const auto &n : m_nodes)
        {
            n->branch_geometry(xform, vertices, vidx, indices, iidx);
        }
    }

    virtual void leaf_geometry(const Imath::M44f &m, int &idx, Imath::V4f *vertices, Imath::V3f *normals) const override
    {
        auto xform = m_xform * m;
        for (const auto &n : m_nodes)
        {
            n->leaf_geometry(xform, idx, vertices, normals);
        }
    }

    Imath::M44f m_xform;
    std::vector<std::unique_ptr<NODE>> m_nodes;
};

// Polygon curves
class POLY_CURVE : public NODE
{
public:
    virtual void geometry_size(int &c, int &p) const
    {
        c++;
        p += m_pos_r.size();
    }

    virtual void branch_geometry(const Imath::M44f &m, Imath::V4f *vertices, int &vidx, unsigned *indices, int &iidx) const override;
    virtual void leaf_geometry(const Imath::M44f &m, int &idx, Imath::V4f *vertices, Imath::V3f *normals) const;

    std::vector<std::pair<Imath::V3f, float>> m_pos_r;
    float m_leaf_radius = 0.01F;
};

class PLANE
{
public:
    PLANE(nlohmann::json parameters, const Imath::V3f &p, const Imath::V3f &u, const Imath::V3f &v)
        : m_parameters(parameters), m_p(p), m_u(u), m_v(v)
    {}

    void embree_geometry(RTCDevice device, RTCScene scene,
                         std::vector<int> &shader_index,
                         std::vector<BRDF> &shaders) const;

private:
    nlohmann::json m_parameters;
    Imath::V3f m_p;
    Imath::V3f m_u;
    Imath::V3f m_v;
};

class TREE {
public:
    TREE(const nlohmann::json &parameters)
        : m_parameters(parameters)
        , m_root_seed(parameters["tree_seed"])
    {
    }

    static void publish_ui(nlohmann::json &json_ui);

    // Performs procedural construction of the tree
    void build();

    // Generate geometry for rendering
    void embree_geometry(RTCDevice device, RTCScene scene,
                         std::vector<int> &shader_index,
                         std::vector<BRDF> &shaders) const;

private:
    // Build the hierarchical representation of the tree recursively
    void construct(GROUP_NODE& local, POLY_CURVE &trunk,
                   float &weight, float &center_of_mass,
                   uint32_t seed, float radius, float leaf_count);

private:
    nlohmann::json m_parameters;

    GROUP_NODE m_root;

    uint32_t m_root_seed = 0;
    float m_leaf_radius = 1.0;
};

#endif // TREE_H
