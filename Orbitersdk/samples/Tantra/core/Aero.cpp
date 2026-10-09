#include "Aero.h"

#include <algorithm>
#include <cmath>

namespace tantra::aero {

namespace {
constexpr double kPi = 3.14159265358979323846;
double Smooth(double t) {
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
double Lerp(double a, double b, double t) { return a + (b - a) * t; }
}  // namespace

// Nose first. Subsonic: skin friction (~0.04 on frontal for this fineness) + base drag of the open well
// (0.08). Transonic rise to 0.58 at M 1.1 (normal shock ahead of the blunt ogive + base). Supersonic: wave
// and base drag fall ~1/M towards the hypersonic floor 0.22: blunted nose (R 1.8 m on a 17.6 m base), the
// chines and the crest roots in the flow, skin friction of a 146 m hull (fits the panel model's L/D).
double AxialNoseFirst(double M) {
    if (M < 0.8) return 0.12;
    if (M < 1.1) return Lerp(0.12, 0.58, Smooth((M - 0.8) / 0.3));
    return 0.20 + 0.42 / M;
}

// Stern first: the stern face (the clover of the four blocks and the well, ~57 % of the frontal area) meets the flow
// head-on, the rest of the section behind it slopes back. Subsonic like a blunt face (0.8), Newtonian stagnation
// 2.0 on the face and ~0.3 on the slopes: 1.2 on the frontal area at hypersonic speed (2026-10-09, was 1.8).
double AxialSternFirst(double M) {
    if (M < 0.8) return 0.8;
    if (M < 1.2) return Lerp(0.8, 1.05, Smooth((M - 0.8) / 0.4));
    return Lerp(1.05, 1.2, Smooth((M - 1.2) / 4.0));
}

// Cross-flow drag of the hull section (flat bottom, sharp chines): 1.2 subsonic, 1.5 by M 2,
// Newtonian 1.9 above M 6 (a little under the flat-plate 2.0: rounded upper corners).
double CrossFlow(double M) {
    if (M < 0.8) return 1.2;
    if (M < 2.0) return Lerp(1.2, 1.5, Smooth((M - 0.8) / 1.2));
    return Lerp(1.5, 1.9, Smooth((M - 2.0) / 4.0));
}

// Body frame: x forward, y up. Velocity in the body frame (cos a, -sin a) for a positive angle of attack.
// Axial force -CA*sign(cos a), normal force +CN; lift is the component along (sin a, cos a), drag the
// component against the velocity.
void BodyPitch(double a, double M, double* cl, double* cd) {
    const double s = std::sin(a), c = std::cos(a);
    const double ratio = kFrontal / kPlanform;
    const double slender = 2.0 * ratio * (M < 1.0 ? 1.0 : 1.0 / M);  // slender-body lift, fades supersonic
    const double CN = CrossFlow(M) * s * std::fabs(s) + slender * s * c;
    const double CA = (c >= 0.0 ? AxialNoseFirst(M) : AxialSternFirst(M)) * ratio;
    *cl = CN * c - CA * s * (c >= 0.0 ? 1.0 : -1.0);
    *cd = CA * std::fabs(c) + CN * s;
}

void BodyYaw(double b, double M, double* cl, double* cd) {
    const double s = std::sin(b), c = std::cos(b);
    const double CN = CrossFlow(M) * s * std::fabs(s);
    *cl = CN * c;
    *cd = CN * s;
}

// Plate: subsonic 2*pi*A/(A+2) linear + vortex lift 1.8 sin|sin|; supersonic linear 4/sqrt(M^2-1)
// (capped by the subsonic slope) + cross-flow; Newtonian 2 sin|sin| above M 5.
void Plate(double a, double M, double A, double* cl, double* cd) {
    const double s = std::sin(a), c = std::cos(a);
    const double kSub = 2.0 * kPi * A / (A + 2.0);
    double CN;
    if (M < 0.9) {
        CN = kSub * s * c + 1.8 * s * std::fabs(s);
    } else if (M < 5.0) {
        const double kSup = std::min(kSub, 4.0 / std::sqrt(std::max(0.2, M * M - 1.0)));
        const double sup = kSup * s * c + 2.0 * s * std::fabs(s);
        const double sub = kSub * s * c + 1.8 * s * std::fabs(s);
        CN = M < 1.2 ? Lerp(sub, sup, Smooth((M - 0.9) / 0.3)) : Lerp(sup, 2.0 * s * std::fabs(s), Smooth((M - 3.0) / 2.0));
    } else {
        CN = 2.0 * s * std::fabs(s);
    }
    const double cd0 = M < 0.9 ? 0.012 : 0.025;
    *cl = CN * c;
    *cd = cd0 + CN * s;
}

}  // namespace tantra::aero
