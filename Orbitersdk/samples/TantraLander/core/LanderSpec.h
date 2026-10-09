// «Грань» Т1Б-А, the lander of «Тантра»: the fixed design numbers. No Orbiter dependencies.
// Source: Tantra_Design/lander_gran_A.json, the design case «max» (ttx, mass_total_A.max, mass_budget, cg_modes, tw,
// store_A, tanks.max, balance_A.max, gear_A, hangar, hangar_check, cabin_A, hover_ru, layout_ru, emergencies, movers).
// Each number cites its json key. «допущение» - a number of mine (or of the Н2 core), not in the json: the user decides.
// «не определено» - the json says so itself; the core keeps the old number to run.
#pragma once

namespace tantra::lander::spec {

constexpr double kPi = 3.14159265358979323846;
constexpr double kG0 = 9.80665;
constexpr double kMu0 = 4e-7 * kPi;

// masses [kg] (mass_total_A.max)
constexpr double kMtom = 127.447e3;               // mass_total_A.max.m0 (ttx.mtom_t 127,45)
constexpr double kDry = 83.064e3;                 // mass_total_A.max.dry (ttx.dry_t 83,06), без груза
constexpr double kPayload = 10e3;                 // ttx.payload_t; mass_budget «gruz» x −1,5
constexpr double kMentry = 99.364e3;              // mass_total_A.max.m_entry (ttx.m_entry_t)
constexpr double kMland = 93.564e3;               // mass_total_A.max.m_land (ttx.m_land_t)
constexpr double kArgon = 34.384e3;               // mass_total_A.max.ar_tot (ttx.reaction_mass_t)
constexpr double kArgonAscent = 28.084e3;         // mass_total_A.max.ar_asc (ttx.argon_ascent_t)
constexpr double kArgonEntry = 6.3e3;             // ttx.argon_entry_t: после выхода на орбиту, всё в кормовом (cg_modes[1])
constexpr double kCharges = 2.0e3;                // mass_budget «zaryady» 2,0 т, x −3,0
constexpr double kChargesX = -3.0;                // mass_budget «zaryady»
constexpr double kNoseReserve = 500.0;            // emergencies «0,5 т неснижаемого резерва»; держится в кормовом (cg_modes[4])
constexpr double kNoseFlow = 5.0;                 // kg/s: ballistic_ru «0,25–0,5 т на пик ~100 с» → 0,5 т / 100 с

// the mass budget for the CG: {json key, t, x m} (mass_budget, без аргона). «zaryady» is taken from Propulsion::chargesKg.
struct Item { const char* key; double t, x; };
constexpr Item kBudget[] = {
    {"korpus_obshivka", 6.353, -1.6}, {"korpus_bariera", 5.359, -1.6}, {"korpus_karkas", 4.336, -1.6},
    {"nos_kromki", 0.811, 8.79}, {"krylya", 4.846, -4.54}, {"koncevye", 0.707, -6.66}, {"elevony", 0.683, -8.22},
    {"schitok", 0.35, -7.51}, {"marsh", 6.499, -8.4}, {"chashi", 6.803, -2.5}, {"energetika", 5.817, -3.0},
    {"tenevoy_ekran", 2.5, 0.5}, {"kabina_sistemy", 2.1, 2.6}, {"germokabina", 0.231, 2.49},
    {"shassi_PO", 2.603, 6.75}, {"shassi_osn", 5.319, -3.6}, {"bak_kormovoy", 0.306, -5.7}, {"bak_nosovoy", 1.638, 3.05},
    {"bak_kryla", 1.138, -3.34}, {"magistrali", 0.15, -1.33}, {"izol_korm", 0.034, -5.7}, {"izol_nos", 0.022, 3.05},
    {"izol_kryla", 0.555, -3.34}, {"kolpaki", 19.982, -2.5}, {"kormovaya_plita", 1.614, -7.55},
    {"uzel: чаша → рама ряда", 0.039, -2.5}, {"uzel: рама ряда → корпус", 0.039, -2.5}, {"uzel: маршевый → корпус", 0.038, -8.4},
    {"uzel: щиток → корпус", 0.055, -7.51}, {"uzel: концевая часть → крыло", 0.012, -6.66}, {"uzel: элевон → крыло", 0.059, -8.22},
    {"uzel: колпаки → рама ряда", 0.029, -2.5}, {"uzel: крыло → корпус", 0.035, -2.5}, {"gruz", 10.0, -1.5},
};

// the argon tanks: 0 носовой, 1 кормовой, 2 в кессоне крыла
constexpr int kTanks = 3, kTNose = 0, kTAft = 1, kTWing = 2;
constexpr double kTankX[3] = {3.05, -5.7, -3.337};    // mass_budget «bak_nosovoy», «bak_kormovoy»; tanks.max.x_wing
constexpr double kTankCap[3] = {4.85e3, 14.54e3, 15.53e3};   // layout_ru «4,85 т», «14,54 т»; ttx.wing_tank_capacity_t
constexpr double kTankWing0 = 15.004e3;           // mass_total_A.max.ar_wing (ttx.argon_wing_t 15,0)
constexpr double kTankNose0 = 4.85e3;             // cg_modes[0] «носовой 4,85»
constexpr double kTankAft0 = kArgon - kTankNose0 - kTankWing0;   // 14,53 т (cg_modes[0] округляет: «кормовой 14,54»)
// the transfer: носовой ↔ кормовой 80 кг/с двумя насосами (layout_ru), «61 с двумя, 121 с одним» (emergencies) → 40 кг/с
// на путь. Путь А основной, путь Б - второй насос/клапан (open_issues_ru: без него при отказе перекачки нет балансировки).
constexpr double kPumpFlow = 40.0;                // kg/s один путь (4,85 т / 121 с)
// крыло → кормовой: «магистраль к кормовому баку» (mesh_changes) - расход не определено; допущение: те же два пути
constexpr double kTrimDead = 5.0;                 // kg: зона нечувствительности перекачки (допущение)

// the CG [m, x вдоль корпуса] (cg_modes, balance_A.max)
constexpr double kXcgHover = -2.50;               // cg_modes[5]: центр рядов чаш, перекачкой носовой 1,36 / кормовой 4,94
constexpr double kXcgStart = -2.687, kXcgOrbit = -2.62, kXcgGlide = -2.192, kXcgBallOld = -2.62, kXcgLand = -2.429;   // cg_modes[0..4]
constexpr double kXBallNeedE0 = -2.5195;          // balance_A.max.x_ball_need_e0: ЦМ не дальше назад - есть балансировка
constexpr double kXBallNeedE25 = -2.4695;         // balance_A.max.x_ball_need_e-25 (элевоны −25°)
constexpr double kBallAlphaE25 = 63.8;            // deg balance_A.max.ball_nose_e-25.alpha (аргон в носовом)
constexpr double kBallAlphaE0 = 53.8;             // deg balance_A.max.ball_nose_e0.alpha
constexpr double kBallElevon = -25.0;             // deg movers elevon ballistic_deg
constexpr double kRunwayFlap = 10.0;              // deg wing_calc / changes_ru «щиток при касании ≤ 10°» (меш: режим glide)
constexpr double kGlideFlap = 15.0;               // deg balance_A.note_ru «Планирование: щиток 15°»
constexpr double kGlideAlpha = 42.3;              // deg balance.max.glide (элевоны 0, щиток 15°)

// field
constexpr double kB = 12.1;                       // T propulsion_ru (B²/2μ0 = 58,3 МПа)
constexpr double kBmax = 16.0;                    // T emergencies «переход на 16 Тл кратко»
constexpr double kChi = 1e-5;                     // ttx.chi_pct 0,001 %: доля мощности реакции в проникающем излучении

// units: 0,1 the marches (stern), 2..7 the lift cups (two rows of three)
constexpr int kMarch = 2, kLift = 6, kUnits = kMarch + kLift;
constexpr double kMarchF = 812.4e3;               // N ttx.thrust_main_kN
constexpr double kMarchV = 40e3;                  // m/s propulsion_ru «30–60 км/с (расчёт при 40 км/с)»; store.formula_ru
constexpr double kMarchS = 0.016;                 // m² допущение Н2 (B²/2μ0·S = 0,93 MN ≥ 0,81 MN at 12,1 T)
constexpr double kMarchX = 1.8;                   // m вбок: geometry.thrusters main_L/R origin z ±1,8
constexpr double kMarchPivotX = -8.0;             // m: movers main_L/R pivot x (ось УВТ); плечо до ЦМ - Propulsion::XCg()
constexpr double kTvcMax = 15.0;                  // deg movers main «УВТ ±15°»
constexpr double kLiftF = 280.1e3;                // N ttx.thrust_cup_kN
constexpr double kLiftV = 15e3;                   // m/s hover_ru «15 км/с»; store.formula_ru
constexpr double kLiftS = 0.007;                  // m² lift_units_ru «принят 70 см²» (B²/2μ0·S = 0,41 MN ≥ 0,28)
constexpr double kLiftX = 3.0;                    // rows at ±3,0 m from the rows' centre (lift_units_ru «x −2,50 ± 3,0»)
constexpr double kLiftZ[3] = {-1.5, 0.0, 1.5};    // m lift_units_ru «z −1,5 / 0 / +1,5»
constexpr double kEmV = 11.5e3;                   // m/s: в А нет; допущение Н2 (аварийный режим чаш)
constexpr double kFrontSwing = 8.0;               // deg movers row_F min_deg −8 («качание ±8°»)

// the stall: thrust falls to zero in ~1 s, a restart from the second charge injector (emergencies)
constexpr double kStallTau = 0.3;                 // s (e^-3 at 1 s)
constexpr double kRestart = 1.5;                  // s with the second injector (допущение)
constexpr double kTriggerT = 0.5;                 // s to charge the trigger (допущение)
constexpr double kRampLift = 4.0, kRampMarch = 1.5;   // T/s field ramp (допущение)
constexpr double kQ = 120.0;                      // store.Q / store_A.formula_ru: the store gives P_jet/Q
constexpr double kChargeShare = kCharges / kArgon;    // kg of charges per kg of argon (допущение Н2: заряды на весь аргон)

// windings (REBCO, as «Тантра»): I/Ic = 0,58 at 20 K and 12,1 T - в А «запас по критическому току не определён, проводник
// не задан» (open_issues_ru, norms coil); числа Н2 оставлены, чтобы ядро работало: не определено
constexpr double kIratio = 0.58, kTc = 92.0, kIcB = 0.5;
constexpr double kCoilBase = 1.5e3;               // W at 20 K per unit: leads, conduction (допущение)
constexpr double kCoilRadShare = 2e-3;            // of the χ radiation the windings take (допущение)
constexpr double kEmHeat = 4.0;                   // the emergency mode's heat on the windings, × (допущение)
constexpr double kLineLift = 5e3, kLineMarch = 20e3;  // W at 20 K: a unit's cold line (допущение)
constexpr double kCLift = 17e3, kCMarch = 40e3;   // J/K the windings (допущение)
constexpr double kCooler = 30e3;                  // W at 20 K a winding cryocooler, two of them (допущение; cryo_A - это
                                                  // холод аргона 87 K, на земле, на борту 0 т - к обмоткам не относится)
constexpr double kCOP = 60.0;                     // W of electricity per W at 20 K (допущение)
constexpr double kEmMargin = 0.20;                // the automat ends the emergency mode at this current margin (допущение)
constexpr double kQuenchJump = 12.0;              // K the windings take from their own field in a quench (допущение)

// shocks on a part [g] (допущение)
constexpr double kShockStall = 6.0, kShockQuench = 15.0, kShockBreak = 40.0;
constexpr double kShockPhoton = 9.0, kShockAnalog = 30.0, kShockCooler = 25.0, kShockTank = 25.0, kShockFrame = 20.0;
constexpr double kTankLeak = 30.0;                // kg/s from a holed tank (допущение)

// rows: door then frame (допущение: 2 s + 4 s)
constexpr double kDoorT = 2.0, kFrameT = 4.0;

// power: the store charged from «Тантра». store_A.formula_ru: E_треб = (E_струи выхода + E_висения)/Q, ёмкость
// E_треб/(1 − 0,25) (GSFC-STD-1000 Rev. I, табл. 1.06-1, до SRR - 25 %)
constexpr double kEjetAscent = 18.9248e12;        // J store.max.E_asc_TJ (по траектории)
constexpr double kEjetHover = 0.5 * kArgonEntry * kLiftV * kLiftV;   // J store.formula_ru «½·6,3 т·(15 км/с)²» = 0,709 ТДж
constexpr double kStoreMargin = 0.25;             // store_A.formula_ru (GSFC-STD-1000, до SRR)
constexpr double kStoreNeed = (kEjetAscent + kEjetHover) / kQ;          // 163,6 ГДж (store_A.max.need_GJ)
constexpr double kStoreDesign = kStoreNeed / (1.0 - kStoreMargin);       // 218,15 ГДж (store_A.max.design_GJ)
constexpr double kStoreE = 219e9;                 // J store_A.new_kStoreE_GJ (≥ kStoreDesign)
static_assert(kStoreE >= kStoreDesign, "накопитель меньше формулы store_A");
constexpr double kStoreP = 500e6;                 // W store_A.kStoreP_MW (пик триггеров 271 МВт - ttx.store_peak_MW)
constexpr double kPhotonP = 150e3;                // W the photonic contour (допущение)
constexpr double kAnalogP = 5e3;                  // W the analog contour; its own battery (допущение)
constexpr double kAnalogBatt = 20e6;              // J (допущение: > 1 h)
constexpr double kLifeP = 30e3;                   // W life support, cabin (допущение)
constexpr double kDriveP = 50e3;                  // W the frames, doors, УВТ, elevons (допущение)
constexpr double kRcsF = 2e3, kRcsV = 2e3;        // N, m/s: the argon attitude thrusters (допущение)

// the cabin (cabin_A.scheme_ru): 2 пилота рядом + 12 кресел (3 ряда × 4)
constexpr int kPilots = 2, kSeats = 12, kCrewMax = kPilots + kSeats;   // 14 (emergencies «O₂ на 14 человек»)

// the hangar (hangar, hangar_check): проходить только в положении «стоянка»
constexpr double kLengthStowed = 19.80;           // m hangar.stowed_bbox_m[0] (ttx.length_m)
constexpr double kLengthTransition = 20.095;      // m hangar.reason_ru «в переходе 20,095 м»
constexpr double kHangarOpening = 20.0;           // m hangar_check.opening_m

// TTX for the reports (tw, design case max)
constexpr double kTwStart = 1.345, kTwOneOutStart = 0.897;   // tw[0] TW_cups / TW_fail (127,46 т)
constexpr double kTwHover = 1.725, kTwOneOutHover = 1.15;    // tw[5] (99,36 т, висение)
constexpr double kTwMains = 1.3;                             // tw[0].TW_mains

}  // namespace tantra::lander::spec
