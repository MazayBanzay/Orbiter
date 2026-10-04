// tantra_plant.html: view, MFD, ascent, transfer, risks, variants. Physics: tantra_plant_model.js (PM).
"use strict";
const $ = id => document.getElementById(id);
const fmt = (v, d = 1) => isFinite(v) ? v.toLocaleString("ru-RU", {minimumFractionDigits: d, maximumFractionDigits: d}) : "—";
const fF = F => F >= 1e9 ? fmt(F / 1e9, 2) + " ГН" : F >= 1e6 ? fmt(F / 1e6, 0) + " МН" : F >= 1e3 ? fmt(F / 1e3, 0) + " кН" : fmt(F, 0) + " Н";
const fW = W => W >= 1e12 ? fmt(W / 1e12, 1) + " ТВт" : W >= 1e9 ? fmt(W / 1e9, 1) + " ГВт" : W >= 1e6 ? fmt(W / 1e6, 0) + " МВт" : fmt(W / 1e3, 0) + " кВт";
const fV = v => fmt(v / 1e3, 0) + " км/с";
const fM = m => m >= 1000 ? fmt(m / 1000, 2) + " т/с" : m >= 1 ? fmt(m, 1) + " кг/с" : fmt(m * 1000, 0) + " г/с";
const fP = p => p >= 0.1 ? fmt(p * 100, 0) + " %" : p >= 0.001 ? fmt(p * 100, 2) + " %" : p > 0 ? fmt(p * 100, 4) + " %" : "0";

const PLANETS = [
  {k: "earth", t: "Земля", g: 9.81, R: 6371e3, rho: 1.225, H: 8500},
  {k: "mars", t: "Марс", g: 3.71, R: 3390e3, rho: 0.020, H: 11100},
  {k: "t25", t: "Планета Т 2,5 g", g: 24.5, R: 10190e3, rho: 1.5, H: 3400},
  {k: "t27", t: "2,7 g", g: 26.5, R: 10830e3, rho: 1.6, H: 3150},
];
const MASSES = [
  {k: "full", t: "52,3 кт (гружёный)", m: 52.34e6},
  {k: "half", t: "33,6 кт", m: 33.6e6},
  {k: "empty", t: "14,9 кт (ловушки пусты)", m: 14.9e6},
];
const ROUTES = [
  {k: "tr", t: "Тритон → Земля, 29 а.е.", D: 4.35e12},
  {k: "jup", t: "Юпитер → Земля, 4,2 а.е.", D: 6.3e11},
  {k: "mars", t: "Марс → Земля, 0,52 а.е.", D: 7.8e10},
];
const MODES = [{k: "auto", t: "АВТО"}, {k: "argon", t: "АРГОН"}, {k: "iron", t: "ЖЕЛЕЗО"}, {k: "products", t: "ПРОДУКТЫ"}];
const TABS = [{k: "view", t: "Вид"}, {k: "mfd", t: "Экран установки"}, {k: "asc", t: "Взлёт"}, {k: "tr", t: "Перелёт"}, {k: "risk", t: "Режимы и риск"}, {k: "var", t: "Варианты"}];
const S = {variant: 2, mode: "auto", B: 12.1, P: 100, ve: 7000, prot: true, d: 15, life: 15, planet: PLANETS[0], mass: MASSES[0], pods: true, g: 5,
           route: ROUTES[0], vc: 1500, m0: 15, prop: 8, tab: "view", scene: "earth", od: false};
const chi = (st = S) => PM.VARIANTS[st.variant].chi;

// the cup as the ship runs it: reaction mass by environment (auto) or forced; the protection holds the stern's heat
function massFor(env, st = S) { return st.mode !== "auto" ? st.mode : env === "air" ? "argon" : env === "space" ? "iron" : "products"; }
function marchCup(mass, st = S, B = st.B, frac = st.P / 100, heatOk = true) {
  const o = {B, PfMax: PM.PF_MARCH * frac, mass, chi: chi(st), d: st.d, deployed: true, vEx: mass === "products" ? st.ve * 1e3 : undefined};
  return st.prot && !heatOk ? PM.cupHeatLimited(o) : st.prot ? PM.cupHeatLimited(o) : PM.cup(o);
}
function podCup(st = S) {
  return PM.cup({A: PM.A_POD, B: PM.B_NOM, PfMax: PM.PF_MARCH * PM.A_POD / PM.A_MARCH, mass: "argon", chi: chi(st), d: 6, deployed: true});
}

// ================= ascent =================
function ascent(st) {
  const P = st.planet, mu = P.g * P.R * P.R;
  let r = P.R, vr = 0, vt = 0, m = st.mass.m, argon = 6.2e6, iron = 3.8e6, fuel = 0, heat = 0, coil = 0, noQuench = 1, t = 0;
  const dt = 0.25, out = [], hTurn = 15 * P.H, hAtm = Math.max(7 * P.H, 20e3);
  let maxG = 0, odTime = 0, podsTime = 0, result = "", lifted = false, maxQ = 0;
  while (t < 1800) {
    const h = r - P.R, v = Math.hypot(vr, vt), rho = P.rho * Math.exp(-h / P.H), air = rho > 1e-5 && h < 30e3, gl = mu / (r * r);
    let mass = air && argon > 0 ? "argon" : iron > 0 ? "iron" : argon > 0 ? "argon" : null;
    if (!mass) { result = "кончилась рабочая масса"; break; }
    // protection: full power while the stern's heat store is below 90 %, then what it can shed
    const o = {B: st.B, PfMax: PM.PF_MARCH * st.P / 100, mass, chi: chi(st), d: st.d, deployed: true};
    let c = st.prot && heat > 0.9 * PM.STERN_STORE ? PM.cupHeatLimited(o) : PM.cup(o);
    let lim = c.lim;
    const podsOn = st.pods && v / 300 < 0.8 && h < 20e3 && rho > 1e-4;
    const cp = podsOn ? podCup(st) : null;
    let F = c.F + (cp ? PM.N_POD * cp.F : 0), mdot = c.mdot + (cp ? PM.N_POD * cp.mdot : 0);
    let qIn = c.heatIn, qOut = c.cool;
    let thr = 1;
    if (F / m > st.g * 9.81) { thr = st.g * 9.81 * m / F; lim = "перегрузка"; }
    F *= thr; mdot *= thr;
    heat = Math.max(0, heat + (qIn - qOut) * thr * dt);
    if (heat > PM.STERN_STORE) { result = "перегрев кормы"; break; }
    coil += PM.coilWearPerSec(st.B, st.life) * dt * thr;
    if (!isFinite(coil) || coil >= 1) { result = "обмотка катушки разрушена"; break; }
    noQuench *= 1 - PM.quenchPerMin(st.B) / 60 * dt * thr;
    if (st.B > PM.B_NOM + 0.05) odTime += dt * thr;
    if (podsOn) podsTime += dt;
    const E = 0.5 * v * v - mu / r, hA = r * vt, a = -mu / (2 * E), ecc = Math.sqrt(Math.max(0, 1 + 2 * E * hA * hA / (mu * mu)));
    const apo = E < 0 ? a * (1 + ecc) - P.R : Infinity, peri = E < 0 ? a * (1 - ecc) - P.R : -P.R;
    let pitch = h < 2000 || v < 150 ? Math.PI / 2 : Math.PI / 2 * (1 - Math.min(1, Math.pow((h - 2000) / hTurn, 0.6)));
    if (apo > hAtm * 1.3) pitch = Math.min(pitch, vr > 0 ? -0.05 : 0.1);
    const D = 0.5 * rho * v * v * 0.4 * 650;
    maxQ = Math.max(maxQ, 0.5 * rho * v * v);
    let ar = (F * Math.sin(pitch) - (v > 1e-6 ? D * vr / v : 0)) / m - gl + vt * vt / r;
    const at = (F * Math.cos(pitch) - (v > 1e-6 ? D * vt / v : 0)) / m - vr * vt / r;
    if (!lifted && ar < 0 && h < 1) { ar = 0; vr = 0; }
    if (ar > 0 || h > 1) lifted = true;
    vr += ar * dt; vt += at * dt; r += vr * dt;
    const dm = mdot * dt, df = (c.fuel + (cp ? PM.N_POD * cp.fuel : 0)) * thr * dt;
    if (mass === "argon") argon -= dm; else iron -= dm;
    m -= dm + df; fuel += df;
    maxG = Math.max(maxG, F / m / 9.81);
    t += dt;
    if (Math.round(t / dt) % 8 === 0) out.push({t, h: h / 1e3, vh: vt / 1e3, vv: vr / 1e3, vc: Math.sqrt(mu / r) / 1e3, tw: F / (m * gl), felt: F / m / 9.81, pods: podsOn, lim, heat: 100 * heat / PM.STERN_STORE, coil: 100 * coil, q: 100 * (1 - noQuench)});
    if (lifted && h < -1) { result = "падение"; break; }
    if (!lifted && t > 60) { result = "не отрывается от грунта"; break; }
    if (peri > hAtm && E < 0) { result = "орбита"; break; }
  }
  if (!result) result = "время вышло (30 мин)";
  const W = st.mass.m * P.g, c0 = marchCup("argon", st), cp0 = st.pods ? podCup(st) : null;
  return {out, result, t, argonUsed: 6.2e6 - Math.max(0, argon), ironUsed: 3.8e6 - Math.max(0, iron), fuel, maxG, odTime, podsTime, coil, heat, maxQ,
          pQuench: 1 - noQuench, tw: c0.F / W, twAll: (c0.F + (cp0 ? PM.N_POD * cp0.F : 0)) / W};
}

// ================= transfer =================
function transfer(st) {
  const D = st.route.D, vc = st.vc * 1e3;
  let m = st.m0 * 1e6, prop = st.prop * 1e6, x = 0, v = 0, t = 0, heat = 0, phase = "разгон", dv = 0, fuelUsed = 0;
  const o = {B: st.B, PfMax: PM.PF_MARCH * st.P / 100, mass: "products", chi: chi(st), d: st.d, deployed: true, vEx: st.ve * 1e3};
  const c = st.prot ? PM.cupHeatLimited(o) : PM.cup(o);
  const full = PM.cup(o);
  const out = [];
  let result = "";
  const dt0 = 600;
  while (t < 3.2e7) {
    const a = c.F / m;
    const stopDist = v * v / (2 * Math.max(a, 1e-12));
    let thrust = 0;
    // brake when the rest of the way is what the braking takes; coast once at the chosen speed
    if (phase !== "торможение" && x >= D - stopDist - v * dt0) phase = "торможение";
    else if (phase === "разгон" && v >= vc) phase = "полёт";
    if (phase === "разгон") thrust = 1; else if (phase === "торможение") thrust = -1;
    let dt = dt0;
    if (thrust !== 0) {
      if (prop <= 0) { result = thrust > 0 ? "кончилась масса на разгоне" : "нечем тормозить — пролёт"; break; }
      const dm = c.mdot * dt;
      prop -= dm; m -= dm; fuelUsed += c.fuel * dt; dv += a * dt;
      v += thrust * a * dt;
      if (!st.prot) { heat = Math.max(0, heat + (c.heatIn - c.cool) * dt); if (heat > PM.STERN_STORE) { result = "перегрев кормы"; break; } }
    }
    if (phase === "торможение" && v <= 0) { v = 0; result = "прибыл"; t += dt; break; }
    x += v * dt; t += dt;
    if (out.length === 0 || t - out[out.length - 1].t > 6 * 3600) out.push({t, v: v / 1e3, x: 100 * x / D});
    if (x >= D * 1.02) { result = "пролёт мимо"; break; }
  }
  if (!result) result = "больше года";
  return {out, result, t, dv, propUsed: st.prop * 1e6 - prop, fuelUsed, F: c.F, a0: c.F / (st.m0 * 1e6), heatLimited: c.lim === "тепло", powerFrac: c.Pf / full.Pf, c, full};
}

// ================= charts =================
function plot(id, out, series, opts = {}) {
  const c = $(id), dpr = window.devicePixelRatio || 1, w = c.clientWidth, h = c.clientHeight;
  if (!w) return;
  c.width = w * dpr; c.height = h * dpr;
  const g = c.getContext("2d"); g.scale(dpr, dpr); g.clearRect(0, 0, w, h);
  const L = 46, R = 8, T = 8, B = 20, pw = w - L - R, ph = h - T - B;
  const tEnd = out.length ? out[out.length - 1].t : 1;
  let ymin = opts.ymin ?? 0, ymax = opts.ymax ?? -Infinity;
  if (opts.ymax === undefined) for (const s of series) for (const p of out) { const v = s.f(p); if (isFinite(v)) ymax = Math.max(ymax, v); }
  if (!isFinite(ymax) || ymax <= ymin) ymax = ymin + 1;
  const X = t => L + pw * t / tEnd, Y = v => T + ph * (1 - (Math.min(ymax, Math.max(ymin, v)) - ymin) / (ymax - ymin));
  if (opts.limits) {
    const col = {"поле": "#fde2e2", "мощность": "#e1ecfa", "тепло": "#feebc8", "перегрузка": "#edf0f3"};
    for (let i = 1; i < out.length; i++) { g.fillStyle = col[out[i].lim] || "#fff"; g.fillRect(X(out[i - 1].t), T, X(out[i].t) - X(out[i - 1].t) + 1, ph); }
  }
  if (opts.pods) { g.fillStyle = "#d69e2e"; for (let i = 1; i < out.length; i++) if (out[i].pods) g.fillRect(X(out[i - 1].t), T + ph - 4, X(out[i].t) - X(out[i - 1].t) + 1, 4); }
  g.strokeStyle = "#e6eaef"; g.fillStyle = "#5d6878"; g.font = "11px system-ui"; g.lineWidth = 1;
  for (let i = 0; i <= 4; i++) { const v = ymin + (ymax - ymin) * i / 4; g.beginPath(); g.moveTo(L, Y(v)); g.lineTo(L + pw, Y(v)); g.stroke(); g.fillText(v >= 100 ? v.toFixed(0) : v.toFixed(1), 2, Y(v) + 4); }
  const unit = opts.days ? 86400 : 60, step = opts.days ? (tEnd > 120 * 86400 ? 30 : tEnd > 30 * 86400 ? 10 : 2) * 86400 : (tEnd > 600 ? 120 : tEnd > 240 ? 60 : 30);
  for (let tt = 0; tt <= tEnd; tt += step) g.fillText(fmt(tt / unit, tt / unit < 10 && !opts.days ? 1 : 0) + (opts.days ? " сут" : " мин"), X(tt) - 12, h - 5);
  for (const s of series) {
    g.strokeStyle = s.c; g.lineWidth = 1.6; g.setLineDash(s.dash || []); g.beginPath();
    out.forEach((p, i) => { const v = s.f(p); if (i) g.lineTo(X(p.t), Y(v)); else g.moveTo(X(p.t), Y(v)); });
    g.stroke(); g.setLineDash([]);
  }
}

// ================= MFD =================
const HIT = [];
function mfdState() {
  const env = S.scene === "earth" ? "air" : S.scene === "space" ? "space" : "products";
  const mass = massFor(env);
  const c = marchCup(mass);
  const raw = PM.cup({B: S.B, PfMax: PM.PF_MARCH * S.P / 100, mass, chi: chi(), d: S.d, deployed: true, vEx: mass === "products" ? S.ve * 1e3 : undefined});
  return {env, mass, c, raw};
}
function drawMFD() {
  const cv = $("mfd"), g = cv.getContext("2d"), W = 640;
  HIT.length = 0;
  const {mass, c, raw} = mfdState();
  const BG = "#0a1311", FR = "#1f3a35", TX = "#7fe0d0", DIM = "#4f8f86", OR = "#ee9a3a", YE = "#e8d35a", RD = "#ff5a4a";
  g.fillStyle = BG; g.fillRect(0, 0, W, W);
  g.strokeStyle = FR; g.lineWidth = 2; g.strokeRect(6, 6, W - 12, W - 12);
  const T = (s, x, y, col = TX, size = 15, al = "left", w = "500") => { g.fillStyle = col; g.font = `${w} ${size}px Segoe UI, system-ui`; g.textAlign = al; g.fillText(s, x, y); };
  const btn = (s, x, y, w, h, on, act, col = OR) => {
    g.fillStyle = on ? "#3b2a14" : "#121d1b"; g.fillRect(x, y, w, h); g.strokeStyle = on ? col : "#5a4325"; g.lineWidth = on ? 2 : 1; g.strokeRect(x, y, w, h);
    T(s, x + w / 2, y + h / 2 + 5, on ? "#ffd29a" : col, 14, "center", "600"); HIT.push({x, y, w, h, act});
  };
  const bar = (x, y, w, h, f, col, marks = []) => {
    g.fillStyle = "#14211e"; g.fillRect(x, y, w, h); g.fillStyle = col; g.fillRect(x, y, w * Math.max(0, Math.min(1, f)), h);
    g.strokeStyle = FR; g.lineWidth = 1; g.strokeRect(x, y, w, h);
    for (const m of marks) { g.strokeStyle = m.c; g.beginPath(); g.moveTo(x + w * m.f, y - 3); g.lineTo(x + w * m.f, y + h + 3); g.stroke(); }
  };
  T("СИЛОВАЯ · МАРШЕВАЯ ЧАША", 24, 38, TX, 20, "left", "700");
  T(PM.VARIANTS[S.variant].t, W - 24, 38, DIM, 12, "right");
  // modes
  MODES.forEach((m, i) => btn(m.t, 24 + i * 150, 56, 140, 32, S.mode === m.k, () => { S.mode = m.k; }));
  T(`рабочая масса: ${{argon: "аргон", iron: "железо", products: "продукты синтеза"}[mass]} · среда: ${{earth: "атмосфера, грунт", space: "космос", cruise: "перелёт"}[S.scene]}`, 24, 106, DIM, 13);
  // field gauge
  const Bmax = PM.B_RUPTURE, gx = 24, gy = 138, gw = 300, gh = 26;
  const zones = [[0, 12.1, "#2c7a5a"], [12.1, 14, "#9a8a2a"], [14, 16, "#b8691f"], [16, Bmax, "#a8322a"]];
  for (const [a, b, col] of zones) { g.fillStyle = col; g.fillRect(gx + gw * a / Bmax, gy, gw * (b - a) / Bmax, gh); }
  g.fillStyle = "#e8fffa"; g.fillRect(gx + gw * S.B / Bmax - 2, gy - 6, 4, gh + 12);
  T("ПОЛЕ", gx, gy - 8, TX, 13, "left", "600");
  T(`${fmt(S.B, 1)} Тл`, gx + gw, gy - 8, S.B > 16 ? RD : S.B > 12.15 ? YE : TX, 15, "right", "700");
  const press = S.B * S.B / (2 * PM.MU0) / 1e6, stress = (S.B / PM.B_NOM) ** 2;
  T(`давление ${fmt(press, 0)} МПа · напряжение обмотки ${fmt(stress * 100, 0)} %`, gx, gy + gh + 18, DIM, 12);
  btn("ПОЛЕ −", gx, gy + 56, 92, 32, false, () => setB(S.B - 0.1));
  btn("ПОЛЕ +", gx + 104, gy + 56, 92, 32, false, () => setB(S.B + 0.1));
  btn("ФОРСАЖ", gx + 208, gy + 56, 92, 32, S.od, () => { S.od = !S.od; if (!S.od && S.B > PM.B_NOM) S.B = PM.B_NOM; }, S.od ? RD : OR);
  // power
  const px = 340, pw2 = 276;
  T("МОЩНОСТЬ СИНТЕЗА", px, gy - 8, TX, 13, "left", "600");
  T(`${S.P} % · ${fW(c.Pf)}`, px + pw2, gy - 8, S.P > 100 ? YE : TX, 14, "right", "700");
  bar(px, gy, pw2, gh, c.Pf / (PM.PF_MARCH * 1.4), S.P > 100 ? "#b8691f" : "#2c7a5a", [{f: 1 / 1.4, c: "#e8fffa"}]);
  T(raw.Pf > c.Pf + 1 ? `урезано защитой до ${fP(c.Pf / raw.Pf)}` : `топливо p-¹¹B ${fM(c.fuel)}`, px, gy + gh + 18, raw.Pf > c.Pf + 1 ? OR : DIM, 12);
  btn("МОЩН −", px, gy + 56, 86, 32, false, () => setP(S.P - 5));
  btn("МОЩН +", px + 95, gy + 56, 86, 32, false, () => setP(S.P + 5));
  btn("ЗАЩИТА", px + 190, gy + 56, 86, 32, S.prot, () => { S.prot = !S.prot; }, S.prot ? "#5ad0a0" : RD);
  // thrust block
  const ty = 250;
  g.strokeStyle = FR; g.beginPath(); g.moveTo(20, ty - 14); g.lineTo(W - 20, ty - 14); g.stroke();
  T("ТЯГА", 24, ty + 8, TX, 13, "left", "600");
  T(fF(c.F), 24, ty + 40, "#e8fffa", 30, "left", "700");
  T(`предел поля ${fF(c.Ffield)}`, 24, ty + 62, DIM, 12);
  const limCol = c.lim === "поле" ? YE : c.lim === "тепло" ? RD : "#7fb8ff";
  T(`ограничивает: ${c.lim.toUpperCase()}`, 24, ty + 84, limCol, 14, "left", "700");
  bar(24, ty + 94, 280, 14, c.F / c.Ffield, "#3fa58a");
  const rows = [["струя", fV(c.v)], ["расход", fM(c.mdot)], ["мощность струи", fW(c.Pjet)], ["тяга/вес на Земле", fmt(c.F / (52.34e6 * 9.81), 2) + " (гружёный)"]];
  rows.forEach((r, i) => { T(r[0], 340, ty + 8 + i * 26, DIM, 13); T(r[1], W - 24, ty + 8 + i * 26, TX, 15, "right", "600"); });
  // heat
  const hy = 380;
  g.beginPath(); g.moveTo(20, hy - 14); g.lineTo(W - 20, hy - 14); g.stroke();
  T("ТЕПЛО КОРМЫ", 24, hy + 8, TX, 13, "left", "600");
  const qmax = Math.max(raw.heatIn, c.cool, 1);
  T("приход (излучение)", 24, hy + 32, DIM, 12); bar(150, hy + 22, 180, 12, raw.heatIn / qmax, "#c0532a"); T(fW(raw.heatIn), 340, hy + 32, TX, 13);
  T("отвод (масса+гребни)", 24, hy + 54, DIM, 12); bar(150, hy + 44, 180, 12, c.cool / qmax, "#2c7aa0"); T(fW(c.cool), 340, hy + 54, TX, 13);
  const net = raw.heatIn - c.cool;
  T(net > 0 ? (S.prot ? "защита держит баланс" : `запас кормы кончится через ${fmt(PM.STERN_STORE / net / 60, 1)} мин`) : "баланс в норме",
    24, hy + 78, net > 0 ? (S.prot ? OR : RD) : "#5ad0a0", 14, "left", "700");
  // risk
  const ry = 480;
  g.beginPath(); g.moveTo(20, ry - 14); g.lineTo(W - 20, ry - 14); g.stroke();
  T("РИСК", 24, ry + 8, TX, 13, "left", "600");
  const q = PM.quenchPerMin(S.B), mf = PM.misfirePerMin(S.P), wear = PM.coilWearPerSec(S.B, S.life);
  const risk = [["срыв поля, за минуту", fP(q), q > 0.01 ? RD : q > 1e-4 ? YE : TX],
                ["пропуск поджига, за минуту", fP(mf), mf > 0.01 ? RD : mf > 0 ? YE : TX],
                ["ресурс обмотки в этом режиме", wear > 0 ? (isFinite(wear) ? fmt(1 / wear / 60, 1) + " мин" : "РАЗРЫВ") : "без износа", wear > 0 ? (isFinite(wear) && 1 / wear > 600 ? YE : RD) : TX]];
  risk.forEach((r, i) => { T(r[0], 24, ry + 32 + i * 24, DIM, 13); T(r[1], 330, ry + 32 + i * 24, r[2], 15, "right", "700"); });
  // lamp + run button
  const danger = Math.max(q, mf, net > 0 && !S.prot ? 0.05 : 0, isFinite(wear) ? 0 : 1);
  g.fillStyle = danger > 0.01 ? RD : danger > 1e-4 ? YE : "#3fbf7f"; g.beginPath(); g.arc(380, ry + 50, 18, 0, 7); g.fill();
  T(danger > 0.01 ? "ОПАСНО" : danger > 1e-4 ? "ПОВЫШЕННЫЙ" : "НОРМА", 410, ry + 56, danger > 0.01 ? RD : danger > 1e-4 ? YE : "#5ad0a0", 16, "left", "700");
  btn("ПРОГОН 10 МИН", 380, ry + 82, 236, 34, false, () => monteCarlo());
  // scene switch (the environment the screen works in)
  [["ГРУНТ", "earth"], ["КОСМОС", "space"], ["ПЕРЕЛЁТ", "cruise"]].forEach(([s, k], i) => btn(s, 24 + i * 100, 596, 92, 28, S.scene === k, () => { S.scene = k; }, "#7fb8ff"));
  T(`${fmt(c.F / 1e6, 0)} МН · ${fV(c.v)} · ${c.lim}`, W - 24, 616, DIM, 12, "right");
}
function setB(b) { b = Math.round(Math.max(6, Math.min(PM.B_RUPTURE, b)) * 10) / 10; if (b > PM.B_NOM + 0.01 && !S.od) b = PM.B_NOM; S.B = b; }
function setP(p) { S.P = Math.max(5, Math.min(140, p)); }
function monteCarlo() {
  const {c, raw} = mfdState();
  const N = 2000, Tsec = 600;
  const q = PM.quenchPerMin(S.B) / 60, mf = PM.misfirePerMin(S.P) / 60;
  const net = raw.heatIn - c.cool, tHeat = !S.prot && net > 0 ? PM.STERN_STORE / net : Infinity;
  const wear = PM.coilWearPerSec(S.B, S.life), tRupt = wear > 0 ? 1 / wear : Infinity;
  let nQ = 0, nM = 0, nH = 0, nR = 0, cupHit = 0, firstSum = 0, clean = 0;
  for (let k = 0; k < N; k++) {
    let ev = null;
    for (let s = 0; s < Tsec; s++) {
      if (s >= tRupt) { ev = "R"; break; }
      if (s >= tHeat) { ev = "H"; break; }
      if (Math.random() < q) { ev = "Q"; break; }
      if (Math.random() < mf) { ev = "M"; break; }
    }
    if (!ev) { clean++; continue; }
    if (ev === "Q") { nQ++; if (Math.random() < 0.2 * S.P / 100) cupHit++; } else if (ev === "M") nM++; else if (ev === "H") nH++; else nR++;
  }
  $("mc").innerHTML = `Режим: поле ${fmt(S.B, 1)} Тл, мощность ${S.P} %, ${S.prot ? "защита по теплу включена" : "<b>без защиты</b>"}.<br>
    Без происшествий: <b>${fP(clean / N)}</b><br>
    Срыв поля (сверхпроводимость): ${fP(nQ / N)} — из них плазма ударила в чашу: ${fP(cupHit / N)}<br>
    Пропуск поджига: ${fP(nM / N)}<br>
    Перегрев кормы: ${fP(nH / N)}${isFinite(tHeat) ? ` (запас кончается на ${fmt(tHeat / 60, 1)} мин)` : ""}<br>
    Разрыв обмотки: ${fP(nR / N)}${isFinite(tRupt) ? ` (ресурс ${fmt(tRupt / 60, 1)} мин)` : ""}`;
}
$("mfd").addEventListener("click", e => {
  const r = $("mfd").getBoundingClientRect(), x = (e.clientX - r.left) * 640 / r.width, y = (e.clientY - r.top) * 640 / r.height;
  for (const h of HIT) if (x >= h.x && x <= h.x + h.w && y >= h.y && y <= h.y + h.h) { h.act(); syncSide(); refresh(); return; }
});

// ================= risks and balance =================
function riskTables() {
  let html = "<tr><th>Поле</th><th>Давление</th><th>Тяга маршевой</th><th>Прирост</th><th>Срыв поля / мин</th><th>Ресурс обмотки</th><th>Что это</th></tr>";
  const rowsB = [[12.1, "номинал — без износа, сколько угодно"], [13, "повышенное — тяжёлые взлёты, износ малый"], [14, "повышенное — заметный износ"],
    [15, "форсаж — минуты"], [16, "форсаж — предел расчёта (2,5 g)"], [16.5, "запредельный — отчаянный"], [17, "запредельный — почти наверняка срыв"], [17.1, "разрыв обмотки"]];
  for (const [b, t] of rowsB) {
    const F = PM.fieldThrust(b, PM.A_MARCH), w = PM.coilWearPerSec(b, S.life), q = PM.quenchPerMin(b);
    html += `<tr><td>${fmt(b, 1)} Тл</td><td class="v">${fmt(b * b / (2 * PM.MU0) / 1e6, 0)} МПа</td><td class="v">${fF(F)}</td><td class="v">+${fmt(((b / PM.B_NOM) ** 2 - 1) * 100, 0)} %</td>
      <td class="v ${q > 0.01 ? "bad" : q > 1e-4 ? "warn" : "ok"}">${fP(q)}</td><td class="v">${w > 0 ? (isFinite(w) ? fmt(1 / w / 60, 1) + " мин" : "—") : "∞"}</td><td>${t}</td></tr>`;
  }
  html += "<tr><th>Мощность</th><th></th><th colspan=2>Что даёт</th><th>Пропуск поджига / мин</th><th colspan=2>Что это</th></tr>";
  for (const [p, t] of [[100, "номинал"], [110, "перегрузка драйверов поджига"], [120, "перегрузка, тепло +20 %"], [130, "на износ"], [140, "аварийная"]]) {
    const mf = PM.misfirePerMin(p);
    html += `<tr><td>${p} %</td><td></td><td colspan=2>тяга +${p - 100} % там, где упирается в мощность (железо на быстрой струе, перелёт); в аргоне на поле не даёт ничего</td><td class="v ${mf > 0.01 ? "bad" : mf > 0 ? "warn" : "ok"}">${fP(mf)}</td><td colspan=2>${t}</td></tr>`;
  }
  $("riskT").innerHTML = html;
  const Ef = PM.fieldEnergy(PM.B_NOM);
  $("failT").innerHTML = `<tr><th>Отказ</th><th>Физика</th><th>Последствие в игре</th><th>Ремонт</th></tr>
    <tr><td><b>Срыв поля</b> (потеря сверхпроводимости)</td><td>Участок обмотки нагрелся выше критического — сопротивление, поле сбрасывается за ~1 с в защитные резисторы (энергия поля ≈ ${fmt(Ef / 1e9, 0)} ГДж при 12,1 Тл). Вероятность растёт с полем: запас по критическому току падает.</td>
      <td>Тяга маршевой — ноль мгновенно. Капсула в горловине без поля — плазма бьёт в чашу (шанс повреждения ∝ мощности, ~20 % на полной). На взлёте — падение, если гондолы не вытянут.</td><td>Охлаждение и перезапуск 10–30 мин; повреждённая чаша — тяга −30 % до станции.</td></tr>
    <tr><td><b>Разрыв обмотки</b></td><td>Напряжение ∝ B²: при 17,1 Тл вдвое выше расчётного — обмотка рвётся; раньше — по исчерпании ресурса усталости.</td>
      <td>Маршевая потеряна. Энергия поля — в обломки (повреждение кормы).</td><td>Только на станции (Тритон, Земля).</td></tr>
    <tr><td><b>Пропуск поджига</b></td><td>Драйверы поджига капсул выше номинала: часть капсул не вспыхивает.</td>
      <td>Рывки тяги, вибрация корпуса; серия пропусков — асимметрия, автомат глушит установку на 5 с.</td><td>Сам проходит при возврате к 100 %.</td></tr>
    <tr><td><b>Перегрев кормы</b></td><td>Проникающее излучение реакции × доля телесного угла кормы больше, чем уносит рабочая масса и сбрасывают гребни; запас ${fmt(PM.STERN_STORE / 1e12, 1)} ТДж (1 кт конструкции × 1400 К).</td>
      <td>При 100 % запаса — повреждение рубашки чаши и радиаторов (отвод тепла хуже до ремонта); при 150 % — авария, сброс поля.</td><td>Радиаторы — в полёте частично (экипаж), рубашка — станция.</td></tr>
    <tr><td><b>Отказ гондолы на старте</b></td><td>Любой из отказов выше в одной гондоле.</td><td>Опрокидывающий момент: автомат глушит противоположную, тяга −2 гондолы.</td><td>Перезапуск гондолы 2 мин.</td></tr>`;
  $("balance").innerHTML = `
    <p><b>Принцип.</b> Номинал (≤ 12,1 Тл, 100 %) — без износа и практически без риска: корабль ходит так годами. Всё, что выше, даёт реальный прирост (тяга ∝ B², в режимах на мощность — ∝ мощности) и платится ресурсом и шансом отказа. Числа отказов — предложение для баланса, опираются на физику: напряжение ∝ B², запас по критическому току, тепловой запас конструкции.</p>
    <p><b>Решение — за человеком.</b> Автомат держит номинал и тепло (кнопка «ЗАЩИТА»). Выйти за ограничитель (ФОРСАЖ) может только командир: экран показывает прирост, шанс отказа за минуту и оставшийся ресурс. Никаких скрытых бросков кубика: риск виден заранее.</p>
    <p><b>Где это нужно.</b> Гружёный взлёт с 2,5 g (16 Тл, ~12 мин — половина ресурса катушек и ~25 % шанс срыва за взлёт); уход от опасности; спасение при отказе гондолы. На лёгком корабле или на Земле форсаж не нужен — это и есть баланс.</p>
    <p><b>Отказы в игре.</b> Каждую секунду в режиме выше номинала — проверка с вероятностью из таблицы (λ/60). Ресурс обмотки копится и сохраняется в сценарии; восстанавливается только на станции. Срыв поля — не конец: перезапуск после охлаждения, но на взлёте это может стоить корабля.</p>
    <p><b>Экран установки</b> (как на вкладке): поле с зонами, давление и напряжение обмотки; мощность; тяга и что её ограничивает; тепло прихода и отвода; риск за минуту; лампа НОРМА/ПОВЫШЕННЫЙ/ОПАСНО.</p>`;
}

// ================= variants =================
function variantsTable() {
  let html = "<tr><th>Вариант</th><th>Взлёт с Земли</th><th>Гружёный с 2,5 g (16 Тл)</th><th>Железо в космосе, долго</th><th>Перелёт: тяга на 15 кт</th><th>Тритон → Земля</th></tr>";
  const verdicts = [];
  PM.VARIANTS.forEach((V, i) => {
    const st = {...S, variant: i, prot: true, B: 12.1, P: 100, pods: true, mass: MASSES[0], planet: PLANETS[0], g: 5};
    const e = ascent(st);
    const h = ascent({...st, planet: PLANETS[2], B: 16});
    const iron = PM.cupHeatLimited({B: 12.1, PfMax: PM.PF_MARCH, mass: "iron", chi: V.chi, d: S.d, deployed: true});
    const cr = PM.cupHeatLimited({B: 12.1, PfMax: PM.PF_MARCH, mass: "products", chi: V.chi, d: S.d, deployed: true, vEx: 7.0e6});
    const tr = transfer({...st, route: ROUTES[0], vc: 1500, m0: 15, prop: 8, ve: 7000});
    const cls = s => s === "орбита" || s === "прибыл" ? "ok" : "bad";
    html += `<tr><td><b>${V.t}</b><br><span class="note">χ = ${fP(V.chi)}</span></td>
      <td class="${cls(e.result)}">${e.result}${e.result === "орбита" ? ", " + fmt(e.t / 60, 1) + " мин" : ""}</td>
      <td class="${cls(h.result)}">${h.result}${h.result === "орбита" ? ", " + fmt(h.t / 60, 1) + " мин" : ""}</td>
      <td class="v">${fF(iron.F)} <span class="note">(${iron.lim})</span></td>
      <td class="v">${fF(cr.F)} · ${fmt(cr.F / 15e6, 3)} м/с²</td>
      <td class="${cls(tr.result)}">${tr.result === "прибыл" ? fmt(tr.t / 86400, 0) + " сут" : tr.result}</td></tr>`;
    verdicts.push({V, e, h, iron, cr, tr});
  });
  $("varT").innerHTML = html;
  $("varExpl").innerHTML = `
    <p><b>Реальный p-¹¹B</b> корабль такой мощности не вынес бы: проникающее излучение сжигает корму, защита урезает мощность до долей процента — не взлетает даже с Земли.</p>
    <p><b>Неравновесный</b> (лучшее, что предлагает сегодняшняя наука) — тоже нет: в 10 раз меньше излучения, но мощность 10¹⁴ Вт всё равно в сотни раз больше, чем может отвести корма.</p>
    <p><b>Канонический ионно-триггерный (χ 0,1 %)</b> — взлёт возможен: на старте рабочая масса (аргон, 30 т/с) уносит тепло, тяга упирается в поле. В космосе на железе — урезан теплом; перелёт на продуктах синтеза медленный: излучение уже не уносит рабочая масса, а гребни сбрасывают меньше гигаватта.</p>
    <p><b>«Чистый» ионно-триггерный (χ 0,001 %)</b> — всё работает: взлёт, форсаж на тяжёлой планете, перелёт Тритон–Земля на 1 500 км/с. Предел — только поле, мощность и катушки. Это и есть единственное допущение, которое надо принять, как анамезон: реакция, почти вся энергия которой уходит в заряженные частицы. Всё остальное — реальная физика.</p>
    <p>Конструкцию это не меняет. Менять пришлось бы только при варианте «канон»: для перелёта нужен теневой щит кормы или втрое больше радиаторов.</p>`;
}

// ================= side panel =================
function buttons(el, list, get, set, keyOf = x => x) {
  el.innerHTML = "";
  for (const it of list) { const b = document.createElement("button"); b.textContent = it.t; if (get() === keyOf(it)) b.classList.add("on"); b.onclick = () => { set(keyOf(it)); syncSide(); refresh(); }; el.appendChild(b); }
}
function syncSide() {
  buttons($("varB"), PM.VARIANTS.map((v, i) => ({t: v.t, i})), () => S.variant, v => S.variant = v, x => x.i);
  $("varD").textContent = PM.VARIANTS[S.variant].d;
  buttons($("modeB"), MODES, () => S.mode, v => S.mode = v, x => x.k);
  buttons($("planetB"), PLANETS, () => S.planet, v => S.planet = v);
  buttons($("massB"), MASSES, () => S.mass, v => S.mass = v);
  buttons($("routeB"), ROUTES, () => S.route, v => S.route = v);
  buttons($("tabsB"), TABS, () => S.tab, v => S.tab = v, x => x.k);
  $("sB").value = S.B; $("sP").value = S.P; $("sVe").value = S.ve; $("sProt").checked = S.prot; $("sD").value = S.d; $("sLife").value = S.life;
  $("sPods").checked = S.pods; $("sG").value = S.g; $("sVc").value = S.vc; $("sM0").value = S.m0; $("sProp").value = S.prop;
  $("sBV").textContent = fmt(S.B, 1) + " Тл"; $("sPV").textContent = S.P + " %"; $("sVeV").textContent = fmt(S.ve, 0) + " км/с";
  $("sDV").textContent = S.d + " м"; $("sLifeV").textContent = S.life + " мин"; $("sGV").textContent = S.g + " g";
  $("sVcV").textContent = fmt(S.vc, 0) + " км/с"; $("sM0V").textContent = fmt(S.m0, 1) + " кт"; $("sPropV").textContent = fmt(S.prop, 1) + " кт";
  for (const t of TABS) $("t_" + t.k).classList.toggle("on", S.tab === t.k);
}
const SL = [["sB", v => { S.B = v; if (v > PM.B_NOM + 0.01) S.od = true; }], ["sP", v => S.P = v], ["sVe", v => S.ve = v], ["sD", v => S.d = v], ["sLife", v => S.life = v],
            ["sG", v => S.g = v], ["sVc", v => S.vc = v], ["sM0", v => S.m0 = v], ["sProp", v => S.prop = v]];
for (const [id, f] of SL) $(id).addEventListener("input", e => { f(+e.target.value); syncSide(); refresh(); });
$("sProt").addEventListener("change", e => { S.prot = e.target.checked; refresh(); });
$("sPods").addEventListener("change", e => { S.pods = e.target.checked; refresh(); });

function refresh() {
  if (S.tab === "mfd") drawMFD();
  if (S.tab === "asc") {
    const r = ascent(S);
    $("ascVerdict").innerHTML = r.result === "орбита" ? `<span class="ok">Орбита за ${fmt(r.t / 60, 1)} мин</span>` : `<span class="bad">Не выходит: ${r.result}</span>`;
    $("ascSum").innerHTML = `
      <tr><td>Тяга/вес на старте: маршевая / с гондолами</td><td class="v ${r.tw < 1 ? "bad" : "ok"}">${fmt(r.tw, 2)} / ${fmt(r.twAll, 2)}</td></tr>
      <tr><td>Аргон / железо / топливо p-¹¹B</td><td class="v">${fmt(r.argonUsed / 1e6, 2)} кт / ${fmt(r.ironUsed / 1e6, 2)} кт / ${fmt(r.fuel / 1e3, 1)} т</td></tr>
      <tr><td>Перегрузка, макс.</td><td class="v">${fmt(r.maxG, 1)} g</td></tr>
      <tr><td>Гондолы / форсаж</td><td class="v">${fmt(r.podsTime, 0)} с / ${fmt(r.odTime / 60, 1)} мин</td></tr>
      <tr><td>Ресурс обмотки израсходован</td><td class="v ${r.coil > 0.5 ? "bad" : r.coil > 0.1 ? "warn" : "ok"}">${fP(r.coil)}</td></tr>
      <tr><td>Шанс срыва поля за взлёт</td><td class="v ${r.pQuench > 0.1 ? "bad" : r.pQuench > 0.01 ? "warn" : "ok"}">${fP(r.pQuench)}</td></tr>
      <tr><td>Тепловой запас кормы в конце</td><td class="v">${fP(r.heat / PM.STERN_STORE)}</td></tr>
      <tr><td>Скоростной напор, макс.</td><td class="v">${fmt(r.maxQ / 1e3, 0)} кПа</td></tr>`;
    plot("cH", r.out, [{f: p => p.h, c: "#2b6cb0"}, {f: p => p.vh * 10, c: "#c05621"}, {f: p => p.vv * 10, c: "#2f855a"}, {f: p => p.vc * 10, c: "#9aa3ad", dash: [4, 3]}]);
    plot("cT", r.out, [{f: p => p.tw, c: "#2d3748"}, {f: p => p.felt, c: "#805ad5"}], {limits: true, pods: true});
    plot("cR", r.out, [{f: p => p.heat, c: "#dd6b20"}, {f: p => p.coil, c: "#c53030"}, {f: p => p.q, c: "#c53030", dash: [4, 3]}], {ymax: 100});
  }
  if (S.tab === "tr") {
    const r = transfer(S);
    $("trVerdict").innerHTML = r.result === "прибыл" ? `<span class="ok">${S.route.t}: ${fmt(r.t / 86400, 1)} сут</span>` : `<span class="bad">${S.route.t}: ${r.result}</span>`;
    $("trSum").innerHTML = `
      <tr><td>Тяга / ускорение в начале</td><td class="v">${fF(r.F)} / ${fmt(r.a0, 3)} м/с²</td></tr>
      <tr><td>Мощность синтеза</td><td class="v ${r.heatLimited ? "warn" : "ok"}">${fW(r.c.Pf)}${r.heatLimited ? ` — урезано теплом до ${fP(r.powerFrac)}` : ""}</td></tr>
      <tr><td>Струя / расход</td><td class="v">${fV(r.c.v)} / ${fM(r.c.mdot)}</td></tr>
      <tr><td>Тепло: приход / отвод</td><td class="v">${fW(r.c.heatIn)} / ${fW(r.c.cool)}</td></tr>
      <tr><td>Δv затрачено</td><td class="v">${fmt(r.dv / 1e3, 0)} км/с</td></tr>
      <tr><td>Масса израсходована (из неё топливо p-¹¹B)</td><td class="v">${fmt(r.propUsed / 1e6, 2)} кт (${fmt(r.fuelUsed / 1e6, 2)} кт)</td></tr>
      <tr><td>Канон: обычный путь Нептун — Земля</td><td class="v">72 дня (≈ 700 км/с в среднем)</td></tr>`;
    plot("cV", r.out, [{f: p => p.v, c: "#2b6cb0"}, {f: p => p.x * Math.max(1, S.vc) / 100, c: "#dd6b20", dash: [4, 3]}], {days: true});
  }
  if (S.tab === "risk") riskTables();
  if (S.tab === "var") variantsTable();
  if (S.tab === "view") updateView();
}

// ================= 3D view =================
let R3 = null;
function b64f(s) { const b = atob(s), u = new Uint8Array(b.length); for (let i = 0; i < b.length; i++) u[i] = b.charCodeAt(i); return u.buffer; }
function glowTex(stops) {
  const c = document.createElement("canvas"); c.width = c.height = 128; const g = c.getContext("2d");
  const gr = g.createRadialGradient(64, 64, 0, 64, 64, 64); for (const [o, col] of stops) gr.addColorStop(o, col);
  g.fillStyle = gr; g.fillRect(0, 0, 128, 128); return new THREE.CanvasTexture(c);
}
function init3D() {
  if (R3) return;
  const box = $("view3d");
  const renderer = new THREE.WebGLRenderer({antialias: true, preserveDrawingBuffer: true}); renderer.setPixelRatio(Math.min(2, window.devicePixelRatio));
  box.insertBefore(renderer.domElement, box.firstChild);
  const scene = new THREE.Scene();
  const cam = new THREE.PerspectiveCamera(40, 1, 0.5, 20000), ctl = new THREE.OrbitControls(cam, renderer.domElement);
  const hemi = new THREE.HemisphereLight(0xffffff, 0x8a7f6a, 0.8); scene.add(hemi);
  const sun = new THREE.DirectionalLight(0xffffff, 0.75); sun.position.set(300, 400, 200); scene.add(sun);
  const shipG = new THREE.Group(), inner = new THREE.Group(); shipG.add(inner); scene.add(shipG);
  const MESH = {}, BASE = {};
  for (const p of T9.parts) {
    const v = new Float32Array(b64f(p.v)), idx = new Uint32Array(b64f(p.i));
    const g = new THREE.BufferGeometry(); g.setAttribute("position", new THREE.BufferAttribute(v.slice(), 3)); g.setIndex(new THREE.BufferAttribute(idx, 1)); g.computeVertexNormals();
    const m = new THREE.Mesh(g, new THREE.MeshLambertMaterial({color: new THREE.Color(...p.color), side: THREE.DoubleSide}));
    m.frustumCulled = false; inner.add(m); MESH[p.name] = m; BASE[p.name] = v;
  }
  // rig (the same math as gen_mesh.comp_matrices / t9_view.html)
  const AS = {};
  function pose(extra) {
    for (const a of T9.anims) AS[a] = 0; for (const c of T9.comps) AS[c.anim] = c.d;
    Object.assign(AS, T9.poses[0].states, extra);
    const M = new Array(T9.comps.length);
    const rotM = (ax, ang) => new THREE.Matrix4().makeRotationAxis(new THREE.Vector3(...ax).normalize(), ang);
    const mat = i => {
      if (M[i]) return M[i];
      const c = T9.comps[i], fr = x => Math.min(1, Math.max(0, (x - c.s0) / (c.s1 - c.s0))), f = fr(AS[c.anim]) - fr(c.d);
      let T = new THREE.Matrix4();
      if (c.kind === "rot") { const Rm = rotM(c.axis, c.ang * f), ref = new THREE.Vector3(...c.ref), r2 = ref.clone().applyMatrix4(new THREE.Matrix4().extractRotation(Rm)); T = Rm; T.setPosition(ref.clone().sub(r2)); }
      else if (c.kind === "tr") T.makeTranslation(c.shift[0] * f, c.shift[1] * f, c.shift[2] * f);
      else { const sc = c.scale.map(s => 1 + (s - 1) * f), ref = new THREE.Vector3(...c.ref); T.makeScale(sc[0], sc[1], sc[2]); T.setPosition(ref.x * (1 - sc[0]), ref.y * (1 - sc[1]), ref.z * (1 - sc[2])); }
      M[i] = c.parent === null || c.parent === undefined || c.parent < 0 ? T : mat(c.parent).clone().multiply(T);
      return M[i];
    };
    T9.comps.forEach((c, i) => { const Mi = mat(i); for (const gname of c.groups) { const m = MESH[gname]; if (!m) continue; const pos = m.geometry.attributes.position.array, base = BASE[gname], v = new THREE.Vector3(); for (let k = 0; k < base.length; k += 3) { v.set(base[k], base[k + 1], base[k + 2]).applyMatrix4(Mi); pos[k] = v.x; pos[k + 1] = v.y; pos[k + 2] = v.z; } m.geometry.attributes.position.needsUpdate = true; m.geometry.computeVertexNormals(); m.geometry.computeBoundingBox(); } });
  }
  // ground, sky, stars
  const ground = new THREE.Mesh(new THREE.CircleGeometry(6000, 64), new THREE.MeshLambertMaterial({color: 0xcdb98f})); ground.rotation.x = -Math.PI / 2; scene.add(ground);
  const pad = new THREE.Mesh(new THREE.CircleGeometry(60, 48), new THREE.MeshLambertMaterial({color: 0x8a8070})); pad.rotation.x = -Math.PI / 2; pad.position.y = 0.05; scene.add(pad);
  const starG = new THREE.BufferGeometry(), sp = [];
  for (let i = 0; i < 2500; i++) { const u = Math.random() * 2 - 1, th = Math.random() * 2 * Math.PI, r = 9000; sp.push(r * Math.sqrt(1 - u * u) * Math.cos(th), r * u, r * Math.sqrt(1 - u * u) * Math.sin(th)); }
  starG.setAttribute("position", new THREE.Float32BufferAttribute(sp, 3));
  const stars = new THREE.Points(starG, new THREE.PointsMaterial({color: 0xffffff, size: 1.6, sizeAttenuation: false})); scene.add(stars);
  // effects in the ship frame (z along the hull, the stern at -60; the march cup at y 1.82)
  const fx = new THREE.Group(); inner.add(fx);
  const CUP = new THREE.Vector3(0, 1.82, -60.6);
  const add = (mesh) => { fx.add(mesh); return mesh; };
  const addMat = (col, op) => new THREE.MeshBasicMaterial({color: col, transparent: true, opacity: op, blending: THREE.AdditiveBlending, depthWrite: false, side: THREE.DoubleSide});
  // plume: stacked open cones along -z
  function plume(nSeg) {
    const g = new THREE.Group(); g.rotation.x = -Math.PI / 2; const segs = [];
    for (let k = 0; k < nSeg; k++) { const m = new THREE.Mesh(new THREE.CylinderGeometry(1, 1, 1, 28, 1, true), addMat(0xffffff, 0.2)); g.add(m); segs.push(m); }
    return {g, segs};
  }
  function setPlume(P, o) {   // o: start (Vector3), len, r0, spread, colA, colB, op
    P.g.position.copy(o.start); const n = P.segs.length;
    P.segs.forEach((m, k) => {
      const a = k / n, b = (k + 1) / n, ra = o.r0 + o.spread * a * o.len, rb = o.r0 + o.spread * b * o.len;
      m.geometry.dispose(); m.geometry = new THREE.CylinderGeometry(ra, rb, o.len / n, 28, 1, true);
      m.position.y = o.len * (a + b) / 2;
      m.material.color.copy(o.colA.clone().lerp(o.colB, a)); m.material.opacity = o.op * Math.pow(1 - a, 1.4);
    });
    P.g.visible = o.op > 0.001;
  }
  const core = plume(10), sheath = plume(12), podPl = [0, 1, 2, 3].map(() => ({core: plume(6), sheath: plume(8)}));
  [core, sheath].forEach(p => add(p.g)); podPl.forEach(p => { add(p.core.g); add(p.sheath.g); });
  const diamonds = []; for (let k = 0; k < 6; k++) { const m = add(new THREE.Mesh(new THREE.SphereGeometry(1, 16, 10), addMat(0xffffff, 0.5))); diamonds.push(m); }
  const flash = new THREE.Sprite(new THREE.SpriteMaterial({map: glowTex([[0, "rgba(255,255,255,1)"], [0.2, "rgba(200,220,255,0.8)"], [1, "rgba(120,140,255,0)"]]), blending: THREE.AdditiveBlending, depthWrite: false, transparent: true}));
  add(flash);
  const xray = new THREE.Sprite(new THREE.SpriteMaterial({map: glowTex([[0, "rgba(220,160,255,0.6)"], [1, "rgba(120,60,255,0)"]]), blending: THREE.AdditiveBlending, depthWrite: false, transparent: true}));
  add(xray);
  const pellets = []; for (let k = 0; k < 8; k++) { const m = add(new THREE.Mesh(new THREE.SphereGeometry(0.25, 8, 6), new THREE.MeshBasicMaterial({color: 0xfff2c0}))); pellets.push(m); }
  // field lines: the magnetic nozzle diverging aft of the cup
  const field = []; for (let k = 0; k < 16; k++) {
    const pts = [], ph = k / 16 * Math.PI * 2;
    for (let s = -3; s <= 70; s += 1.5) { const rr = 2.2 * (s < 0 ? 1 + 0.15 * s * s / 9 : 1 + Math.pow(s / 14, 1.35) * 0.9); pts.push(new THREE.Vector3(CUP.x + rr * Math.cos(ph), CUP.y + rr * Math.sin(ph), CUP.z - s)); }
    const l = new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), new THREE.LineBasicMaterial({color: 0x5ff0ff, transparent: true, opacity: 0.4, blending: THREE.AdditiveBlending, depthWrite: false}));
    fx.add(l); field.push(l);
  }
  // dust: world-space particles on the ground
  const dustN = 600, dustG = new THREE.BufferGeometry(), dustP = new Float32Array(dustN * 3), dustV = [];
  for (let i = 0; i < dustN; i++) dustV.push({a: Math.random() * Math.PI * 2, r: Math.random() * 40, h: Math.random() * 8, s: 10 + Math.random() * 40});
  dustG.setAttribute("position", new THREE.BufferAttribute(dustP, 3));
  const dust = new THREE.Points(dustG, new THREE.PointsMaterial({map: glowTex([[0, "rgba(190,170,130,0.9)"], [1, "rgba(190,170,130,0)"]]), size: 14, transparent: true, depthWrite: false, opacity: 0.6}));
  scene.add(dust);
  // the jet spreading over the ground while the stern is low
  const splash = new THREE.Mesh(new THREE.CircleGeometry(1, 48), new THREE.MeshBasicMaterial({map: glowTex([[0, "rgba(255,250,235,1)"], [0.35, "rgba(255,170,80,0.8)"], [1, "rgba(255,120,40,0)"]]), transparent: true, blending: THREE.AdditiveBlending, depthWrite: false}));
  splash.rotation.x = -Math.PI / 2; splash.position.y = 0.3; scene.add(splash);
  R3 = {renderer, scene, cam, ctl, hemi, sun, shipG, inner, MESH, pose, ground, pad, stars, core, sheath, podPl, diamonds, flash, xray, pellets, field, dust, dustV, dustP, CUP, setPlume, splash, t0: performance.now(), scene_: null, alt: 0, vUp: 0};
  // view buttons
  const vb = $("vbar");
  [["Земля: взлёт (аргон, гондолы)", "earth"], ["Космос: железо", "space"], ["Перелёт: продукты синтеза", "cruise"]].forEach(([t, k]) => {
    const b = document.createElement("button"); b.textContent = t; b.onclick = () => { S.scene = k; R3.scene_ = null; syncView(); refresh(); }; vb.appendChild(b);
  });
  window.addEventListener("resize", resize3D);
  resize3D();
  setInterval(tick3D, 33);
}
function syncView() { [...$("vbar").children].forEach((b, i) => b.classList.toggle("on", ["earth", "space", "cruise"][i] === S.scene)); }
function resize3D() { if (!R3) return; const box = $("view3d"), w = box.clientWidth, h = box.clientHeight; R3.renderer.setSize(w, h); R3.cam.aspect = w / h; R3.cam.updateProjectionMatrix(); }
function setupScene() {
  const k = S.scene;
  if (R3.scene_ === k) return;
  R3.scene_ = k;
  const earth = k === "earth";
  // wings and fin open (radiators); pods out on the ground (swivel aft: the ship stands), in the bays in space
  R3.pose({crest_lateral: 0, wing_outer: 0, crest_dorsal: 0, pod_retract: earth ? 0 : 1, pod_swivel: 0, march_slide: 1, iris_march: 1});
  R3.ground.visible = R3.pad.visible = R3.dust.visible = R3.splash.visible = earth; R3.stars.visible = !earth;
  R3.scene.background = new THREE.Color(earth ? 0x8fb8e0 : 0x02030a);
  R3.scene.fog = earth ? new THREE.Fog(0x8fb8e0, 1500, 7000) : null;
  R3.hemi.intensity = earth ? 0.8 : 0.35;
  R3.shipG.rotation.set(earth ? -Math.PI / 2 : 0, 0, 0);
  R3.alt = 0; R3.vUp = 0; R3.t0 = performance.now();
  // pod cup positions (the aft end of each pod)
  R3.podPos = [0, 1, 2, 3].map(i => { const bb = R3.MESH["pod_" + i].geometry.boundingBox; return new THREE.Vector3((bb.min.x + bb.max.x) / 2, (bb.min.y + bb.max.y) / 2, bb.min.z); });
  if (earth) { R3.cam.position.set(480, 90, 560); R3.ctl.target.set(0, 40, 0); }
  else { R3.cam.position.set(520, 160, -40); R3.ctl.target.set(0, 0, -200); }
  R3.ctl.update();
}
function viewState() {
  const env = S.scene === "earth" ? "air" : S.scene === "space" ? "space" : "products";
  const mass = massFor(env), c = marchCup(mass);
  const raw = PM.cup({B: S.B, PfMax: PM.PF_MARCH * S.P / 100, mass, chi: chi(), d: S.d, deployed: true, vEx: mass === "products" ? S.ve * 1e3 : undefined});
  const cp = S.scene === "earth" && S.pods ? podCup() : null;
  return {env, mass, c, raw, cp};
}
function updateView() { init3D(); syncView(); setupScene(); }
function tick3D() {
  if (!R3 || S.tab !== "view") return;
  setupScene();
  const t = (performance.now() - R3.t0) / 1000, fl = 0.85 + 0.15 * Math.sin(t * 37) * Math.sin(t * 13.3);
  const {mass, c, cp} = viewState();
  const earth = S.scene === "earth";
  // the ship rises on the ground scene: T/W from the cups against the chosen planet and mass
  if (earth) {
    const W = S.mass.m * S.planet.g, F = c.F + (cp && R3.alt < 2000 ? PM.N_POD * cp.F : 0), a = Math.max(0, F / W - 1) * S.planet.g;
    if (t > 2) { R3.vUp = F > W ? R3.vUp + a * 0.033 : 0; R3.alt += R3.vUp * 0.033; }
    if (R3.alt > 1500) { R3.alt = 0; R3.vUp = 0; R3.t0 = performance.now(); }
    R3.shipG.position.set(0, 60.6 + 22.5 + R3.alt, 0);
    R3.ctl.target.y += (40 + R3.alt * 0.9 - R3.ctl.target.y) * 0.1; R3.cam.position.y += (90 + R3.alt * 0.9 - R3.cam.position.y) * 0.1;
  } else R3.shipG.position.set(0, 0, 0);
  const Fn = c.F / 886e6, Bq = (S.B / PM.B_NOM) ** 2;
  const burn = new THREE.Vector3(R3.CUP.x, R3.CUP.y, R3.CUP.z - S.d);
  const ARc = [new THREE.Color(0xffffff), new THREE.Color(0xff9a40)], FE = [new THREE.Color(0xd8e4ff), new THREE.Color(0x6a40ff)], PR = [new THREE.Color(0xe8d8ff), new THREE.Color(0x8060ff)];
  const cols = mass === "argon" ? ARc : mass === "iron" ? FE : PR;
  const thick = mass === "argon" ? 1 : mass === "iron" ? 0.45 : 0.15;
  const len = mass === "argon" ? 120 + 160 * Fn : mass === "iron" ? 260 + 300 * Fn : 900;
  const op = Math.min(1, (mass === "products" ? 0.3 : earth ? 0.95 : 0.6) * Math.sqrt(Math.max(0.02, Fn)) * fl);
  R3.setPlume(R3.core, {start: burn, len: len * 0.7, r0: 1.2 * thick + 0.3, spread: 0.02 * thick + 0.003, colA: cols[0], colB: cols[1], op: Math.min(1, op * 1.6)});
  R3.setPlume(R3.sheath, {start: burn, len, r0: 2.4 * thick + 0.5, spread: (mass === "argon" ? 0.09 : 0.03) * thick + 0.004, colA: cols[1], colB: cols[1], op: op * 0.8});
  R3.diamonds.forEach((m, k) => { const on = mass === "argon"; m.visible = on; if (!on) return; m.position.set(burn.x, burn.y, burn.z - 14 - k * 16); const s = (1.6 - k * 0.15) * (0.8 + 0.4 * Math.random()); m.scale.set(s, s, s * 2.2); m.material.opacity = 0.5 * Fn * (1 - k / 7); });
  R3.flash.position.copy(burn); const fs = (6 + 10 * Math.sqrt(c.Pf / PM.PF_MARCH)) * (0.6 + 0.8 * Math.random()); R3.flash.scale.set(fs, fs, 1);
  R3.xray.position.copy(burn); const xs = 40 * Math.sqrt(chi() / 0.35) * Math.sqrt(c.Pf / PM.PF_MARCH) + 1; R3.xray.scale.set(xs, xs, 1); R3.xray.material.opacity = Math.min(0.8, 0.15 + chi() * 2);
  R3.pellets.forEach((m, k) => { const u = ((t * 3 + k / R3.pellets.length) % 1); m.position.set(R3.CUP.x, R3.CUP.y, R3.CUP.z + 1 - u * (S.d + 1)); });
  R3.field.forEach(l => { l.material.opacity = Math.min(0.9, 0.12 + 0.35 * Bq) * (S.B > 16 ? 0.7 + 0.3 * Math.random() : 1); l.material.color.set(S.B > 16 ? 0xff7060 : S.B > PM.B_NOM + 0.05 ? 0xffe070 : 0x5ff0ff); });
  // pods
  R3.podPl.forEach((p, i) => {
    const on = !!cp && R3.alt < 2000;
    if (!on) { p.core.g.visible = p.sheath.g.visible = false; return; }
    const st = R3.podPos[i].clone().add(new THREE.Vector3(0, 0, -4));
    R3.setPlume(p.core, {start: st, len: 50, r0: 0.6, spread: 0.02, colA: ARc[0], colB: ARc[1], op: 0.7 * fl});
    R3.setPlume(p.sheath, {start: st, len: 80, r0: 1.0, spread: 0.08, colA: ARc[1], colB: ARc[1], op: 0.4 * fl});
  });
  // dust under the stern: the jet on the ground
  if (earth) {
    const hStern = 22.5 + R3.alt, inten = Math.max(0, 1 - hStern / 400) * Fn;
    R3.dust.material.opacity = 0.7 * inten;
    const sr = (40 + 140 * inten) * (0.9 + 0.2 * Math.random()); R3.splash.scale.set(sr, sr, 1); R3.splash.material.opacity = Math.min(1, 1.2 * inten);
    R3.dustV.forEach((d, i) => { d.r += d.s * 0.033 * (0.5 + inten); if (d.r > 260) { d.r = Math.random() * 20; } R3.dustP[i * 3] = Math.cos(d.a) * d.r; R3.dustP[i * 3 + 1] = (2 + d.h) * (0.5 + d.r / 70); R3.dustP[i * 3 + 2] = Math.sin(d.a) * d.r; });
    R3.dust.geometry.attributes.position.needsUpdate = true;
  }
  const W = S.mass.m * S.planet.g;
  $("vhud").innerHTML = `<b>${PM.VARIANTS[S.variant].t}</b><br>
    ${{argon: "аргон", iron: "железо", products: "продукты синтеза"}[mass]} · поле ${fmt(S.B, 1)} Тл · мощность ${S.P} %<br>
    тяга маршевой ${fF(c.F)} (ограничивает: ${c.lim})${cp ? ` + гондолы ${fF(PM.N_POD * cp.F)}` : ""} · струя ${fV(c.v)} · расход ${fM(c.mdot)}<br>
    ${earth ? `${S.planet.t}, ${S.mass.t}: тяга/вес ${fmt((c.F + (cp ? PM.N_POD * cp.F : 0)) / W, 2)}${(c.F + (cp ? PM.N_POD * cp.F : 0)) < W ? " — <b>не отрывается</b>" : ` · высота ${fmt(R3.alt, 0)} м`}<br>` : ""}
    синтез ${fW(c.Pf)} · излучение на корму ${fW(c.heatIn)} / отвод ${fW(c.cool)}`;
  R3.ctl.update();
  R3.renderer.render(R3.scene, R3.cam);
}

syncSide();
refresh();
