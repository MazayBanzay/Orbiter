// Tantra planetary power plant: the physics model shared by every tab of tantra_plant.html.
// One magnetic-nozzle cup: pulsed fusion in the throat behind the cup; the plasma presses on the field, the field on
// the coils, the coils on the ship. Limits: field pressure (B^2/2mu0 x area), fusion power, exhaust speed (reaction
// mass), heat on the stern (penetrating radiation), coil stress (B^2). Numbers of the ship from core/Spec.h, Params.h.
"use strict";
const PM = (() => {
  const MU0 = 4e-7 * Math.PI, SIG = 5.670e-8, E_FUS = 7.0e13;   // J per kg of p-11B fuel
  const B_NOM = 12.1, B_RUPTURE = B_NOM * Math.SQRT2;         // twice the design stress: the winding tears
  const ETA_N = 0.9;                                         // magnetic nozzle: charged products -> jet
  const R_MARCH = 2.2, R_POD = 0.8, N_POD = 12, R_STERN = 15.0;
  const A_MARCH = Math.PI * R_MARCH * R_MARCH, A_POD = Math.PI * R_POD * R_POD;
  const PF_MARCH = 2.2e14;                                   // W fusion at 100 % (jet 1.33e14 W - today's iron mode)
  const H_COOL = {argon: 1.5e6, iron: 6.0e6, products: 0};   // J/kg the reaction mass carries away cooling the stern
  const V_MODE = {argon: 3.0e4, iron: 3.0e5, products: 1.0e7};
  const RAD_AREA = 2700, RAD_T = 1500, RAD_EPS = 0.9;        // the crests and the fin are the radiators
  const STERN_STORE = 1.0e12;                                // J the stern structure soaks (1 kt x 700 J/kgK x 1400 K)
  const VARIANTS = [
    {k: "real",  t: "Реальный p-¹¹B (равновесная плазма)", chi: 0.35,
     d: "Тормозное излучение электронов — треть энергии синтеза. Так работает любой известный нам термоядерный синтез."},
    {k: "noneq", t: "Неравновесный p-¹¹B (лучшие оценки)", chi: 0.03,
     d: "Горячие ионы, холодные электроны: излучения в 10 раз меньше. Предел современных проектов."},
    {k: "canon", t: "Ионно-триггерный, каскад (канон, χ 0,1 %)", chi: 1e-3,
     d: "Каскадная реакция ионизированного вещества (роман, сноска 11): энергия уходит в заряженные частицы, излучения тысячная доля."},
    {k: "clean", t: "Ионно-триггерный «чистый» (χ 0,001 %)", chi: 1e-5,
     d: "Практически без проникающего излучения. Единственное допущение уровня анамезона — всё остальное реально."},
  ];
  // the part of the radiation from the burn (on the axis, d behind the cup) that hits the stern face: the cup shades
  // the centre (R 2.2), the stern face out to R 15 sees it
  function sternFraction(d) {
    const c1 = d / Math.hypot(d, R_MARCH), c2 = d / Math.hypot(d, R_STERN);
    return (c1 - c2) / 2;
  }
  const fieldThrust = (B, A) => B * B / (2 * MU0) * A;
  // one cup at a field, a power cap and a reaction mass; mode sets the exhaust speed (iron: the speed at which full
  // power just meets the field, but not below 300 km/s; products: the free exhaust speed vEx)
  function cup(o) {
    const A = o.A || A_MARCH, B = o.B, PfMax = o.PfMax, mass = o.mass;
    const Ffield = fieldThrust(B, A);
    // iron: 300 km/s, slower when the field allows more thrust than the power gives at that speed (the computer takes
    // the whole field: lower speed = more flow = more thrust per watt, and the flow cools the stern)
    let v = o.vEx || V_MODE[mass];
    if (mass === "iron" && !o.vEx) v = Math.max(V_MODE.argon, Math.min(V_MODE.iron, 2 * ETA_N * PfMax * (1 - o.chi) / Ffield));
    let Pf = PfMax, F = 2 * ETA_N * Pf * (1 - o.chi) / v, lim = "мощность";
    if (F > Ffield) { F = Ffield; Pf = F * v / (2 * ETA_N * (1 - o.chi)); lim = "поле"; }
    const mdot = F / v, fuel = Pf / E_FUS;
    const heatIn = o.chi * Pf * sternFraction(o.d);
    const cool = mdot * H_COOL[mass] + radiators(o.deployed);
    return {F, v, mdot, Pf, fuel, heatIn, cool, lim, Ffield, Pjet: F * v / 2};
  }
  function radiators(deployed) { return RAD_EPS * SIG * Math.pow(RAD_T, 4) * RAD_AREA * (deployed ? 1 : 0.1); }
  // the cup held at the heat it can shed (the computer's protection): scale the power down
  function cupHeatLimited(o) {
    const c = cup(o);
    if (c.heatIn <= c.cool) return c;
    // heatIn ~ Pf, cooling by the reaction mass ~ Pf too (mdot ~ Pf at fixed v): solve heat(Pf) = cool(Pf)
    const kHeat = c.heatIn / c.Pf, kCool = (c.mdot * H_COOL[o.mass]) / c.Pf, rad = radiators(o.deployed);
    const Pf = kHeat > kCool ? Math.min(c.Pf, rad / (kHeat - kCool)) : c.Pf;
    const s = Pf / c.Pf;
    return {...c, F: c.F * s, mdot: c.mdot * s, Pf, fuel: c.fuel * s, heatIn: c.heatIn * s, cool: c.mdot * s * H_COOL[o.mass] + rad, lim: "тепло", Pjet: c.Pjet * s};
  }
  // ---- hazards (balance proposal, grounded in the physics: stress ~ B^2, quench margin falls with field, the
  // ignition drivers past their duty, the stern past its heat store) ----
  // per-minute probabilities, log-interpolated
  const QUENCH = [[12.1, 1e-6], [13.0, 1e-4], [14.0, 1e-3], [15.0, 5e-3], [16.0, 2e-2], [16.5, 5e-2], [17.0, 0.2], [B_RUPTURE, 1.0]];
  const MISFIRE = [[100, 0], [110, 1e-3], [120, 5e-3], [130, 2e-2], [140, 6e-2]];
  function interpLog(tab, x) {
    if (x <= tab[0][0]) return tab[0][1];
    for (let i = 1; i < tab.length; i++) if (x <= tab[i][0]) {
      const [x0, y0] = tab[i - 1], [x1, y1] = tab[i], f = (x - x0) / (x1 - x0);
      if (y0 <= 0) return y1 * f;
      return Math.exp(Math.log(y0) + f * (Math.log(y1) - Math.log(y0)));
    }
    return tab[tab.length - 1][1];
  }
  const quenchPerMin = B => interpLog(QUENCH, B);
  const misfirePerMin = pPct => interpLog(MISFIRE, pPct);
  // fatigue of the windings: life at 16 T (minutes), Basquin exponent 6 on the stress; none below the nominal field
  function coilWearPerSec(B, lifeAt16) {
    const s = (B / B_NOM) ** 2;
    if (s <= 1.0001) return 0;
    if (B >= B_RUPTURE) return Infinity;
    return 1 / (lifeAt16 * 60 * Math.pow(((16 / B_NOM) ** 2) / s, 6));
  }
  const fieldEnergy = B => B * B / (2 * MU0) * 120;   // J in the cup's field volume (~120 m^3): what a quench must dump
  // ---- the ways the installation burns the ship (on top of the reaction's own radiation) ----
  // 1. Overpower: more fusion than the field holds - the plasma pressure opens the field and the excess jet power
  //    lands on the cup and the stern (30 % of it). The limiter never lets it happen; without the limiter it is physics.
  const LEAK = 0.3;
  function overpowerHeat(PfReq, Ffield, v, chi) {
    const want = ETA_N * PfReq * (1 - chi), held = Ffield * v / 2;
    return Math.max(0, want - held) * LEAK;
  }
  // 2. The jet off the ground on the stand: the back-scatter of plasma, vapour and dust onto the stern, falling with
  //    the stern's height (R0 20 m). Argon was chosen for this: slow dense jet, little back-scatter, 30 t/s to cool.
  const REFLECT = {argon: 0.002, iron: 0.004, products: 0.01};
  function groundHeat(Pjet, sternH, mass) { const f = 20 / (20 + Math.max(0, sternH)); return Pjet * REFLECT[mass] * f * f; }
  // 3. A fast jet in the air: the mixing layer heats the air to tens of thousands of kelvin, it shines on the hull
  //    (iron also burns in oxygen). Argon below 30 km for exactly this reason.
  const AIRLAYER = {argon: 0, iron: 0.002, products: 0.003};
  function airHeat(Pjet, rho, mass) { return Pjet * AIRLAYER[mass] * Math.min(2, rho / 1.225); }
  const DAMAGE_AT = 1.0, LOST_AT = 1.5;               // of the stern's heat store: damage, burn-through (ship lost)
  return {MU0, SIG, E_FUS, B_NOM, B_RUPTURE, ETA_N, R_MARCH, R_POD, N_POD, R_STERN, A_MARCH, A_POD, PF_MARCH, H_COOL, V_MODE,
          RAD_AREA, STERN_STORE, VARIANTS, sternFraction, fieldThrust, cup, cupHeatLimited, radiators, quenchPerMin, misfirePerMin,
          coilWearPerSec, fieldEnergy, QUENCH, MISFIRE, overpowerHeat, groundHeat, airHeat, REFLECT, AIRLAYER, DAMAGE_AT, LOST_AT};
})();
