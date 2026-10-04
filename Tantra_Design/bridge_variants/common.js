// The shared parts of the desk variants (Tantra_Design/tantra_bridge_3d_preview.html). Orbiter coordinates (x right, y up,
// z forward) go to three.js through V3; the desk's polar frame through P3(a, r, y) round the console axis (0, 79.75).
// THE GEOMETRY (the seat mesh and the seated pose, see the note of the page): the seat cushion's top is 0.635 m over the floor,
// the armrests' top 0.765 m (x ±0.32…0.40, z 78.81…79.17 at rest); the hip joint sits at the seat's place (z 78.95 at rest,
// +0.6 travel, +adj −0.10…+0.20), the knees 0.37–0.40 ahead of it, the shoulders 0.45 over it and 0.10 behind. From the
// shoulder the fingertips reach 0.68 m (5 % woman) … 0.85 m (95 % man): at the desk top (0.45 m under the shoulder) that is
// 0.51…0.72 m ahead, i.e. z ≤ 80.0–80.3, while the desk's edge is at r 0.92 (z 80.67). So nothing on the desk is pressed:
// the desk is for looking (instruments, lamps, the machine's screen), all the input rides with the seat or stands beside it.
const TB = (() => {
  const T = THREE, D2R = Math.PI / 180;
  // ---- materials ----
  function grain(base, amp, blot) {   // a fine grain, or a hammer-tone (blotches) for the military enamel
    const cv = document.createElement("canvas"); cv.width = cv.height = 256; const c = cv.getContext("2d"), im = c.createImageData(256, 256);
    for (let i = 0; i < 65536; i++) { const n = base + (Math.random() - .5) * amp; im.data[i * 4] = im.data[i * 4 + 1] = im.data[i * 4 + 2] = n; im.data[i * 4 + 3] = 255; }
    c.putImageData(im, 0, 0);
    if (blot) for (let i = 0; i < 900; i++) { const x = Math.random() * 256, y = Math.random() * 256, r = 2 + Math.random() * 7; c.fillStyle = `rgba(${Math.random() < .5 ? "255,255,255" : "0,0,0"},${.03 + Math.random() * .05})`; c.beginPath(); c.arc(x, y, r, 0, 7); c.fill(); }
    const t = new T.CanvasTexture(cv); t.wrapS = t.wrapT = T.RepeatWrapping; return t;
  }
  const std = (color, roughness, metalness, map, extra = {}) => new T.MeshStandardMaterial({color, roughness, metalness, map: map || null, side: T.DoubleSide, ...extra});
  function M(kind) {
    const hammer = grain(205, 26, true), fine = grain(232, 14, false);
    const base = {rubber: std(0x15181a, .9, 0), grip: std(0x24282b, .7, .05), dark: std(0x161a1d, .5, .3), glassRim: std(0x9aa6ae, .25, .8)};
    if (kind === "mal") {
      const cv = document.createElement("canvas"); cv.width = cv.height = 256; const c = cv.getContext("2d"), im = c.createImageData(256, 256);
      for (let i = 0; i < 65536; i++) { const x = i % 256, y = Math.floor(i / 256), r = Math.hypot(x - 90, y - 140) + 9 * Math.sin(x * .045) + 6 * Math.sin(y * .07 + x * .02);
        const b = .5 + .5 * Math.sin(r * .9), n = 196 + 22 * b + (Math.random() - .5) * 10; im.data[i * 4] = im.data[i * 4 + 1] = im.data[i * 4 + 2] = n; im.data[i * 4 + 3] = 255; }
      c.putImageData(im, 0, 0); const mt = new T.CanvasTexture(cv); mt.wrapS = mt.wrapT = T.RepeatWrapping;
      return {...base, kind, desk: std(0x2f7560, .42, .45, mt), panel: std(0x275f4f, .45, .4, mt), trim: std(0xa3b8ae, .3, .75), key: std(0x1d2a26, .5, .2), band: "mal"};
    }
    if (kind === "metal") return {...base, kind, desk: std(0x56616a, .55, .35, hammer), panel: std(0x4b545c, .5, .35, hammer), trim: std(0x9aa3aa, .3, .75), key: std(0x2b3136, .55, .2), band: "metal"};
    if (kind === "aes") return {...base, kind, desk: std(0xd6d9d2, .5, .04, fine), panel: std(0xe4e6e0, .45, .03, fine), trim: std(0x8f979c, .3, .7), key: std(0xe9eae4, .5, .02), band: "aes"};
    if (kind === "two") return {...base, kind, desk: std(0xd9dbd5, .5, .04, fine), panel: std(0x5a646c, .5, .4, hammer), trim: std(0x9aa3aa, .3, .75), key: std(0x2b3136, .55, .2), band: "metal"};
    return {...base, kind: "dark", desk: std(0x45413c, .8, .08, deskTex), panel: std(0x2c2f32, .6, .2), trim: std(0x8a96a4, .3, .7), key: keySide, band: "dark"};
  }
  // ---- the desk: three bodies (left, front, right), each may carry a sunken sloped band (a0..a1, [r0, r1, depth]) ----
  const BAND = [.99, 1.19, .07];
  function desk(ctx, Mt, bands = {L: BAND, F: BAND, R: BAND}) {
    const {DESK, hsF0, YD, RG} = ctx, ri = DESK.ri, ro = DESK.ro, top = YD, gi = RG - .035, go = RG + .035;
    const sec = band => {
      const S = [[[ri + .10, 0], [ri + .10, .09]], [[ri + .10, .09], [ri, .24]], [[ri, .24], [ri, top - .03]], arcPts(ri + .03, top - .03, .03, 180, 90)];
      if (band) { const [b0, b1, dp] = band; S.push([[ri + .03, top], [b0, top]], [[b0, top], [b0, top - dp]], [[b0, top - dp], [b1, top]], [[b1, top], [gi, top]]); }
      else S.push([[ri + .03, top], [gi, top]]);
      S.push([[gi, top], [gi, top - .06]], [[gi, top - .06], [go, top - .06]], [[go, top - .06], [go, top]], [[go, top], [ro - .03, top]], arcPts(ro - .03, top - .03, .03, 90, 0), [[ro, top - .03], [ro, 0]], [[ro, 0], [ri + .10, 0]]);
      return S;
    };
    for (const [a0, a1, b] of [[DESK.a0, -hsF0, bands.L], [-hsF0, hsF0, bands.F], [hsF0, DESK.a1, bands.R]]) ctx.group.add(new T.Mesh(arcBody(sec(b), a0, a1, 60), Mt.desk));
    return [Mt.desk];
  }
  // a canvas on the sloped band (its face lit by itself: the photonic image or the lamps behind the glass)
  function band(ctx, a0, a1, b = BAND, w = 2400, h = 400) {
    const [b0, b1, dp] = b, y0 = ctx.FLOOR + ctx.YD - dp, y1 = ctx.FLOOR + ctx.YD, L = Math.hypot(b1 - b0, dp), nr = -dp / L * .002, ny = (b1 - b0) / L * .002;
    const {cv, tex} = canvasTex(w, h), NU = 48, pos = [], uv = [], idx = [];
    for (let j = 0; j <= 1; j++) for (let i = 0; i <= NU; i++) { const a = a0 + (a1 - a0) * i / NU, q = P3(a, (j ? b1 : b0) + nr, (j ? y1 : y0) + ny); pos.push(q.x, q.y, q.z); uv.push(i / NU, j); }
    for (let i = 0; i < NU; i++) { const a = i, bb = i + 1, c = i + NU + 1, d = c + 1; idx.push(a, c, bb, bb, c, d); }
    const g = new T.BufferGeometry(); g.setAttribute("position", new T.Float32BufferAttribute(pos, 3)); g.setAttribute("uv", new T.Float32BufferAttribute(uv, 2)); g.setIndex(idx);
    const mesh = new T.Mesh(g, new T.MeshBasicMaterial({map: tex, side: T.DoubleSide})); ctx.group.add(mesh);
    return {mesh, cv, tex, c: cv.getContext("2d"), w, h, a0, a1, X: a => (a - a0) / (a1 - a0) * w};
  }
  // ---- drawing as light: the instruments ----
  const GOST = (c, px, w = 700) => { c.font = `${w} ${px}px Segoe UI`; };
  function txt(c, s, x, y, col, px, al = "center", w = 700) { GOST(c, px, w); c.fillStyle = col; c.textAlign = al; c.textBaseline = "middle"; c.fillText(s, x, y); }
  const look = st => st === "mal" ? {bg: "#163a30", win: "#020b08", lab: "#a6d8c3", bez: "#6a9a8a", grid: "#21463b"} : st === "aes" ? {bg: "#d3d6cf", win: "#06100e", lab: "#2a3036", bez: "#9aa1a6", grid: "#b9bdb5"} : st === "dark" ? {bg: "#15191c", win: "#04090a", lab: "#8a9aa4", bez: "#3a4248", grid: "#20262a"} : {bg: "#2a3137", win: "#05090b", lab: "#c4ccd2", bez: "#8e979e", grid: "#353d44"};
  function bezel(c, x, y, R, L) { const g = c.createRadialGradient(x - R * .3, y - R * .3, R * .2, x, y, R * 1.12); g.addColorStop(0, L.bez); g.addColorStop(1, "#1a1e21"); c.fillStyle = g; c.beginPath(); c.arc(x, y, R * 1.12, 0, 7); c.fill(); c.fillStyle = L.win; c.beginPath(); c.arc(x, y, R, 0, 7); c.fill(); }
  function adi(c, x, y, R, P, L, t) {
    bezel(c, x, y, R, L);
    const pitch = 4 + 2 * Math.sin(t * .2), bank = 8 * Math.sin(t * .13);
    c.save(); c.beginPath(); c.arc(x, y, R * .96, 0, 7); c.clip(); c.translate(x, y); c.rotate(-bank * D2R);
    c.strokeStyle = P.main; c.lineWidth = 3; c.beginPath(); c.moveTo(-R * 1.2, pitch * R / 40); c.lineTo(R * 1.2, pitch * R / 40); c.stroke();
    c.lineWidth = 2; for (let p = -20; p <= 20; p += 10) { if (!p) continue; const yy = (pitch - p) * R / 40; c.beginPath(); c.moveTo(-R * .3, yy); c.lineTo(R * .3, yy); c.stroke(); txt(c, String(Math.abs(p)), -R * .45, yy, P.main, R * .13); txt(c, String(Math.abs(p)), R * .45, yy, P.main, R * .13); }
    c.restore();
    c.strokeStyle = P.acc; c.lineWidth = 5; c.beginPath(); c.moveTo(x - R * .55, y); c.lineTo(x - R * .18, y); c.lineTo(x - R * .1, y + R * .1); c.moveTo(x + R * .55, y); c.lineTo(x + R * .18, y); c.lineTo(x + R * .1, y + R * .1); c.stroke();
    c.strokeStyle = P.main; c.lineWidth = 2; for (let b = -60; b <= 60; b += 15) { const a = (b - 90) * D2R; c.beginPath(); c.moveTo(x + R * .9 * Math.cos(a), y + R * .9 * Math.sin(a)); c.lineTo(x + R * (b % 30 ? .84 : .78) * Math.cos(a), y + R * (b % 30 ? .84 : .78) * Math.sin(a)); c.stroke(); }
    txt(c, "АВИАГОРИЗОНТ", x, y + R * 1.3, L.lab, R * .14);
  }
  function round(c, x, y, R, label, val, f, P, L, warn = .85) {   // a round gauge: the scale and the needle are light
    bezel(c, x, y, R, L);
    const a0 = 135 * D2R, a1 = 405 * D2R, fa = a0 + (a1 - a0) * Math.max(0, Math.min(1, f));
    c.lineWidth = R * .06; c.strokeStyle = P.dim; c.beginPath(); c.arc(x, y, R * .82, a0, a1); c.stroke();
    c.strokeStyle = P.red; c.beginPath(); c.arc(x, y, R * .82, a0 + (a1 - a0) * warn, a1); c.stroke();
    c.strokeStyle = f > warn ? P.red : P.main; c.lineWidth = 4; c.beginPath(); c.moveTo(x, y); c.lineTo(x + R * .74 * Math.cos(fa), y + R * .74 * Math.sin(fa)); c.stroke();
    txt(c, val, x, y + R * .45, f > warn ? P.red : P.main, R * .26);
    txt(c, label, x, y + R * 1.3, L.lab, R * .16);
  }
  function bars(c, x, y, w, h, labels, fs, vals, P, L, title) {   // the chambers' temperatures: light-guide columns, the limit line
    c.fillStyle = L.win; c.fillRect(x - 14, y - 14, w + 28, h + 28);
    const n = labels.length, bw = w / n * .45;
    labels.forEach((lb, i) => { const bx = x + (i + .5) * w / n - bw / 2, f = fs[i];
      c.strokeStyle = P.dim; c.lineWidth = 2; c.strokeRect(bx, y, bw, h);
      c.fillStyle = f > .9 ? P.red : f > .8 ? P.acc : P.main; c.globalAlpha = .9; c.fillRect(bx + 4, y + h - (h - 8) * f - 4, bw - 8, (h - 8) * f); c.globalAlpha = 1;
      c.strokeStyle = P.red; c.beginPath(); c.moveTo(bx - 6, y + h * .1); c.lineTo(bx + bw + 6, y + h * .1); c.stroke();
      txt(c, lb, bx + bw / 2, y + h + 26, L.lab, 22); txt(c, vals[i], bx + bw / 2, y - 30, P.main, 20); });
    txt(c, title, x + w / 2, y + h + 58, L.lab, 22);
  }
  function lampTile(c, x, y, w, h, label, state, P, L, st) {   // an indicator: its lens lit by the state (0 dark, 1 normal, 2 attention, 3 alarm)
    c.fillStyle = st === "aes" ? "#e9ebe5" : "#0d1215"; c.fillRect(x, y, w, h); c.strokeStyle = L.grid; c.lineWidth = 2; c.strokeRect(x, y, w, h);
    const col = [st === "aes" ? "#9aa1a0" : "#1f2a2e", P.main, P.acc, P.red][state];
    c.fillStyle = col; c.globalAlpha = state ? .95 : 1; c.fillRect(x + 8, y + 8, w - 16, h * .42); c.globalAlpha = 1;
    txt(c, label, x + w / 2, y + h * .74, st === "aes" ? "#1d2226" : L.lab, Math.min(24, h * .2));
  }
  // the front band: the flight instruments and the chambers (under the front glass, where the keys were)
  function drawFront(B, P, st, t) {
    const c = B.c, W = B.w, H = B.h, L = look(st);
    c.fillStyle = L.bg; c.fillRect(0, 0, W, H);
    const alt = 2.49 + .01 * Math.sin(t * .2), vs = 11.7 + 2 * Math.sin(t * .3), spd = 212 + 4 * Math.sin(t * .17);
    round(c, 230, 175, 120, "СКОРОСТЬ м/с", String(Math.round(spd)), spd / 400, P, L);
    round(c, 530, 175, 120, "ВЫСОТА км", alt.toFixed(2).replace(".", ","), alt / 10, P, L);
    round(c, 830, 175, 120, "ВЕРТ. СКОР. м/с", "+" + vs.toFixed(1).replace(".", ","), .5 + vs / 60, P, L);
    adi(c, W / 2, 180, 150, P, L, t);
    const T = [2480, 2510, 2460, 2495].map((v, i) => v + 20 * Math.sin(t * .3 + i));
    bars(c, 1500, 70, 330, 200, ["К1", "К2", "К3", "К4"], T.map(v => v / 3000), T.map(v => Math.round(v) + " К"), P, L, "ТЕМПЕРАТУРА КАМЕР");
    round(c, 1990, 175, 110, "ПОЛЕ Тл", "38,2", .62, P, L);
    const thr = window.THR ? window.THR.cur : .46 + .02 * Math.sin(t * .3); round(c, 2250, 175, 110, "ТЯГА %", String(Math.round(thr * 100)), thr, P, L);
    B.tex.needsUpdate = true;
  }
  // the left band: the state of the mechanisation (lamps; the keys to call its page ride with the seat)
  function drawLeft(B, P, st, t) {
    const c = B.c, W = B.w, H = B.h, L = look(st);
    c.fillStyle = L.bg; c.fillRect(0, 0, W, H);
    const items = [["ЛАФЕТ", 1], ["ЛАПЫ", 1], ["ШАССИ", 1], ["КРЫЛЬЯ", 1], ["ГОНДОЛЫ", 0], ["СОПЛА", 1], ["АНГАР", 0], ["ПОРТ", 0], ["ШЛЮЗ", 2], ["ЛИФТ", 0], ["СТОЛ", 0], ["ОБШИВКА", 1]];
    txt(c, "МЕХАНИЗАЦИЯ · СОСТОЯНИЕ", 300, 40, L.lab, 26, "left");
    const cols = 6, tw = (W - 400) / cols;
    items.forEach(([n, s], i) => lampTile(c, 300 + (i % cols) * tw + 8, 80 + Math.floor(i / cols) * 150, tw - 16, 130, n, s === 2 && Math.sin(t * 6) < 0 ? 1 : s, P, L, st));
    B.tex.needsUpdate = true;
  }
  // the right band: the computing machine's own phosphor screen (it stands in the desk right under it), its store, the plant
  const PHOS = "#4dff88";
  function drawRight(B, P, st, t, calc) {
    const c = B.c, W = B.w, H = B.h, L = look(st), x1 = 1050;
    c.fillStyle = L.bg; c.fillRect(0, 0, W, H);
    c.fillStyle = "#020803"; c.fillRect(40, 40, x1 - 80, H - 80); c.strokeStyle = L.bez; c.lineWidth = 6; c.strokeRect(40, 40, x1 - 80, H - 80);
    c.save(); c.shadowColor = PHOS; c.shadowBlur = 14;   // the phosphor: its own glow, not the photonic palette
    txt(c, calc.disp, x1 - 80, 150, PHOS, 96, "right", 700);
    calc.log.slice(0, 3).forEach((l, i) => txt(c, l, 80, 230 + i * 40, "#2fbf62", 28, "left", 600));
    c.restore();
    txt(c, "РАСЧЁТНАЯ МАШИНА · ОПТИЧЕСКАЯ", 60, H - 18, L.lab, 22, "left");
    txt(c, "ХРАНЕНИЕ", x1 + 130, 60, L.lab, 24);
    for (let i = 0; i < 8; i++) lampTile(c, x1 + 30 + (i % 4) * 52, 90 + Math.floor(i / 4) * 92, 46, 80, String(i + 1), calc.cells[i] ? 1 : 0, P, L, st);
    const items = [["ЭНЕРГОУСТ.", "412 МВт", .55], ["ПОЛЕ", "38,2 Тл", .62], ["ЛОВУШКИ", "НОРМА", .3], ["ЖИЗНЕОБЕСП.", "НОРМА", .2]];
    items.forEach(([n, v, f], i) => { const x = 1420 + i * 240; round(c, x + 100, 170, 90, n, v, f + .03 * Math.sin(t * .4 + i), P, L); });
    B.tex.needsUpdate = true;
  }
  // the machine itself: a big optical computing block in the desk under its screen (seen through the desk cut away)
  function machine(ctx, a0, a1) {
    const g = new T.Group(); ctx.group.add(g);
    const y0 = ctx.FLOOR + .12, y1 = ctx.FLOOR + ctx.YD - .09;
    g.add(new T.Mesh(arcBox(a0, a1, 1.0, 1.36, y0, y1, 16), std(0x1b2226, .4, .6)));
    const lm = new T.LineBasicMaterial({color: 0x4dff88, transparent: true, opacity: .55});
    for (let k = 0; k < 7; k++) { const pts = []; for (let i = 0; i <= 24; i++) pts.push(P3(a0 + (a1 - a0) * i / 24, 1.0 - .004, y0 + .06 + k * (y1 - y0 - .1) / 6 + .015 * Math.sin(i * .9 + k))); g.add(new T.Line(new T.BufferGeometry().setFromPoints(pts), lm)); }
    return g;
  }
  // ---- planar keys (on the pods and the consoles): a box, its top a canvas ----
  const HALO = (() => { const cv = document.createElement("canvas"); cv.width = cv.height = 128; const c = cv.getContext("2d"), g = c.createRadialGradient(64, 64, 8, 64, 64, 64);
    g.addColorStop(0, "rgba(255,255,255,.9)"); g.addColorStop(.55, "rgba(255,255,255,.32)"); g.addColorStop(1, "rgba(255,255,255,0)"); c.fillStyle = g; c.fillRect(0, 0, 128, 128); return new T.CanvasTexture(cv); })();
  function pkey(parent, x, y, z, w, d, label, group, col, o = {}) {
    const cw = 256, ch = Math.max(64, Math.round(256 * d / w)), cv = document.createElement("canvas"); cv.width = cw; cv.height = ch;
    const tex = new T.CanvasTexture(cv); tex.anisotropy = 8; const h = o.h || .012;
    // a glass key emits its own light (no shading); its sides glow as the edges of a light guide
    const top = o.glass ? new T.MeshBasicMaterial({map: tex}) : new T.MeshLambertMaterial({map: tex, emissive: 0xffffff, emissiveMap: tex, emissiveIntensity: o.glow !== undefined ? o.glow : .45});
    const side = o.glass ? new T.MeshBasicMaterial({color: 0x16404f}) : o.side || keySide;
    const mesh = new T.Mesh(new T.BoxGeometry(w, h, d), [side, side, top, side, side, side]);
    mesh.position.set(x, y + h / 2, z); parent.add(mesh);
    const k = {mesh, cv, tex, on: false, group, label, col, y0: mesh.position.y, paint: o.paint, style: o.style, state: o.state || 0, topMat: top, sideMat: o.glass ? side : null};
    if (o.glass) {   // the light it spills on the panel round it (only while it is on)
      const halo = new T.Mesh(new T.PlaneGeometry(w * 1.9, d * 2.2), new T.MeshBasicMaterial({map: HALO, color: 0xb8ecff, transparent: true, opacity: .5, blending: T.AdditiveBlending, depthWrite: false}));
      halo.rotation.x = -Math.PI / 2; halo.position.set(x, y + .0012, z); halo.visible = false; parent.add(halo); k.halo = halo;
    }
    KEYS.push(k); if (BT) BT.keys.push(k); mesh.userData.key = k; paintKey(k); return k;
  }
  const hexA = (hex, a) => { const n = parseInt(hex.slice(1), 16); return `rgba(${n >> 16},${n >> 8 & 255},${n & 255},${a})`; };
  const ICONS = new Set(["ПРОГРАД", "РЕТРОГРАД", "НОРМ. +", "НОРМ. −", "РАД. +", "РАД. −", "ГОРИЗОНТ", "СТОП ВРАЩ.", "ВЫСОТА"]);
  function paintGlass(k, c, W, H, P, col) {
    const lc = k.col ? col : P.main, on = k.on;
    if (on) { const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, hexA(P.white, 1)); g.addColorStop(.4, hexA(lc, .96)); g.addColorStop(1, hexA(lc, .78)); c.fillStyle = g; c.fillRect(0, 0, W, H); }
    else { c.fillStyle = "#051017"; c.fillRect(0, 0, W, H); const g = c.createRadialGradient(W / 2, H * .45, 4, W / 2, H / 2, W * .62); g.addColorStop(0, hexA(lc, .22)); g.addColorStop(1, hexA(lc, .06)); c.fillStyle = g; c.fillRect(0, 0, W, H); }
    c.strokeStyle = on ? hexA(P.white, .95) : hexA(lc, .5); c.lineWidth = on ? 5 : 3; c.strokeRect(4, 4, W - 8, H - 8);   // the lit edge of the glass
    if (on) { c.strokeStyle = "rgba(3,20,28,.5)"; c.lineWidth = 2; c.strokeRect(14, 14, W - 28, H - 28); }              // the helmet's double contour
    const fg = on ? "#03141c" : lc;
    c.save(); if (!on) { c.shadowColor = lc; c.shadowBlur = 12; }
    if (ICONS.has(k.label)) { c.strokeStyle = fg; c.lineWidth = Math.max(3, H * .04); icon(c, k.label, W / 2, H * .4, H * .17); txt(c, k.label, W / 2, H * .82, fg, H * .15); }
    else keyLabel(k, c, W, H, fg, 1);
    c.restore();
    if (k.sideMat) k.sideMat.color.set(on ? lc : "#16404f");
    if (k.halo) { k.halo.visible = on; k.halo.material.color.set(lc); }
  }
  // a grid of keys on a plate (local coords of the plate: x across, z toward him = +z three), rows from the far side
  function grid(parent, x0, z0, cols, w, d, px, pz, items, o = {}) {
    const ks = []; items.forEach((it, i) => { if (!it) return; const c = i % cols, r = Math.floor(i / cols); ks.push(pkey(parent, x0 + c * px, o.y || 0, z0 + r * pz, w, d, it[0], it[1], it[2], o)); }); return ks;
  }
  // the key styles: a retro cap with an engraved label; a lamp-key; a photonic glass orientation key with its symbol
  function paintCap(k, c, W, H, P, col) {
    const aes = k.style === "aes"; c.fillStyle = aes ? (k.on ? "#cfd3cb" : "#ecede7") : (k.on ? "#3a4248" : "#262c31"); c.fillRect(0, 0, W, H);
    const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, "rgba(255,255,255,.10)"); g.addColorStop(1, "rgba(0,0,0,.18)"); c.fillStyle = g; c.fillRect(0, 0, W, H);
    keyLabel(k, c, W, H, k.col ? col : aes ? "#15191c" : "#e3e8eb", 1);
  }
  function paintLampKey(k, c, W, H, P, col) {
    const aes = k.style === "aes"; c.fillStyle = aes ? "#e9eae4" : "#22282d"; c.fillRect(0, 0, W, H);
    const lit = k.on ? P.acc : [aes ? "#a9b0ae" : "#1c272b", P.main, P.acc, P.red][k.state];
    c.fillStyle = lit; c.globalAlpha = k.on || k.state ? .95 : 1; c.fillRect(W * .1, H * .1, W * .8, H * .38); c.globalAlpha = 1;
    const kk = {...k, label: k.label}; c.save(); c.translate(0, H * .2); keyLabel(kk, c, W, H * .8, aes ? "#15191c" : "#dfe6ea", 1); c.restore();
  }
  function icon(c, name, x, y, s) {
    c.beginPath();
    const circ = () => { c.moveTo(x + s, y); c.arc(x, y, s, 0, 7); };
    if (name === "ПРОГРАД") { circ(); c.moveTo(x, y - s); c.lineTo(x, y - s * 1.7); c.moveTo(x - s, y); c.lineTo(x - s * 1.7, y); c.moveTo(x + s, y); c.lineTo(x + s * 1.7, y); }
    else if (name === "РЕТРОГРАД") { circ(); const d = s * .7; c.moveTo(x - d, y - d); c.lineTo(x + d, y + d); c.moveTo(x + d, y - d); c.lineTo(x - d, y + d); }
    else if (name === "НОРМ. +") { c.moveTo(x, y - s); c.lineTo(x + s, y + s * .7); c.lineTo(x - s, y + s * .7); c.closePath(); }
    else if (name === "НОРМ. −") { c.moveTo(x, y + s); c.lineTo(x + s, y - s * .7); c.lineTo(x - s, y - s * .7); c.closePath(); }
    else if (name === "РАД. +") { circ(); for (const a of [45, 135, 225, 315]) { const r = a * D2R; c.moveTo(x + s * Math.cos(r), y + s * Math.sin(r)); c.lineTo(x + s * 1.6 * Math.cos(r), y + s * 1.6 * Math.sin(r)); } }
    else if (name === "РАД. −") { circ(); for (const a of [45, 135, 225, 315]) { const r = a * D2R; c.moveTo(x + s * Math.cos(r), y + s * Math.sin(r)); c.lineTo(x + s * .45 * Math.cos(r), y + s * .45 * Math.sin(r)); } }
    else if (name === "ГОРИЗОНТ") { c.moveTo(x - s * 1.6, y); c.lineTo(x + s * 1.6, y); c.moveTo(x - s * .7, y + s * .6); c.lineTo(x, y); c.lineTo(x + s * .7, y + s * .6); }
    else if (name === "СТОП ВРАЩ.") { c.arc(x, y, s, -.3, 4.9); c.moveTo(x - s * 1.3, y - s * 1.3); c.lineTo(x + s * 1.3, y + s * 1.3); }
    else if (name === "ВЫСОТА") { c.moveTo(x, y + s * 1.2); c.lineTo(x, y - s * 1.2); c.moveTo(x - s * .7, y - s * .5); c.lineTo(x, y - s * 1.2); c.lineTo(x + s * .7, y - s * .5); c.moveTo(x - s * 1.3, y + s * 1.2); c.lineTo(x + s * 1.3, y + s * 1.2); }
    else { c.moveTo(x - s, y - s); c.lineTo(x + s, y - s); c.lineTo(x + s, y + s); c.lineTo(x - s, y + s); c.closePath(); }
    c.stroke();
  }
  function paintOrient(k, c, W, H, P, col) {   // dark glass; the symbol and the label are light; «on» as the palette says
    const g = c.createLinearGradient(0, 0, 0, H); g.addColorStop(0, "#0e171b"); g.addColorStop(1, "#071013"); c.fillStyle = g; c.fillRect(0, 0, W, H);
    const fg = P.fill && k.on ? "#000" : k.on ? (k.col ? col : P.acc) : (k.col ? col : P.main);
    if (k.on) { if (P.fill) { c.fillStyle = k.col ? col : P.acc; c.fillRect(6, 6, W - 12, H - 12); } else { c.strokeStyle = fg; c.lineWidth = 5; c.strokeRect(7, 7, W - 14, H - 14); c.lineWidth = 2; c.strokeRect(17, 17, W - 34, H - 34); } }
    else { c.strokeStyle = P.dim; c.globalAlpha = .6; c.lineWidth = 2; c.strokeRect(7, 7, W - 14, H - 14); c.globalAlpha = 1; }
    c.strokeStyle = fg; c.lineWidth = Math.max(3, H * .035); icon(c, k.label, W / 2, H * .4, H * .17);
    txt(c, k.label, W / 2, H * .82, fg, H * .15);
  }
  // ---- the hand controllers ----
  function ruo(parent, x, y, z, Mt, style = "pistol") {   // the orientation controller (right hand): a boot, a stalk, the grip
    const g = new T.Group(); g.position.copy(V3(x, y, z)); parent.add(g);
    const boot = new T.Mesh(new T.CylinderGeometry(.03, .038, .03, 20), Mt.rubber); boot.position.y = .015; g.add(boot);
    if (style === "ball") { const b = new T.Mesh(new T.SphereGeometry(.036, 24, 16), Mt.grip); b.position.y = .085; g.add(b); const cap = new T.Mesh(new T.CylinderGeometry(.022, .022, .006, 20), Mt.trim); cap.position.y = .121; g.add(cap); return g; }
    const st = new T.Mesh(new T.CylinderGeometry(.01, .01, .04, 10), Mt.trim); st.position.y = .05; g.add(st);
    const grip = new T.Mesh(new T.CylinderGeometry(.019, .023, .11, 18), Mt.grip); grip.position.set(0, .12, -.008); grip.rotation.x = -.21; g.add(grip);
    const head = new T.Mesh(new T.SphereGeometry(.024, 16, 12), Mt.grip); head.position.set(0, .178, -.02); g.add(head);
    const trig = new T.Mesh(new T.BoxGeometry(.012, .02, .012), Mt.trim); trig.position.set(0, .135, -.034); g.add(trig);
    return g;
  }
  function rud(parent, x, y, z, Mt, style = "t") {   // the translation controller (left hand): a T-handle or a mushroom
    const g = new T.Group(); g.position.copy(V3(x, y, z)); parent.add(g);
    const boot = new T.Mesh(new T.CylinderGeometry(.03, .038, .03, 20), Mt.rubber); boot.position.y = .015; g.add(boot);
    const st = new T.Mesh(new T.CylinderGeometry(.01, .01, .06, 10), Mt.trim); st.position.y = .06; g.add(st);
    if (style === "mush") { const m = new T.Mesh(new T.CylinderGeometry(.036, .03, .026, 24), Mt.grip); m.position.y = .1; g.add(m); return g; }
    const bar = new T.Mesh(new T.CylinderGeometry(.014, .014, .10, 14), Mt.grip); bar.rotation.z = Math.PI / 2; bar.position.y = .094; g.add(bar);
    for (const s of [-1, 1]) { const e = new T.Mesh(new T.SphereGeometry(.017, 12, 8), Mt.grip); e.position.set(s * .05, .094, 0); g.add(e); }
    return g;
  }
  // ---- the seat pods: plates over the armrests, riding with the seat (built at its rest place) ----
  // the armrest: x ±0.32…0.40, z 78.81…79.17, top 1.765; the pod: x ±0.30…±xo, z 78.80…79.38, top 1.79
  function pod(ctx, sx, Mt, xo = .64) {
    const top = 1.79, z0 = 78.80, z1 = 79.38, g = new T.Group(); ctx.seatGroup.add(g);
    const w = xo - .30, body = new T.Mesh(new T.BoxGeometry(w, .05, z1 - z0), Mt.panel); body.position.copy(V3(sx * (.30 + w / 2), top - .025, (z0 + z1) / 2)); g.add(body);
    const rim = new T.Mesh(new T.BoxGeometry(w + .01, .012, .012), Mt.trim); rim.position.copy(V3(sx * (.30 + w / 2), top - .006, z1 + .006)); g.add(rim);
    // the plate frame for the keys: origin at (x, top, z) of the pod, three axes (x right, z toward him)
    const plate = new T.Group(); plate.position.copy(V3(0, top, 0)); g.add(plate);
    return {g, plate, top, z0, z1, X: x => x, Z: z => -z};
  }
  // ---- the machine's arithmetic, the store, the system keys ----
  function behave(ctx, extra) {
    const calc = {disp: "0", acc: null, op: null, fresh: true, cells: ["9 260", "43,4", "", "", "", "", "", ""], log: ["Δv перелёт  9 260 м/с", "азимут  43,4°", "окно  Т+02:14:00"]};
    const num = s => parseFloat(String(s).replace(/\s/g, "").replace(",", ".")) || 0, show = v => String(+v.toPrecision(9)).replace(".", ",");
    const fw = () => { try { return ctx.mockup.win(); } catch (e) { return null; } };
    const evalFw = code => { const w = fw(); try { return w && w.eval(code); } catch (e) {} };
    const radio = ["orient", "scr", "hud", "lmode", "ftab", "mfdsel", "mode"];
    function onKey(k) {
      const g = k.group, L = k.label;
      if (extra && extra(k) === true) return;
      if (radio.includes(g)) { for (const o of BT_KEYS()) if (o.group === g) setKey(o, false); setKey(k, true); }
      if (g === "calc") {
        if (/^[0-9]$/.test(L)) { calc.disp = calc.fresh || calc.disp === "0" ? L : calc.disp + L; calc.fresh = false; }
        else if (L === ",") { if (!calc.disp.includes(",")) calc.disp += ","; calc.fresh = false; }
        else if (L === "C") { calc.disp = "0"; calc.acc = null; calc.op = null; calc.fresh = true; }
        else if (L === "±") calc.disp = calc.disp.startsWith("-") ? calc.disp.slice(1) : "-" + calc.disp;
        else if ("+−×÷=".includes(L)) {
          const v = num(calc.disp);
          if (calc.op && calc.acc !== null && !calc.fresh) { const a = calc.acc; calc.disp = show(calc.op === "+" ? a + v : calc.op === "−" ? a - v : calc.op === "×" ? a * v : a / v); }
          calc.acc = num(calc.disp); calc.op = L === "=" ? null : L; calc.fresh = true;
          if (L === "=") calc.log.unshift("= " + calc.disp);
        }
        else if (L === "ЗАП") { const i = calc.cells.indexOf(""); if (i >= 0) calc.cells[i] = calc.disp; calc.log.unshift(`ЯЧ ${i + 1} ← ${calc.disp}`); }
        else if (L === "ВЫЗ") { const last = calc.cells.filter(Boolean).pop(); if (last) { calc.disp = last; calc.fresh = true; } }
        flash(k);
      }
      else if (g === "sys") {   // a system key: calls its page onto the screens (and lights while it is shown)
        for (const o of BT_KEYS()) if (o.group === "sys") setKey(o, false); setKey(k, true);
        if (L.startsWith("МЕХАН")) evalFw("mode3 = 0"); else if (L.startsWith("ТЕПЛ")) evalFw("mode3 = 1"); else if (L.startsWith("АВТОП")) evalFw("mode3 = 2");
        else if (L.startsWith("ДВИГ")) evalFw("frontTab = 1"); else if (L.startsWith("ПОЛЁТ")) evalFw("frontTab = 0");
      }
      else if (g === "flash") flash(k);
      else if (!radio.includes(g)) setKey(k, !k.on);
    }
    let keysRef = null; const BT_KEYS = () => keysRef || [];
    return {calc, onKey, bind(keys) { keysRef = keys; }, evalFw};
  }
  // ---- the crew places in the same language: a desk arc round each crew member at full travel, its groove, one photonic glass ----
  const CREW = [{k: 1, name: "ПУЛЬТ ПРАВЫЙ", hip: [1.665 + .6 * .6204, 78.935 + .6 * .7843], phi: 38.35}, {k: 2, name: "ПУЛЬТ ЛЕВЫЙ", hip: [-1.665 - .6 * .6204, 78.935 + .6 * .7843], phi: -38.35},
                {k: 3, name: "АСТРОНАВИГАТОР", hip: [-2.4, 76.2], phi: 0}];
  function crew(ctx, Mt, P) {
    const ri = .62, ro = .98, top = ctx.YD, gr = .86, span = 42, glassW = .95, glassH = .55;
    const sec = [[[ri + .08, 0], [ri + .08, .09]], [[ri + .08, .09], [ri, .24]], [[ri, .24], [ri, top - .03]], arcPts(ri + .03, top - .03, .03, 180, 90), [[ri + .03, top], [gr - .03, top]],
                 [[gr - .03, top], [gr - .03, top - .05]], [[gr - .03, top - .05], [gr + .03, top - .05]], [[gr + .03, top - .05], [gr + .03, top]], [[gr + .03, top], [ro - .03, top]], arcPts(ro - .03, top - .03, .03, 90, 0), [[ro, top - .03], [ro, 0]], [[ro, 0], [ri + .08, 0]]];
    const canv = [];
    for (const s of CREW) {
      const piv = new T.Group(); piv.position.copy(V3(s.hip[0], 0, s.hip[1])); piv.rotation.y = -s.phi * D2R; ctx.group.add(piv);
      const m = new T.Mesh(arcBody(sec, -span, span, 40), Mt.desk); m.position.z = 79.75; piv.add(m);   // built round AX, moved to the hip
      const {cv, tex} = canvasTex(950, 550), glass = new T.Mesh(new T.PlaneGeometry(glassW, glassH), new T.MeshBasicMaterial({map: tex, side: T.DoubleSide}));
      glass.position.set(0, ctx.FLOOR + top + glassH / 2, -(gr + glassH / 2 * Math.tan(14 * D2R))); glass.rotation.x = -14 * D2R; piv.add(glass);
      canv.push({cv, tex, s, glass, piv, glassH, gr});
    }
    const draw = (P2, t) => { for (const {cv, tex, s} of canv) { const c = cv.getContext("2d"); c.fillStyle = "#03070a"; c.fillRect(0, 0, 950, 550);
      for (let i = 0; i < 2; i++) { const x = 30 + i * 460; c.strokeStyle = P2.dim; c.lineWidth = 2; c.strokeRect(x, 70, 430, 440); txt(c, "МФД " + (i + 1), x + 215, 100, P2.dim, 24);
        for (let j = 0; j < 6; j++) txt(c, ["Орбита", "ВЫС 2,49 км", "СКОР 212 м/с", "Поле 38,2 Тл", "Энерг. 412 МВт", "Время Т+02:14"][(j + i * 3) % 6], x + 30, 160 + j * 52, P2.main, 28, "left", 600); }
      txt(c, s.name, 475, 36, P2.main, 30); tex.needsUpdate = true; } };
    draw(P, 0);
    const cut = (x, y, z, part) => {
      if (ctx.cmdCut(x, y, z, part)) return true;
      if (/^bridge_mfd[0-5]$/.test(part)) return true;
      if (part === "bridge_mfd6" || part === "bridge_nav") return Math.hypot(x + 2.4, z - 76.4) < 1.9;
      if (["bridge_console", "bridge_console_face", "bridge_tub", "bridge_leds", "bridge_metal"].includes(part) && y > 1.03) for (const s of CREW) if (Math.hypot(x - s.hip[0], z - s.hip[1]) < 1.2) return true;
      return false;
    };
    return {draw, cut, canv};
  }
  return {HALO, M, BAND, desk, band, drawFront, drawLeft, drawRight, machine, pkey, grid, paintCap, paintLampKey, paintOrient, paintGlass, ruo, rud, pod, behave, crew, txt, look, round, adi, bars, lampTile, std, D2R};
})();
// the standard assembly of a variant: the desk with its three bands, the machine, the crew places; returns what the page needs
TB.assemble = function (ctx, Mt, o = {}) {
  const P = () => ctx.PAL[ctx.STYLE.pal], st = o.bandStyle || Mt.band, cutMats = TB.desk(ctx, Mt);
  const bL = TB.band(ctx, ctx.DESK.a0, -ctx.hsF0), bF = TB.band(ctx, -ctx.hsF0, ctx.hsF0), bR = TB.band(ctx, ctx.hsF0, ctx.DESK.a1);
  const mA1 = ctx.hsF0 + (ctx.DESK.a1 - ctx.hsF0) * 1050 / 2400; if (!o.noMachine) TB.machine(ctx, ctx.hsF0 + 1, mA1 - 1);
  const crew = TB.crew(ctx, Mt, P());
  const beh = TB.behave(ctx, o.extra);
  let t = 0, acc = 0;
  const drawAll = () => { const p = P(); (o.drawFront || TB.drawFront)(bF, p, st, t); (o.drawLeft || TB.drawLeft)(bL, p, st, t); (o.drawRight || TB.drawRight)(bR, p, st, t, beh.calc); crew.draw(p, t); };
  return {beh, crew, bands: {bL, bF, bR}, cutMats, cut: crew.cut, drawAll,
    step: dt => { t += dt; acc += dt; if (acc > .25) { acc = 0; drawAll(); } if (o.step) o.step(dt); }};
};
