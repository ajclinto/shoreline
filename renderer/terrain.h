/*
   Shoreline Renderer

   Copyright (C) 2021 Andrew Clinton
*/

#ifndef TERRAIN_H
#define TERRAIN_H

#include <embree3/rtcore.h>
#include <nlohmann/json.hpp>
#include "shading.h"

class TERRAIN
{
public:
    TERRAIN(const nlohmann::json &parameters)
        : m_parameters(parameters)
    {}

    static void publish_ui(nlohmann::json &json_ui);

    void embree_geometry(RTCDevice device, RTCScene scene,
                         std::vector<int> &shader_index,
                         const std::vector<std::string> &shader_names) const;

private:
    nlohmann::json m_parameters;
};

#endif // TERRAIN_H
