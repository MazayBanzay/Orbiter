// placeholder until the FX atmosphere ships its own (FxAtmo.cpp)
bool FxActive() { return false; }
float3 FxSunLight(float3 wp) { return float3(1, 1, 1); }
float3 FxSkyLight(float3 n, float3 wp) { return float3(0, 0, 0); }
