// Tantra core: aerodynamic coefficients of the C-148 hull, crests, fin and gear, from subsonic to
// hypersonic. No Orbiter dependencies (tests/aero_table.cpp prints them).
//
// Body: axial force on the frontal area (skin friction + base drag of the open engine well subsonic,
// transonic rise, wave drag falling with Mach, blunt-nose Newtonian limit; stern first the well rim is
// the blunt face), normal force on the planform by cross-flow (Allen: Cd_c 1.2 subsonic) rising to the
// Newtonian 2 sin^2(a) of the flat bottom at hypersonic speed, plus a slender-body linear term that fades
// supersonic. Calibrated to the panel model of tantra_c148.html: hypersonic L/D 1.4 at 30 deg ... 0.3 at 70.
// Plates (crests, fin): low-aspect subsonic lift with vortex lift, linear supersonic 4/sqrt(M^2-1), and
// Newtonian above Mach 5.
#pragma once

namespace tantra::aero {

// Reference geometry of the hull (m, m^2).
constexpr double kLength = 168.968;
constexpr double kPlanform = 3589.0;   // hull planform, T8 (wings are their own airfoils)
constexpr double kFrontal = 407.0;     // largest cross-section (fairings, s 40..80)
constexpr double kSide = 2649.0;       // side projection

double AxialNoseFirst(double mach);    // on the frontal area
double AxialSternFirst(double mach);   // on the frontal area: the well rim and baffle are the front
double CrossFlow(double mach);         // normal-force coefficient of a body section, per sin^2

// Body in the pitch plane: aoa [rad], coefficients referred to kPlanform.
void BodyPitch(double aoa, double mach, double* cl, double* cd);
// Body in the yaw plane: sideslip [rad], coefficients referred to kSide (no axial part).
void BodyYaw(double beta, double mach, double* cl, double* cd);
// Thin low-aspect plate (crest, fin), coefficients referred to its own area.
void Plate(double aoa, double mach, double aspect, double* cl, double* cd);

}  // namespace tantra::aero
