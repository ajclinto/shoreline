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

void TREE::build()
{
    printf("Constructing tree... levels %d\n", levels);

    // Determine the initial radius of the tree
    float radius = lengths[0]*ratio;

    construct(0, m_root, radius, lengths[0]);
}

void TREE::construct(int level, GROUP_NODE& local, 
        float radius, float length, float maxLength, 
        float parentLength, float offset)
{

    // If there is a need, these could also be made parameters for the
    // generation algorithm.
    
    // The extent to which a stem will taper to a point
    const float taper = 1.0;
    
    // Create the geometry for the stem.  Uses the curvature variation to 
    // introduce a curvature in the branch.  The points array stores the 
    // position of the segments in the tree.
    float rad = radius;
    float factor = (radius*taper/(float)curveResolution[level]);
    float segmentRotation = m_rand.nextf(-curveVar[level]/2.0, curveVar[level]/2.0)/
        (float)curveResolution[level];
    float segmentLen = length/(float)curveResolution[level];

    POLY_CURVE *segments = new POLY_CURVE;
    segments->m_pos_r.resize(curveResolution[level]+1);
    segments->m_pos_r[0] = std::make_pair(Imath::V3f(0, 0, 0), rad);
    for (int i = 0; i < curveResolution[level]; i++) {
        rad -= factor;
        Imath::M44f r;
        r.rotate(Imath::V3f(radians(i*segmentRotation), 0, 0));
        segments->m_pos_r[i+1] = std::make_pair(segments->m_pos_r[i].first+Imath::V3f(0, 0, segmentLen)*r, rad);
    }
    local.add_child(segments);

    // Branch if we are still under the recursion limit
    if (level < levels-1) {
    
        // Determine the number of sub-branches that should be created.  More
        // sub-branches are automatically created on branches that are nearer to
        // the base of the parent branch.
        int branches = 0;
        if (level == 0) {
            branches = branchingFactors[level+1];
        }
        else if (level == 1) {
            branches = (int)(branchingFactors[level+1]*(0.2+0.8*length/
                    (parentLength*maxLength)));
        }
        else {
            branches = (int)(branchingFactors[level+1]*(1.0-0.5*offset/parentLength));
        }
        
        // Construct the sub-branches at evenly spaced positions on the stem.
        
        float baseLength = level == 0 ? bare : 0.0;
        float offset = baseLength;
        float interBranchDist = (length-baseLength)/((float)branches + 1.0);
        float rotation = m_rand.nextf(0.0, 360.0);

        for (int i = 0; i < branches; i++) {
            // Randomly generate a down rotation vector
            float down = downAngles[level+1] + m_rand.nextf(
                    -downAnglesVar[level+1]/2.0, downAnglesVar[level+1]/2.0);
            // Randomly generate a around rotation vector
            rotation = rotation + rotations[level+1] + 
                m_rand.nextf(-rotationsVar[level+1]/2.0, rotationsVar[level+1]/2.0);
            // Generate the new offset for the branch
            offset += interBranchDist;

            // Generate the length of the child branch.  This is done in two 
            // different ways, depending on whether this branch is the trunk or 
            // another major branch.
            float childLength = 0.0;
            float maxChildLength = lengths[level+1] +
                m_rand.nextf(lengthsVar[level+1]/2.0, lengthsVar[level+1]/2.0);
            if (level == 0) {
                childLength = maxChildLength*length*evalShape((length-offset)/(length-baseLength));
            }
            else {
                childLength = maxChildLength*(length-0.6*offset);
            }

            // Determine the child radius
            float childRadius = std::min(radius*(length-offset)/length, pow(childLength/length, ratioPower)*radius);

            // Evaluate the position of the child branch on the parent using 
            // linear interpolation
            int indexLow = (int)(curveResolution[level]*offset/length);
            float interp = (curveResolution[level]*offset/length) -
                    (int)(curveResolution[level]*offset/length);
            Imath::V3f position = Imath::lerp(segments->m_pos_r[indexLow].first,
                                              segments->m_pos_r[indexLow+1].first, interp);

            // Create transformation nodes for the new subtree
            GROUP_NODE* child = new GROUP_NODE;

            Imath::M44f t;
            t.translate(position);
            t.rotate(Imath::V3f(radians(down), 0, radians(rotation)));
            child->set_transform(t);
            local.add_child(child);

            // Recursively construct the subtree or a leaf
            if (level == levels-2) {
                if (leaf) child->add_child(leaf);
            }
            else {
                construct(level+1, *child, childRadius, 
                        childLength, maxChildLength, length, offset);
            }
        }
    }
}

