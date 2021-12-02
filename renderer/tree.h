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


// Base class for scene nodes
class NODE
{
public:
    virtual void embree_geometry(const Imath::M44f &m, RTCDevice device, RTCScene scene) const = 0;
};

// A transform and a list of nodes
class GROUP_NODE : public NODE
{
public:
    void add_child(NODE *n) { m_nodes.push_back(std::unique_ptr<NODE>(n)); }
    void set_transform(const Imath::M44f &x) { m_xform = x; }

    virtual void embree_geometry(const Imath::M44f &m, RTCDevice device, RTCScene scene) const override
    {
        auto xform = m_xform * m;
        for (const auto &n : m_nodes)
        {
            n->embree_geometry(xform, device, scene);
        }
    }

    Imath::M44f m_xform;
    std::vector<std::unique_ptr<NODE>> m_nodes;
};

// Polygon curves
class POLY_CURVE : public NODE
{
public:
    virtual void embree_geometry(const Imath::M44f &m, RTCDevice device, RTCScene scene) const override;

    std::vector<std::pair<Imath::V3f, float>> m_pos_r;
};

class PLANE
{
public:
    PLANE(const Imath::V3f &p, const Imath::V3f &u, const Imath::V3f &v) : m_p(p), m_u(u), m_v(v) {}

    virtual void embree_geometry(RTCDevice device, RTCScene scene) const;

private:
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
    void embree_geometry(RTCDevice device, RTCScene scene)
    {
        m_root.embree_geometry(Imath::M44f(), device, scene);
    }

private:
    // Build the hierarchical representation of the tree recursively
    void construct(GROUP_NODE& local, POLY_CURVE &trunk,
                   float &weight, float &center_of_mass,
                   uint32_t seed, float radius, float leaf_count);

private:
    nlohmann::json m_parameters;

    GROUP_NODE m_root;

    uint32_t m_root_seed;
};

#endif // TREE_H
