// «Грань» 25,4 м, the lander of «Тантра»: the fixed design numbers. No Orbiter dependencies.
// Source: Tantra_Design/lander_gran_254.json - the agreed form Т1Б-А scaled ×1,285 on all axes (the user's decision
// 2026-10-09), the folding fin 9,85 м, the suits «Каркас» 42 кг; the mass closure is the varA method ×K (lengths ×K, areas
// ×K², volumes ×K³), checked at K = 1 against Т1Б-А (127,44 т vs 127,45). The balance: the Newton tables of the mesh
// (gen_lander.py, the hidden faces fixed) by similarity x/K. Each number cites its json key; Т1Б-А numbers that only scale
// say «×K». «допущение» - a number of mine (or of the Н2 core), not in the json: the user decides. «не определено» - the
// json says so itself; the core keeps a number to run.
#pragma once

namespace tantra::lander::spec {

constexpr double kPi = 3.14159265358979323846;
constexpr double kG0 = 9.80665;
constexpr double kMu0 = 4e-7 * kPi;

// masses [kg] (masses_t)
constexpr double kMtom = 211.995e3;               // masses_t.m0 (полная: 14 чел., 2 контейнера, аргон 53,0 т)
constexpr double kDry = 146.056e3;                // masses_t.dry (с килем 6,818 т), без груза
constexpr double kPayload = 12.931e3;             // payload: 14 × (110,2 + 42) кг + контейнеры 10,8 т; базовый модуль и
                                                  // тяжёлые скафандры - не определено (0)
constexpr double kMentry = 165.286e3;             // masses_t.m_entry
constexpr double kMland = 159.486e3;              // masses_t.m_land
constexpr double kArgon = 53.009e3;               // masses_t.ar_tot
constexpr double kArgonAscent = 46.709e3;         // masses_t.ar_asc
constexpr double kArgonEntry = 6.3e3;             // masses_t.ar_entry: после выхода на орбиту, всё в кормовом (cg_modes[1]);
                                                  // как у Т1Б-А (не ×K): висение при 165 т ~54 с (решение пользователя)
constexpr double kCharges = 3.279e3;              // items.zaryady (∝ ёмкости накопителя, допущение calc1285)
constexpr double kChargesX = -3.855;              // cg_rows «zaryady» (−3,0 ×K)
constexpr double kNoseReserve = 500.0;            // emergencies «0,5 т неснижаемого резерва»; держится в кормовом (cg_modes[4])
constexpr double kNoseFlow = 5.0;                 // kg/s: ballistic_ru «0,25–0,5 т на пик ~100 с» → 0,5 т / 100 с

// the mass budget for the CG: {json key, t, x m} (cg_rows, без аргона; груз - экипаж и контейнеры). «zaryady» is taken
// from Propulsion::chargesKg. «energetika» x +4,16 - под полом салона, как в 3D-компоновке (в Т1Б-А x −3,0·K): иначе ЦМ
// входа позади и балансировки нет (решение пользователя). «chashi», «kolpaki» - на центре рядов −2,856
struct Item { const char* key; double t, x; };
constexpr Item kBudget[] = {
    {"korpus_obshivka", 13.057, -2.056}, {"korpus_bariera", 8.848, -2.056}, {"korpus_karkas", 10.122, -2.056},
    {"nos_kromki", 1.339, 11.289}, {"krylya", 7.831, -5.834}, {"koncevye", 1.167, -8.558}, {"elevony", 1.128, -10.563},
    {"schitok", 0.578, -9.650}, {"marsh", 10.811, -10.794}, {"chashi", 10.985, -2.856}, {"energetika", 9.537, 4.160},
    {"tenevoy_ekran", 4.128, 0.642}, {"kabina_sistemy", 3.468, 3.341}, {"germokabina", 0.405, 2.345},
    {"shassi_PO", 4.997, 8.674}, {"shassi_osn", 10.211, -4.626}, {"bak_kormovoy", 0.505, -7.324},
    {"bak_nosovoy", 2.766, 3.919}, {"bak_kryla", 0.778, -3.325}, {"magistrali", 0.150, -1.703},
    {"izol_korm", 0.056, -7.324}, {"izol_nos", 0.037, 3.919}, {"izol_kryla", 0.364, -3.325}, {"kolpaki", 29.373, -2.856},
    {"kormovaya_plita", 2.668, -9.702}, {"uzel: чаша - рама ряда", 0.084, -2.856},
    {"uzel: рама ряда - корпус", 0.084, -2.856}, {"uzel: маршевый - корпус", 0.081, -10.794},
    {"uzel: щиток - корпус", 0.117, -9.650}, {"uzel: концевая часть - крыло", 0.025, -8.558},
    {"uzel: элевон - крыло", 0.127, -10.563}, {"uzel: колпаки - рама ряда", 0.055, -2.856},
    {"uzel: крыло - корпус", 0.078, -3.212}, {"kil", 6.818, -9.134}, {"ekipazh_kokpit", 0.304, 6.580},
    {"ekipazh_salon", 1.826, 3.405}, {"kontejner_1", 5.400, -2.957}, {"kontejner_2", 5.400, -4.413},
};

// the argon tanks: 0 носовой, 1 кормовой, 2 в кессоне крыла
constexpr int kTanks = 3, kTNose = 0, kTAft = 1, kTWing = 2;
constexpr double kTankX[3] = {3.9192, -7.3245, -3.3247};   // tanks.x (3,05 и −5,7 ×K; кессон крыла)
constexpr double kTankCap[3] = {10.291e3, 30.851e3, 35.053e3};   // tanks.cap_t (×K³; кессон крыла)
constexpr double kTankWing0 = 11.867e3;           // masses_t.ar_wing
constexpr double kTankNose0 = 10.291e3;           // cg_modes[0]: носовой полный
constexpr double kTankAft0 = kArgon - kTankNose0 - kTankWing0;   // 30,851 т: кормовой полный (cg_modes[0])
// the transfer: носовой ↔ кормовой 80 кг/с двумя насосами (Т1Б-А layout_ru; насосы не масштабировались - допущение) →
// 40 кг/с на путь: 6,3 т входа за ~79 с двумя, ~158 с одним. Путь А основной, путь Б - второй насос/клапан.
constexpr double kPumpFlow = 40.0;                // kg/s один путь
constexpr double kTrimDead = 5.0;                 // kg: зона нечувствительности перекачки (допущение)

// the CG [m, x вдоль корпуса от начала меша, вперёд +] (cg_modes, balance)
constexpr double kXcgHover = -2.8563;             // центр рядов чаш = ЦМ висения при половине аргона входа (ряды следуют за
                                                  // ЦМ: у Т1Б-А −2,50 ×K = −3,21; киль и энергетика под салоном сдвинули ЦМ)
constexpr double kXcgStart = -3.132, kXcgOrbit = -2.935, kXcgGlide = -2.506, kXcgBallOld = -2.935, kXcgLand = -2.775;   // cg_modes
// the entry: все 6,3 т в носовом (бак 10,29 т вмещает) - ЦМ −2,506 (balance.x_glide_all_nose)
constexpr double kXBallNeedE0 = -3.0475;          // balance.x_need_e0: ЦМ не дальше назад - есть балансировка
constexpr double kXBallNeedE25 = -2.9725;         // balance.x_need_e-25 (элевоны −25°)
constexpr double kBallAlphaE25 = 58.3;            // deg balance.ball_e-25.alpha (аргон в носовом)
constexpr double kBallAlphaE0 = 47.5;             // deg balance.ball_e0.alpha
constexpr double kBallElevon = -25.0;             // deg movers elevon ballistic_deg
constexpr double kRunwayFlap = 10.0;              // deg щиток при касании ≤ 10° (меш: режим glide)
constexpr double kGlideFlap = 15.0;               // deg планирование: щиток 15°
constexpr double kGlideAlpha = 32.8;              // deg balance.glide_fl15 (элевоны 0, щиток 15°, ЦМ −2,506; L/D 1,37)

// field
constexpr double kB = 12.1;                       // T propulsion_ru (B²/2μ0 = 58,3 МПа)
constexpr double kBmax = 16.0;                    // T emergencies «переход на 16 Тл кратко»
constexpr double kChi = 1e-5;                     // ttx.chi_pct 0,001 %: доля мощности реакции в проникающем излучении

// units: 0,1 the marches (stern), 2..7 the lift cups (two rows of three)
constexpr int kMarch = 2, kLift = 6, kUnits = kMarch + kLift;
constexpr double kMarchF = 1351.3e3;              // N engines.F_main_each_kN (Т/В 1,30 на m0)
constexpr double kMarchV = 40e3;                  // m/s propulsion_ru «30–60 км/с (расчёт при 40 км/с)»; store.formula_ru
constexpr double kMarchS = 0.02642;               // m² 0,016 ×K² (B²/2μ0·S = 1,54 MN ≥ 1,35 MN at 12,1 T)
constexpr double kMarchX = 2.313;                 // m вбок: main_L/R z ±1,8 ×K
constexpr double kMarchPivotX = -10.28;           // m: ось УВТ −8,0 ×K; плечо до ЦМ - Propulsion::XCg()
constexpr double kTvcMax = 15.0;                  // deg movers main «УВТ ±15°»
constexpr double kLiftF = 466.0e3;                // N engines.F_cup_kN (Т/В 1,725 на массе входа); чаши Ø0,84 ×K = Ø1,08
constexpr double kLiftV = 15e3;                   // m/s hover_ru «15 км/с»; store.formula_ru
constexpr double kLiftS = 0.01156;                // m² 70 см² ×K² (B²/2μ0·S = 0,67 MN ≥ 0,47; нужно 80 см² - engines.cup_scaled)
constexpr double kLiftX = 3.855;                  // rows at ±3,0 ×K from the rows' centre
constexpr double kLiftZ[3] = {-1.9275, 0.0, 1.9275};   // m z ±1,5 ×K
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
// the marches' afterburner (форсаж, слово пользователя 2026-10-09: «жрёт топливо, но помогает в сложных разгонных траекториях;
// у корабля сразу небезопасно»): the same jet power at a lower exhaust speed - more thrust, more argon; the field up to 16 T
// (F ≤ B²/2μ0·S: 2,69 MN a march, ×1,99), the windings near their critical current and warmed by the denser plasma
constexpr double kAfterV = 19.3e3;                // m/s: 2P/v = ×2,07 at the march's power (допущение; поле ограничивает ×1,99)
constexpr double kAfterHeat = 21e3;               // W on a march winding in the afterburner (допущение: ~1 мин до предела)
constexpr double kAfterMargin = 0.06;             // the automat ends the afterburner at this current margin (допущение)
constexpr double kAfterSafeDist = 1000.0;         // m: closer to «Тантра» the afterburner is refused (допущение: струя)
constexpr double kQuenchJump = 12.0;              // K the windings take from their own field in a quench (допущение)

// shocks on a part [g] (допущение)
constexpr double kShockStall = 6.0, kShockQuench = 15.0, kShockBreak = 40.0;
constexpr double kShockPhoton = 9.0, kShockAnalog = 30.0, kShockCooler = 25.0, kShockTank = 25.0, kShockFrame = 20.0;
constexpr double kTankLeak = 30.0;                // kg/s from a holed tank (допущение)

// rows: door then frame (допущение: 2 s + 4 s)
constexpr double kDoorT = 2.0, kFrameT = 4.0;

// power: the store charged from «Тантра». E_треб = (E_струи выхода + E_висения)/Q, ёмкость E_треб/(1 − 0,25)
// (GSFC-STD-1000 Rev. I, табл. 1.06-1, до SRR - 25 %)
constexpr double kEjetAscent = 31.4778e12;        // J store.need_GJ × Q − E_висения (траектория ×K)
constexpr double kEjetHover = 0.5 * kArgonEntry * kLiftV * kLiftV;   // J ½·6,3 т·(15 км/с)² = 0,709 ТДж
constexpr double kStoreMargin = 0.25;             // GSFC-STD-1000, до SRR
constexpr double kStoreNeed = (kEjetAscent + kEjetHover) / kQ;          // 268,22 ГДж (store.need_GJ)
constexpr double kStoreDesign = kStoreNeed / (1.0 - kStoreMargin);       // 357,63 ГДж (store.design_GJ)
constexpr double kStoreE = 358e9;                 // J (≥ kStoreDesign)
static_assert(kStoreE >= kStoreDesign, "накопитель меньше формулы store");
constexpr double kStoreP = 570e6;                 // W пик триггеров маршевых 450,4 МВт (store.peak_asc_MW) + 25 % (допущение;
                                                  // в Т1Б-А было 500 МВт при пике 271)
constexpr double kPhotonP = 150e3;                // W the photonic contour (допущение)
constexpr double kAnalogP = 5e3;                  // W the analog contour; its own battery (допущение)
constexpr double kAnalogBatt = 20e6;              // J (допущение: > 1 h)
constexpr double kLifeP = 30e3;                   // W life support, cabin (допущение)
constexpr double kDriveP = 50e3;                  // W the frames, doors, УВТ, elevons (допущение)
constexpr double kRcsF = 15e3, kRcsV = 2e3;       // N, m/s: an argon attitude thruster (допущение; 25,4 м: ~2,5°/с² по тангажу)

// the cabin: 2 пилота рядом + 12 кресел полулёжа (2 ряда × 6, проход 0,77 м)
constexpr int kPilots = 2, kSeats = 12, kCrewMax = kPilots + kSeats;   // 14 (emergencies «O₂ на 14 человек»)

// the hangar (lander_mesh.json scale; «Тантра модули» 2026-10-09): проходить только в положении «стоянка»
constexpr double kLengthStowed = 25.443;          // m scale.by_mode.stowed.L (киль сложен влево)
constexpr double kLengthTransition = 25.822;      // m scale.by_mode.transition.L
constexpr double kHangarOpening = 25.7;           // m проём верхних ворот s 94,7–120,4

// TTX for the reports (tw)
constexpr double kTwStart = 1.345, kTwOneOutStart = 0.897;   // tw[0] TW_cups / TW_fail (212,0 т)
constexpr double kTwHover = 1.725, kTwOneOutHover = 1.15;    // tw[6] (165,29 т, висение)
constexpr double kTwMains = 1.3;                             // tw[0].TW_mains

}  // namespace tantra::lander::spec
