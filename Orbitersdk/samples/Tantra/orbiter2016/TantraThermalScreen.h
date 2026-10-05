// TantraThermalScreen: the thermal page of the left riser (the tab ТЕПЛО beside МЕХАНИЗАЦИЯ) - the mockup
// Tantra_Design/tantra_thermal_screen.html (the user, 2026-10-04) carried over in its own pixels and colours (1600 x 800):
//   the skin's thermal map (the hull drawn level, the free stream at the angle of attack, the plasma and the bow shock, the
//   zones' callouts, the colour scale of hot metal), the zones' table (T, limit, margin, flux, dT/dt, 5 minutes of history, the
//   forecast peak) with the closest zone and the advice, the entry corridor (altitude over speed: below each line the zone passes
//   its limit in radiative equilibrium; the g limit; the flown track and the forecast), the flight and the forecast to the end of
//   the entry with the events, the engines' temperatures (the stern, the cup, the jacket, the winding, the pods, the radiators,
//   the anamezon chambers, the nose retro cups).
// The game's data in place of the mockup's scenario: the zones are core/Damage's (Tantra::damage_), the engines the power
// plant's (core/Plant, the same derived temperatures as the plant screen's thermal map); the mockup's sliders (mass, angle of
// attack, entry angle, bank, thrust), its scenario and time keys are gone - the ship flies, the screen reads it. The title bar
// carries the tabs МЕХАНИЗАЦИЯ | ТЕПЛО where the mechanisation screen has them (the page switches without the tabs moving).
// The forecast is the mockup's predict() with the game's models in place of its stand-ins: the point-mass entry at the held
// attitude with core/Aero's coefficients (the airfoils of Tantra::DefineAerodynamics), the skin by a copy of the ship's own
// core/Damage model stepped forward. RunForecast() is pure (no Orbiter calls): the ship calls it about once a second while
// Entry() and hands the result to SetForecast().
#pragma once
#include "orbitersdk.h"
#include "TantraScreenCanvas.h"
#include "../core/Damage.h"

#include <array>
#include <deque>
#include <string>
#include <utility>
#include <vector>

namespace tantra::thermalscreen {

enum Cmd { kCmdTabMech, kCmdTabThermal };   // the title bar's tabs: to the mechanisation page / this page

constexpr int kZones = tantra::damage::kZoneCount;   // nose, belly, wing edges, fin, stern, gear, pods

// the atmosphere of the forecast and the corridor: Orbiter's own density by altitude when the ship fills the table
// (oapiGetPlanetAtmParams at its longitude and latitude, every 2 km), else the mockup's Earth, 1.225 e^(-h / 8500) - nothing
// above 200 km either way; between the table's points log-linear, above its top extrapolated with its top scale height
struct Air {
    static constexpr int kN = 66;              // 0 .. 130 km
    static constexpr double kStep = 2000.0;    // [m]
    double rho[kN] = {};                       // [kg/m^3]; rho[0] == 0: no table
    double Rho(double h) const;                // the density at an altitude [kg/m^3]
    double Alt(double rho) const;              // the altitude of a density [m] (< 0: denser than at the ground)
};
struct Body { double R = 6371e3, g0 = 9.81; };   // the planet: mean radius [m], surface gravity [m/s^2] (the mockup's Earth)

// the ship on its path, as the forecast and the corridor need it
struct Path {
    double h = 0.0, v = 0.0, gamma = 0.0;      // altitude [m], airspeed [m/s], flight path angle of the airspeed [rad] (< 0 down)
    double mass = 0.0;                         // [kg]
    double aoa = 0.0, bank = 0.0;              // [rad]: the forecast holds them (the pilot or the autopilot keeps the attitude)
    double crestAvail = 1.0;                   // the wings in the flow: Tantra::aeroCrest_ (0 folded .. 1 out at 90 deg)
    double gearArea = 0.0;                     // the gear and the pods in the flow: Tantra::aeroGearArea_ [m^2]
    tantra::damage::Exposure expo;             // what the hull presents to the flow - as Tantra::UpdateDamage builds it
    Air air;
    Body body;
};

// the forecast: the ship's state and a copy of its zone model run forward to the end of the entry (the mockup's predict())
struct ForecastIn {
    Path path;
    tantra::damage::Model skin;                // a copy of Tantra::damage_ (its temperatures now); stepped with damage off
    double ambientT = 250.0;                   // the air's temperature for the radiative equilibrium [K] (the mockup's)
};
struct Forecast {
    bool valid = false;
    double peakT[kZones] = {}, peakAt[kZones] = {};   // each zone's highest temperature ahead [K] and in how long [s] (0: now)
    double nMax = 0.0, nMaxAt = 0.0;           // the aerodynamic load's peak [g] and in how long [s]
    double qMax = 0.0, qMaxAt = 0.0;           // the nose's heat flux peak [W/m^2] and in how long [s]
    double endH = 0.0, endV = 0.0, endT = 0.0; // where the entry ends (slower than 600 m/s or below 12 km) and in how long [s]
    bool skip = false;                         // it ends climbing out above 125 km: a skip
    std::vector<scr::Pt> track;                // every 4 s: x the speed [m/s], y the altitude [m]
};
// up to 6000 steps of 1 s (to the end of any entry): 1 .. 8 ms; pure - no Orbiter calls
Forecast RunForecast(const ForecastIn& in);

// the ship as the screen shows it
struct View {
    double t = 0.0;                            // sim time [s]: the journal, the history, the stream's dashes
    // the skin: core/Damage's zones (nose, belly, wing edges, fin, stern, gear, pods)
    double skin[kZones] = {}, lim[kZones] = {}, flux[kZones] = {};   // temperature [K], material limit [K], heat flux [W/m^2]
    double edgeIntegrity = 1.0;                // the wings, the lower of the two (damage::kCrestPort / kCrestStbd)
    bool hullLost = false;                     // damage::Model::Destroyed()
    // the flight
    Path path;
    double rho = 0.0, mach = 0.0, q = 0.0;     // Orbiter's air density [kg/m^3], Mach number (0 in vacuum), dynamic pressure [Pa]
    double gLoad = 0.0;                        // the aerodynamic load (lift and drag over the weight) [g]
    double gLimit = 4.0;                       // the load limit of the advice and the corridor [g]
    bool onGround = false;
    int wingMode = 0;                          // 0 = 90 deg, 1 = 30 deg, 2 = folded
    double wingFold = 0.0;                     // the folding now 0 (as set) .. 1 (folded flat, on the ground)
    // the power plant (core/Plant)
    bool plantRun = false, plantLost = false;  // on the run; the stern burnt through (the ship lost)
    double sternT = 300.0, coilT = 20.0;       // the stern structure, the field winding [K]
    double tSafe = 800.0, tLost = 2300.0;      // the stern's safe and burn-through temperatures [K]
    double thr = 0.0;                          // the march cup's level 0..1
    int reactMass = 0;                         // the reaction mass in use: 0 argon, 1 iron, 2 the bare products
    double heatIn = 0.0, mdot = 0.0;           // the heat into the stern [W], the reaction mass flow [kg/s]
    int pods = 0;                              // 0 in the bays, 1 out, 2 thrusting
    double podLevel = 0.0;                     // their level 0..1
};

class Screen {
public:
    // the page on the riser: w x h of the mockup's pixels (1600 x 800) at (ox, oy) of the surface
    void Draw(oapi::Sketchpad* skp, ScreenFont& font, int ox, int oy, int w, int h, const View& v);
    int Hit(double x, double y) const { return scr::HitTest(hits_, x, y, nullptr); }
    // the entry's state, the history, the heat load, the journal: Draw calls it, and the ship calls it about once a second
    // whatever page the riser shows (else Entry() would not see an entry begin behind the МЕХАНИЗАЦИЯ page); a second call at
    // the same sim time changes nothing
    void Track(const View& v);
    bool Entry() const { return entry_; }      // an entry is going on (kept by Track): the ship runs RunForecast about once a second
    void SetForecast(const Forecast& f) { fc_ = f; }
    void Note(double t, const std::wstring& s, int lvl) { log_.Add(t, s, lvl); }

private:
    void ThermalMap(scr::Canvas& g, const View& v, const double* sk);
    void ZoneTable(scr::Canvas& g, const View& v, const double* sk, double W);
    void CorridorPlot(scr::Canvas& g, const View& v, double H);
    void FlightBox(scr::Canvas& g, const View& v, double H);
    void EngineBox(scr::Canvas& g, const View& v, const double* sk, double W, double H);
    std::pair<std::wstring, unsigned> Advice(const View& v) const;
    bool Climbing(const View& v) const;
    std::vector<scr::Hit> hits_;
    scr::Journal log_;
    std::deque<std::array<double, kZones>> hist_;   // every 2 s, 5 minutes
    std::deque<scr::Pt> track_;                     // the flown (speed, altitude) every 2 s
    double prev_[kZones] = {}, dT_[kZones] = {};
    bool crossed_[kZones] = {};
    double lastT_ = 0.0, histT_ = 0.0, heat_ = 0.0; // heat_: the nose's heat load since the entry began [J/m^2]
    bool seen_ = false, entry_ = false, edgeHurt_ = false;
    Forecast fc_;
    View last_;
};

}  // namespace tantra::thermalscreen
