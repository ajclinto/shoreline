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

// A simple subset of the Weber-Penn tree model
class TREE {
public:
    TREE(int levels_in, int shape_in, float ratio_in, float ratioPower_in, 
         float bare_in,
         const std::vector<int> &branchingFactors_in, 
         const std::vector<float> &curveVar_in, const std::vector<int> &curveResolution_in,
         const std::vector<float> &lengths_in, const std::vector<float> &lengthsVar_in,
         const std::vector<float> &downAngles_in, const std::vector<float> &downAnglesVar_in,
         const std::vector<float> &rotations_in, const std::vector<float> &rotationsVar_in,
         NODE* leaf_in, unsigned long int seed = 0)
        : levels(levels_in)
        , shape(shape_in)
        , ratio(ratio_in)
        , ratioPower(ratioPower_in)
        , bare(bare_in)
        , branchingFactors(branchingFactors_in)
        , curveVar(curveVar_in)
        , curveResolution(curveResolution_in)
        , lengths(lengths_in)
        , lengthsVar(lengthsVar_in)
        , downAngles(downAngles_in)
        , downAnglesVar(downAnglesVar_in)
        , rotations(rotations_in)
        , rotationsVar(rotationsVar_in)
        , leaf(leaf_in)
        , m_rand(seed)
        {
        }

    // Performs procedural construction of the tree
    void build();

    // Generate geometry for rendering
    void embree_geometry(RTCDevice device, RTCScene scene)
    {
        m_root.embree_geometry(Imath::M44f(), device, scene);
    }

protected:
    // Evaluate the tree shape ratio
    float evalShape(float ratio) {
        switch (shape) {
            case 0: // Conical
                return 0.2+0.8*ratio;
            case 1: // Spherical
                return 0.2+0.8*sin(M_PI*ratio);
            case 2: // Hemispherical
                return 0.2+0.8*sin(0.5*M_PI*ratio);
            case 3: // Cylindrical 
                return 1.0;
            case 4: // Tapered Cylindrical
                return 0.5+0.5*ratio;
            default:
                printf("Invalid tree shape, aborting...\n");
                exit(1);
        }
    }

    // Build the hierarchical representation of the tree recursively
    void construct(int level, GROUP_NODE& local, 
                   float radius, float length, float maxLength = 0.0,
                   float parentLength = 0.0, float offset = 0.0);

private:
    int levels;
    int shape;
    float ratio;
    float ratioPower;
    float bare;

    std::vector<int> branchingFactors;
    std::vector<float> curveVar;
    std::vector<int> curveResolution;
    std::vector<float> lengths;
    std::vector<float> lengthsVar;
    std::vector<float> downAngles;
    std::vector<float> downAnglesVar;
    std::vector<float> rotations;
    std::vector<float> rotationsVar;

    NODE* leaf;

    GROUP_NODE m_root;

    Imath::Rand32 m_rand;
};

#endif // TREE_H
