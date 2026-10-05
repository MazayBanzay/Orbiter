// Stand-in for Orbiter's SDK header, for the hands trace only (tests\hands_trace.cpp): the figure's own code - Skin,
// Motion, SeatArms - built and run without Orbiter. Only the names those files use; the mesh functions are the test's
// (it reads the .msh itself and keeps what the skin writes into the "device mesh").
#pragma once
#include <cmath>
#include <cstdarg>
#include <cstdio>

typedef unsigned long DWORD;
typedef unsigned int UINT;
typedef unsigned short WORD;
typedef void* OBJHANDLE;
typedef void* VISHANDLE;
typedef void* MESHHANDLE;
typedef void* DEVMESHHANDLE;

const double PI = 3.14159265358979323846, PI05 = PI / 2, PI2 = 2 * PI, RAD = PI / 180, DEG = 180 / PI;

typedef union { double data[3]; struct { double x, y, z; }; } VECTOR3;
inline VECTOR3 _V(double x, double y, double z) { VECTOR3 v; v.x = x; v.y = y; v.z = z; return v; }
inline VECTOR3 operator+(const VECTOR3& a, const VECTOR3& b) { return _V(a.x + b.x, a.y + b.y, a.z + b.z); }
inline VECTOR3 operator-(const VECTOR3& a, const VECTOR3& b) { return _V(a.x - b.x, a.y - b.y, a.z - b.z); }
inline VECTOR3 operator-(const VECTOR3& a) { return _V(-a.x, -a.y, -a.z); }
inline VECTOR3 operator*(const VECTOR3& a, double f) { return _V(a.x * f, a.y * f, a.z * f); }
inline VECTOR3 operator/(const VECTOR3& a, double f) { return _V(a.x / f, a.y / f, a.z / f); }
inline VECTOR3& operator+=(VECTOR3& a, const VECTOR3& b) { a = a + b; return a; }
inline VECTOR3& operator-=(VECTOR3& a, const VECTOR3& b) { a = a - b; return a; }
inline double dotp(const VECTOR3& a, const VECTOR3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline VECTOR3 crossp(const VECTOR3& a, const VECTOR3& b) { return _V(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline double length(const VECTOR3& a) { return std::sqrt(dotp(a, a)); }
inline VECTOR3 unit(const VECTOR3& a) { return a / length(a); }

struct NTVERTEX { float x, y, z, nx, ny, nz, tu, tv; };
struct MESHGROUP { NTVERTEX* Vtx; WORD* Idx; DWORD nVtx, nIdx, MtrlIdx, TexIdx, UsrFlag; WORD zBias, Flags; };
const DWORD GRPEDIT_VTXCRD = 0x0007, GRPEDIT_VTXNML = 0x0038;
struct GROUPEDITSPEC { DWORD flags; DWORD UsrFlag; NTVERTEX* Vtx; DWORD nVtx; WORD* vIdx; };

void oapiWriteLogV(const char* fmt, ...);
MESHHANDLE oapiLoadMeshGlobal(const char* name);
MESHGROUP* oapiMeshGroup(MESHHANDLE mesh, DWORD idx);
int oapiEditMeshGroup(DEVMESHHANDLE dev, DWORD idx, GROUPEDITSPEC* ges);

class VESSEL
{
public:
	DEVMESHHANDLE GetDevMesh(VISHANDLE, UINT) const { return reinterpret_cast<DEVMESHHANDLE>(1); }
};
