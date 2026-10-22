// D3D11Client: the interior API's data (LightsApi.cpp), read by Lights.cpp
#pragma once
#include "D3D11Lights.h"
#include <map>
#include <string>
#include <vector>

unsigned LightsApiData(std::map<OBJHANDLE, std::vector<D3D11Lights::Volume>>* vol, std::map<OBJHANDLE, std::vector<D3D11Lights::AreaLight>>* area,
                       std::map<OBJHANDLE, std::map<int, bool>>* circuits, std::map<OBJHANDLE, std::vector<D3D11Lights::Opening>>* openings = nullptr);
void LightsApiForget(OBJHANDLE h);     // the vessel is gone
void LightsApiGI(std::map<OBJHANDLE, std::string>* files, std::map<OBJHANDLE, std::vector<float>>* portals);   // the radiosity's files and portals
