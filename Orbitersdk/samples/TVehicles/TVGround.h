// TVGround - the ground-type raster of a site (the user, 2026-10-18, Titan): one byte a cell, shared with OrbiterCrew so the
// suit and the vehicles stand on the same ground. Written by the Titan surface session:
//   Config\OrbiterCrew\Ground\<Body>.cfg  - the header: Body, CenterLat, CenterLon (deg, east-positive), StepM (the cell, m),
//                                           W, H (cells), each as "Key = value" (or "Key value"); ';' starts a comment;
//                                           optional File (the data's name, default <Body>.bin beside the .cfg)
//   Config\OrbiterCrew\Ground\<Body>.bin  - W x H bytes, rows from the north, columns from the west
// Titan's kinds: 0 sand (damp organic), 1 pebbles (rounded ice), 2 bedrock ice, 3 the wet margin, 4 liquid. Outside the
// raster (or with no raster): -1, the caller's default.
#pragma once
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace tvground {

enum Kind { SAND = 0, PEBBLES = 1, ICE = 2, WET = 3, LIQUID = 4 };

class Raster {
    double cLat = 0, cLon = 0, step = 1; int w = 0, h = 0; std::vector<unsigned char> d;
    double level = 0; bool liquid = false;                  // LiquidLevel: the seas' surface, m from the mean radius
public:
    bool Load(const char* cfgPath, const char* body) {
        d.clear(); w = h = 0;
        FILE* f = std::fopen(cfgPath, "r");
        if (!f) return false;
        std::string file = std::string(body) + ".bin";
        char line[256];
        while (std::fgets(line, sizeof line, f)) {
            if (char* c = std::strchr(line, ';')) *c = 0;
            char key[64] = "", val[160] = "";
            for (char* p = line; *p; ++p) if (*p == '=') *p = ' ';
            if (std::sscanf(line, "%63s %159s", key, val) != 2) continue;
            if (!_stricmp(key, "CenterLat")) cLat = std::atof(val);
            else if (!_stricmp(key, "CenterLon") || !_stricmp(key, "CenterLng")) cLon = std::atof(val);
            else if (!_stricmp(key, "StepM")) step = std::atof(val);
            else if (!_stricmp(key, "W")) w = std::atoi(val);
            else if (!_stricmp(key, "H")) h = std::atoi(val);
            else if (!_stricmp(key, "File")) file = val;
            else if (!_stricmp(key, "LiquidLevel")) { level = std::atof(val); liquid = true; }
        }
        std::fclose(f);
        if (w <= 0 || h <= 0 || step <= 0) return false;
        std::string dir(cfgPath); const size_t sl = dir.find_last_of("\\/");
        dir = sl == std::string::npos ? std::string() : dir.substr(0, sl + 1);
        FILE* b = std::fopen((dir + file).c_str(), "rb");
        if (!b) return false;
        d.resize((size_t)w * h);
        const bool ok = std::fread(d.data(), 1, d.size(), b) == d.size();
        std::fclose(b);
        if (!ok) d.clear();
        return ok;
    }
    bool Loaded() const { return !d.empty(); }
    bool HasLiquid() const { return liquid; }
    double LiquidLevel() const { return level; }
    // lat, lon in radians (Orbiter's); R the body's radius, m -> the kind, or -1 off the raster
    int At(double lat, double lon, double R) const {
        if (d.empty()) return -1;
        const double D = 3.14159265358979 / 180.0;
        double dl = lon - cLon * D; while (dl > 3.14159265358979) dl -= 2 * 3.14159265358979; while (dl < -3.14159265358979) dl += 2 * 3.14159265358979;
        const double x = dl * R * std::cos(cLat * D), y = (lat - cLat * D) * R;   // m east, m north of the centre (the writer's: cos of the centre's latitude)
        const int c = (int)std::floor(x / step + 0.5 * w), r = (int)std::floor(-y / step + 0.5 * h);
        if (c < 0 || r < 0 || c >= w || r >= h) return -1;
        return d[(size_t)r * w + c];
    }
};

}  // namespace tvground
#ifdef _MSC_VER
#pragma warning(pop)
#endif
