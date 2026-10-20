// Earth's clouds (not Titan's: those are Clouds.cpp): the far 2D layer (CloudLayer.cpp), the moving cyclone's weather map
// (Cyclone.cpp), the near volumetric clouds (EarthClouds.cpp)
#pragma once
class D3D11Client;

// CloudLayer.cpp
void CloudLayerSetCyclone(ID3D11ShaderResourceView* srv);
ID3D11ShaderResourceView* CloudLayerCoverage(OBJHANDLE body, double* alt);
double CloudLayerFade(double camAlt);       // 1: the 2D layer alone (afar), 0: the volumetric clouds alone (near)

// Cyclone.cpp
bool CycloneUpdate(D3D11Client* c, ID3D11Device* dev, ID3D11DeviceContext* ctx, OBJHANDLE body);
void CycloneLayerConstants(OBJHANDLE body, float* cy4);
ID3D11ShaderResourceView* CycloneMap();
