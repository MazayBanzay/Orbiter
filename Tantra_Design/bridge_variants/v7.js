// VARIANT 7 · the yoke (variant 3) + the keys driving the MFDs (variant 6); the colour of the mnemonic variant (2): the white
// АЭС enamel; the light: the suit's emerald EL mode. Only the left handle (РУД) stays: the right hand is on the yoke, a side-stick
// there would be a second rotation control. The orientation keys moved onto the yoke's hub, under the thumbs. The key fields are
// 4 cm nearer to him (the outer column at x 0.68): a 5 % woman reaches every key at adj ≥ +0.10, a 95 % man at any adj.
// The seat's place: a physical sensor — a glowing strip with a scale, sunk 3 mm into the right armrest's inner ledge (where the
// game has its slider, kSeatAdjC), riding with the seat; a touch on it sets the place (−0.10…+0.20 m).
VARIANTS.push({id: "V7", name: "7 · штурвал + клавиши/MFD", info: "Штурвал (из 3) + клавиши, которые управляют MFD (из 6). Стол и пульты — малахитовый металл, свет — голубая фотоника; клавиши — горящее стекло: крышка-световод слабо светится, включённая наливается светом (тёмная надпись, двойной контур, свет на панель вокруг), мерцают только включённые. Штурвал: телескопическая колонка с гофрой, рога вбок-вниз, рукояти разведены наружу, справа красная «АП ОТКЛ.», слева грибок триммера; наверху хаба экран: скорость, высота, перегрузка (верх экрана ниже линии взгляда на нижний край стекла). Справа ручки нет — правая рука на штурвале; слева РУД. Клавиши ориентации — на хабе штурвала под большими пальцами. Блоки клавиш подняты по пультам к столу; наверху правого — люминофорный экран расчётной машины, сама машина стоит внутри правого пульта (видна «в разрезе»); на столе справа — журнал расчётов и хранение. Передний ряд клавиш 5 % женщина достаёт при регулировке +0,20, 95 % мужчина — при любой. Положение кресла — сенсорная светящаяся полоса со шкалой, утопленная во внутреннюю полку правого подлокотника (там же, где в игре); касание задаёт место. Курсорный пульт («солнечный зайчик») — на переднем конце левого подлокотника: предплечье на подлокотнике, ладонь на шаре; едет вместе с креслом, поэтому всегда под рукой при любой регулировке. Нажатие на шар включает пятно света на экранах, повторное — гасит. Пока включено, пятно идёт за указателем мыши по стёклам (мышь — это шар), ничего тянуть не надо; щелчок по стеклу (или ВВОД под большим пальцем) нажимает, пятно вспыхивает; над нажимаемым оно янтарное и крупнее. Всё управление с экранов — как было, пятно лишь показывает, куда «дотянулся». На левом пульте впереди посередине — две рукояти рядом: МАРШ (все маршевые вместе, на голове мини-джойстик поступательного движения) и ГОНДОЛЫ (их тяга; на голове колёсико угла сопел 0° назад … 90° вниз … 100°, упоры 0° и 90°) — тянуть мышью. Выпуск и уборка гондол — клавиша под красным колпачком (сначала откинуть колпачок, потом клавиша): створки, выход 6 с; пока не вышли — рукоять заперта на 0, сопла на 0°. Уборка — только при тяге гондол 0: сопла сами в 0°, затем в отсеки. Перед рукоятями — табло гондол: состояние, угол сопел, тяга, замок. За рукоятями — клавиши экранов. Экраны управляются клавишами, «зайчиком» и прямым касанием. Рыскание и гондолы — ждут решения.",
  build(ctx) {
    const Mt = TB.M("mal");   // the malachite metal: one family with the emerald light, no glare in the glass (the white glared)
    // ---- the MFDs answer the keys (the mockup's fake MFDs: the selected one framed, its page, power, menu, the pressed softkey) ----
    const S6 = {sel: 1, page: {1: 0, 2: 1, 3: 2, 4: 3, 5: 4, 6: 5}, off: {}, menu: {}, hl: null};
    function install() {
      const w = ctx.mockup.win(); if (!w || !w.fakeMfd || w.__v7) return; w.__v7 = true;
      const orig = w.fakeMfd, C = w.eval("C");
      w.fakeMfd = function (x, y, ww, h, n) {
        if (!VAR || VAR.def.id !== "V7") return orig(x, y, ww, h, n);
        const bw = 46, dx = x + bw + 10, dy = y + 30, dw = ww - 2 * (bw + 10), dh = h - 90;
        if (S6.off[n]) { w.rect(x, y, ww, h, C.bg, C.fr, 2); w.text("MFD " + n + " · ВЫКЛ", x + ww / 2, y + h / 2, C.dim, 22, "center", 700); }
        else {
          orig(x, y, ww, h, S6.page[n] + 1);
          w.rect(x + ww / 2 - 70, y + 4, 140, 26, C.bg); w.text("MFD " + n, x + ww / 2, y + 22, C.dim, 18, "center", 700);
          if (S6.menu[n]) { w.rect(dx, dy, dw, dh, "#050b0a", "#2c6a54", 1.5); ["Orbit", "Surface", "HSI / Map", "Docking", "Transfer", "Systems"].forEach((t, i) => w.text((i === S6.page[n] ? "> " : "   ") + t, dx + 14, dy + 34 + i * 30, i === S6.page[n] ? "#ffd29a" : C.tx, 22, "left", 600)); }
        }
        if (n === S6.sel) w.rect(x - 3, y - 3, ww + 6, h + 6, null, "#ffc46a", 4);
        const X = w.eval("X"), Y = w.eval("Y"), K = w.eval("K"), hits = w.eval("hits"), hit = (rx, ry, rw, rh, act) => hits.push({x: X(rx), y: Y(ry), w: rw * K, h: rh * K, act});
        hit(x, y, ww, h, () => mfdAct(n, null));                                          // the MFD itself: select it
        for (let i = 0; i < 6; i++) { hit(x + 4, dy + i * dh / 6 + 4, bw, dh / 6 - 8, () => mfdAct(n, "L", i)); hit(x + ww - bw - 4, dy + i * dh / 6 + 4, bw, dh / 6 - 8, () => mfdAct(n, "R", i)); }
        ["PWR", "SEL", "MNU"].forEach((m, i) => hit(dx + i * dw / 3 + 8, y + h - 50, dw / 3 - 16, 38, () => mfdAct(n, m)));
        const hl = S6.hl;
        if (hl && hl.n === n && performance.now() - hl.t < 400) {
          if (hl.side) w.rect(hl.side === "L" ? x + 4 : x + ww - bw - 4, dy + hl.i * dh / 6 + 4, bw, dh / 6 - 8, "#3b2a14", "#ffc46a", 3);
          else w.rect(dx + hl.i * dw / 3 + 8, y + h - 50, dw / 3 - 16, 38, "#3b2a14", "#ffc46a", 3);
        }
      };
    }
    function mfdAct(n, what, i) {   // one action for the keys, the cursor and the mouse: select the MFD, press its button
      S6.sel = n; for (const k of myKeys) if (k.group === "mfdsel") setKey(k, k.label === "МФД " + n);
      const now = performance.now();
      if (what === "PWR") { S6.off[n] = !S6.off[n]; S6.hl = {n, i: 0, t: now}; }
      else if (what === "SEL") { S6.page[n] = (S6.page[n] + 1) % 6; S6.hl = {n, i: 1, t: now}; }
      else if (what === "MNU") { S6.menu[n] = !S6.menu[n]; S6.hl = {n, i: 2, t: now}; }
      else if (what) S6.hl = {n, side: what, i, t: now};
    }
    const extra = k => {
      const L = k.label, g = k.group;
      if (g === "mfdsel") S6.sel = +L.slice(-1);
      else if (g === "mfd") {
        mfdAct(S6.sel, {"ПИТ.": "PWR", "ВЫБ.": "SEL", "МЕНЮ": "MNU"}[L] || (L[0] === "Л" ? "L" : "R"), +L.slice(1) - 1);
        flash(k); return true;
      }
      else if (g === "cur") { cursorEnter(); flash(k); return true; }
      else if (g === "pods") {   // under the guard: deploy, or stow (only with their thrust at 0)
        if (!cover.open) { podMsg("сначала откройте колпачок"); return true; }
        if (POD.want) { if (POD.set > .001) podMsg("уборка: сначала тяга гондол 0"); else { POD.want = 0; podMsg("уборка: сопла в 0°, затем в отсеки"); } }
        else { POD.want = 1; podMsg("выпуск: створки, выход 6 с"); }
        cover.t = performance.now(); return true;
      }
      else if (g === "lmode") A.beh.evalFw("mode3 = " + ["МЕХАНИЗАЦИЯ", "ТЕПЛО", "АВТОПИЛОТ"].indexOf(L));
      else if (g === "ftab") A.beh.evalFw("frontTab = " + ["ПОЛЁТ", "ДВИГАТЕЛИ", "ПАРАМЕТРЫ"].indexOf(L));
      else if (g === "mode") ctx.LOOK.modeT = L === "ЛЕНТА" ? 1 : 0;
      else if (g === "opq") { ctx.setOpq(Math.max(0, Math.min(1, ctx.LOOK.opq + (L.endsWith("+") ? .1 : -.1)))); flash(k); return true; }
      return false;
    };
    // ---- the yoke: out of the desk's inner face only when the seat has stopped, 0.16 m short of the knees, its hub turned to the eye ----
    const yoke = {d: 0};
    function stepYoke(dt) {
      const want = ctx.SEAT.target === 1 && ctx.SEAT.cur > .985 ? 1 : 0;
      yoke.d += Math.sign(want - yoke.d) * Math.min(Math.abs(want - yoke.d), dt / (want ? 1.6 : .5));
      const e = yoke.d * yoke.d * (3 - 2 * yoke.d), hip = 78.95 + ctx.SEAT.offset(), f = MAN.show === "f05", D = ctx.MAN_DIMS[f ? "f05" : "m95"], ext = f ? .52 : .62;
      const hz = 80.62 - e * (80.62 - (hip + ext)), hy = 1.80 + .12 * e;   // stowed low against the desk (under his sight line), up as it comes out
      const B = V3(0, 1.60, 80.66), H = V3(0, hy, hz), dir = H.clone().sub(B);
      const L = dir.length(), u = dir.clone().normalize();   // the telescopic column: the outer tube out of the gaiter, the inner one to the hub
      tube(colOut, B, B.clone().addScaledVector(u, Math.min(L, .26))); tube(colIn, B.clone().addScaledVector(u, Math.min(L, .2)), H);
      boot.position.copy(B); boot.quaternion.setFromUnitVectors(Y1, u);
      hub.position.copy(H);
      const eyeY = D.hipY + (D.torso + D.neckHead) * Math.cos(11 * Math.PI / 180), eyeZ = hip - .1;
      hub.rotation.x = -Math.atan2(eyeY - hy, hz - eyeZ);
    }
    // ---- the seat's place: the sensor strip (64 × 400 px canvas, its top = forward = +0.20 m) ----
    const adjC = {x: .31, y: 1.6715, z: 79.05}, len = .28, wid = .045, stripT = canvasTex(80, 480);
    function drawStrip() {
      const c = stripT.ctx, W = 80, H = 480, P = ctx.PAL[ctx.STYLE.pal], yOf = v => H - 24 - (v + .10) / .30 * (H - 48);
      c.fillStyle = "#03080a"; c.fillRect(0, 0, W, H);
      const y = yOf(ctx.SEAT.adj); c.fillStyle = P.main; c.globalAlpha = .5; c.fillRect(8, y, 30, H - 24 - y); c.globalAlpha = 1;   // the fill up to the seat's place
      for (let i = 0; i <= 30; i++) { const v = -.10 + i / 100, yy = yOf(v), big = i % 5 === 0; c.strokeStyle = P.dim; c.lineWidth = big ? 3 : 1.5; c.beginPath(); c.moveTo(8, yy); c.lineTo(big ? 44 : 30, yy); c.stroke();
        if (i % 10 === 0) TB.txt(c, (v > 0 ? "+" : v < 0 ? "−" : "") + Math.abs(Math.round(v * 100)), 62, yy, P.dim, 18); }
      c.strokeStyle = P.acc; c.lineWidth = 3; c.beginPath(); c.moveTo(4, yOf(0)); c.lineTo(48, yOf(0)); c.stroke();   // the nominal place
      c.strokeStyle = P.white; c.lineWidth = 4; c.beginPath(); c.moveTo(4, y); c.lineTo(52, y); c.stroke();              // where the seat is
      stripT.tex.needsUpdate = true;
    }
    function drawRight7(B, P, st, tt, calc) {   // the desk's right band: the journal of the calculations instead of the machine's screen
      TB.drawRight(B, P, st, tt, calc);
      const c = B.c, L = TB.look(st), x1 = 1050, H = B.h;
      c.fillStyle = L.bg; c.fillRect(0, 0, x1, H);
      c.fillStyle = L.win; c.fillRect(40, 40, x1 - 80, H - 80); c.strokeStyle = L.bez; c.lineWidth = 6; c.strokeRect(40, 40, x1 - 80, H - 80);
      TB.txt(c, "ЖУРНАЛ РАСЧЁТОВ", 80, 82, P.dim, 30, "left");
      calc.log.slice(0, 5).forEach((l, i) => TB.txt(c, l, 80, 132 + i * 46, i ? P.main : P.white, 34, "left", 600));
      TB.txt(c, "ЗАПИСЬ И ХРАНЕНИЕ РАСЧЁТОВ", 60, H - 18, L.lab, 22, "left");
      B.tex.needsUpdate = true;
    }
    // the machine's own phosphor screen, at the top (front) of the right console's block, tilted to his eye
    const scrT = canvasTex(420, 380);
    function drawCalcScreen() {
      const c = scrT.ctx, W = 420, H = 380, calc = A.beh.calc, PH = "#4dff88";
      c.fillStyle = "#020803"; c.fillRect(0, 0, W, H);
      c.save(); c.shadowColor = PH; c.shadowBlur = 10;
      TB.txt(c, calc.disp, W - 24, 120, PH, 78, "right", 700);
      if (calc.op) TB.txt(c, calc.op, 30, 120, PH, 54, "left", 700);
      calc.log.slice(0, 2).forEach((l, i) => TB.txt(c, l, 24, 215 + i * 44, "#2fbf62", 30, "left", 600));
      c.restore();
      for (let i = 0; i < 8; i++) { c.fillStyle = calc.cells[i] ? PH : "#0c2414"; c.fillRect(24 + i * 48, H - 64, 38, 22); TB.txt(c, String(i + 1), 43 + i * 48, H - 24, "#2fbf62", 20); }
      TB.txt(c, "РАСЧЁТНАЯ МАШИНА", 24, 36, "#2fbf62", 24, "left");
      scrT.tex.needsUpdate = true;
    }
    const A = TB.assemble(ctx, Mt, {extra, noMachine: true, drawRight: drawRight7, step: dt => { install(); stepYoke(dt); drawStrip(); drawCalcScreen(); drawYokeScreen(); stepCursor(); stepThrust(dt); flicker(); }});
    function flicker() { const fl = ctx.PAL[ctx.STYLE.pal].halo; for (const k of myKeys) if (k.topMat) k.topMat.color.setScalar(k.on && fl ? .94 + .06 * Math.random() : 1); }   // the photonic flicker: on the lit keys only
    // ---- THE YOKE as a real one: a telescopic column with a gaiter where it leaves the desk; the hub (its face to the eye) with
    // the orientation keys and, on its top, a screen with the speed, the altitude and the g; the horns out and down, the grips
    // splayed 13° outward at their feet; on the right horn's top the red autopilot disconnect, on the left one the trim hat ----
    const Y1 = new THREE.Vector3(0, 1, 0), tube = (m, a, b) => { const d = b.clone().sub(a), l = d.length(); m.scale.y = Math.max(.01, l); m.position.copy(a).addScaledVector(d, .5); m.quaternion.setFromUnitVectors(Y1, d.normalize()); };
    const colOut = new THREE.Mesh(new THREE.CylinderGeometry(.032, .032, 1, 20), Mt.trim), colIn = new THREE.Mesh(new THREE.CylinderGeometry(.024, .024, 1, 20), Mt.trim); ctx.group.add(colOut, colIn);
    const bootG = new THREE.CylinderGeometry(.034, .055, .09, 24, 4); bootG.translate(0, .045, 0); const boot = new THREE.Mesh(bootG, Mt.rubber); ctx.group.add(boot);
    const hub = new THREE.Group(); ctx.group.add(hub);
    { const sh = new THREE.Shape(), w = .26, h = .088, r = .02; sh.moveTo(-w / 2 + r, -h / 2); sh.lineTo(w / 2 - r, -h / 2); sh.quadraticCurveTo(w / 2, -h / 2, w / 2, -h / 2 + r); sh.lineTo(w / 2, h / 2 - r); sh.quadraticCurveTo(w / 2, h / 2, w / 2 - r, h / 2);
      sh.lineTo(-w / 2 + r, h / 2); sh.quadraticCurveTo(-w / 2, h / 2, -w / 2, h / 2 - r); sh.lineTo(-w / 2, -h / 2 + r); sh.quadraticCurveTo(-w / 2, -h / 2, -w / 2 + r, -h / 2);
      const g = new THREE.ExtrudeGeometry(sh, {depth: .036, bevelEnabled: true, bevelThickness: .008, bevelSize: .007, bevelSegments: 3}); g.translate(0, 0, -.018 - .008); hub.add(new THREE.Mesh(g, Mt.dark)); }
    for (const s of [-1, 1]) {
      const horn = new THREE.CatmullRomCurve3([new THREE.Vector3(s * .12, 0, 0), new THREE.Vector3(s * .175, .008, 0), new THREE.Vector3(s * .222, -.018, 0), new THREE.Vector3(s * .243, -.058, 0)]);
      hub.add(new THREE.Mesh(new THREE.TubeGeometry(horn, 24, .017, 12, false), Mt.dark));
      const a = new THREE.Vector3(s * .243, -.058, 0), b = new THREE.Vector3(s * .268, -.168, 0);   // the grip, splayed outward
      const grip = new THREE.Mesh(new THREE.CylinderGeometry(.021, .0185, 1, 18), Mt.grip); tube(grip, a, b); hub.add(grip);
      for (let k = 1; k <= 3; k++) { const q = a.clone().lerp(b, .22 + k * .17), rd = new THREE.Mesh(new THREE.TorusGeometry(.0205, .0032, 6, 18), Mt.rubber); rd.position.copy(q); rd.quaternion.setFromUnitVectors(new THREE.Vector3(0, 0, 1), b.clone().sub(a).normalize()); hub.add(rd); }   // the finger ridges
      const end = new THREE.Mesh(new THREE.SphereGeometry(.0185, 14, 10), Mt.grip); end.position.copy(b); hub.add(end);
      if (s > 0) { const btn = new THREE.Mesh(new THREE.CylinderGeometry(.008, .008, .008, 14), TB.std(0xd8261c, .4, 0, null, {emissive: 0x3a0503})); btn.position.set(.236, -.012, .006); btn.rotation.z = -.5; hub.add(btn); }   // АП ОТКЛ.
      else { const hat = new THREE.Mesh(new THREE.BoxGeometry(.016, .007, .016), Mt.trim); hat.position.set(-.236, -.01, .006); hat.rotation.z = .5; hub.add(hat); }          // the trim hat
    }
    // the yoke's own screen on the hub's top: the speed, the altitude, the g (a copy for the eyes kept up, the hub hides the desk's centre)
    const ykT = canvasTex(512, 128);
    { const hous = new THREE.Mesh(new THREE.BoxGeometry(.214, .062, .022), Mt.dark); hous.position.set(0, .078, -.004); hub.add(hous);
      const scr = new THREE.Mesh(new THREE.PlaneGeometry(.2, .05), new THREE.MeshBasicMaterial({map: ykT.tex})); scr.position.set(0, .078, .0075); hub.add(scr); }
    function drawYokeScreen() {
      const c = ykT.ctx, P = ctx.PAL[ctx.STYLE.pal], tt = performance.now() / 1000;
      c.fillStyle = "#020906"; c.fillRect(0, 0, 512, 128);
      const vals = [["СКОР м/с", String(Math.round(212 + 4 * Math.sin(tt * .17)))], ["ВЫС км", (2.49 + .01 * Math.sin(tt * .2)).toFixed(2).replace(".", ",")], ["g", (1.02 + .03 * Math.sin(tt * .4)).toFixed(2).replace(".", ",")]];
      vals.forEach(([l, val], i) => { const x = 14 + i * 168; TB.txt(c, l, x, 26, P.dim, 22, "left", 700); TB.txt(c, val, x + 150, 84, P.main, 58, "right", 700); if (i) { c.strokeStyle = P.dim; c.lineWidth = 2; c.beginPath(); c.moveTo(x - 8, 14); c.lineTo(x - 8, 114); c.stroke(); } });
      ykT.tex.needsUpdate = true;
    }
    const face = new THREE.Group(); face.position.set(0, 0, .018); face.rotation.x = Math.PI / 2; hub.add(face);
    TB.grid(face, -.104, -.018, 5, .046, .03, .052, .036,
      [["ПРОГРАД", "orient"], ["РЕТРОГРАД", "orient"], ["НОРМ. +", "orient"], ["НОРМ. −", "orient"], ["ВЫСОТА", "orient"],
       ["РАД. +", "orient"], ["РАД. −", "orient"], ["ГОРИЗОНТ", "orient"], ["СТОП ВРАЩ.", "orient"], ["РУЧН.", "orient", AMB]], {paint: TB.paintGlass, glass: true, h: .006});
    // the side consoles: the right one drives the MFDs and the machine, the left one the screens; РУД at the left one's front
    const R = TB.sideConsole(ctx, 1, Mt), Lc = TB.sideConsole(ctx, -1, Mt);
    const kw = .056, kd = .042, px = .064, pz = .052, _ = null, glass = {paint: TB.paintGlass, glass: true, h: .012}, myKeys = BT.keys;
    TB.grid(R.plate, .49, -79.94, 4, kw, kd, px, pz, [
      ["МФД 1", "mfdsel"], ["МФД 2", "mfdsel"], ["МФД 3", "mfdsel"], ["ПИТ.", "mfd"],
      ["МФД 4", "mfdsel"], ["МФД 5", "mfdsel"], ["МФД 6", "mfdsel"], ["ВЫБ.", "mfd"],
      ["Л1", "mfd"], ["Л2", "mfd"], ["Л3", "mfd"], ["МЕНЮ", "mfd"],
      ["Л4", "mfd"], ["Л5", "mfd"], ["Л6", "mfd"], ["ВЫЗ", "calc"],
      ["П1", "mfd"], ["П2", "mfd"], ["П3", "mfd"], ["ЗАП", "calc"],
      ["П4", "mfd"], ["П5", "mfd"], ["П6", "mfd"], ["C", "calc", RED],
      ["7", "calc"], ["8", "calc"], ["9", "calc"], ["÷", "calc"], ["4", "calc"], ["5", "calc"], ["6", "calc"], ["×", "calc"],
      ["1", "calc"], ["2", "calc"], ["3", "calc"], ["−", "calc"], ["0", "calc"], [",", "calc"], ["=", "calc", AMB], ["+", "calc"]], glass);
    TB.grid(Lc.plate, -.618, -79.88, 3, kw, kd, px, pz, [
      ["МЕХАНИЗАЦИЯ", "lmode"], ["ТЕПЛО", "lmode"], ["АВТОПИЛОТ", "lmode"],
      ["ПОЛЁТ", "ftab"], ["ДВИГАТЕЛИ", "ftab"], ["ПАРАМЕТРЫ", "ftab"],
      ["HUD ГОРИЗ.", "hud"], ["HUD ОРБИТА", "hud"], ["HUD СТЫК.", "hud"],
      ["HUD ВЫКЛ", "hud"], ["РАБОТА", "mode"], ["ЛЕНТА", "mode"],
      ["НЕПРОЗР. −", "opq"], ["НЕПРОЗР. +", "opq"]], glass);
    // ---- THE CURSOR UNIT («солнечный зайчик»): on the front end of the LEFT ARMREST, under the left palm with the forearm lying on
    // the armrest; it rides with the seat, so it is under the hand at any adj, for a 95 % man and a 5 % woman alike, and the thrust
    // lever is one short move forward-out. A press on the ball switches the spot of light on the screens on, the next press off;
    // rolling it moves the spot over the three glasses as one strip (L | F | R); ВВОД (under the thumb, inboard) presses what the
    // spot is on. It is how he "reaches" the screens from the seat. (Built at the seat's rest place: armrest top 1.765, its end 79.17.)
    const tbTop = 1.765, tbX = -.36, tbZ = 79.20, SG = ctx.seatGroup;
    const housing = new THREE.Mesh(new THREE.BoxGeometry(.14, .014, .17), Mt.dark); housing.position.copy(V3(tbX, tbTop + .007, tbZ)); SG.add(housing);
    const ballMat = TB.std(0x9fdcf2, .08, 0, null, {transparent: true, opacity: .72, emissive: 0x1a4352});
    const ball = new THREE.Mesh(new THREE.SphereGeometry(.023, 32, 20), ballMat); ball.position.copy(V3(tbX - .01, tbTop + .016, tbZ + .02)); SG.add(ball);
    const bez = new THREE.Mesh(new THREE.TorusGeometry(.026, .0035, 8, 32), Mt.trim); bez.rotation.x = Math.PI / 2; bez.position.copy(V3(tbX - .01, tbTop + .015, tbZ + .02)); SG.add(bez);
    const tbPlate = new THREE.Group(); tbPlate.position.copy(V3(0, tbTop, 0)); SG.add(tbPlate);
    TB.pkey(tbPlate, tbX + .052, .014, -(tbZ - .01), .03, .028, "ВВОД", "cur", null, glass);
    const rayAt = e => { const r = renderer.domElement.getBoundingClientRect(), rc = new THREE.Raycaster(); rc.setFromCamera(new THREE.Vector2((e.clientX - r.left) / r.width * 2 - 1, -(e.clientY - r.top) / r.height * 2 + 1), cam); return rc; };
    // The spot follows the pointer over the glasses (the mouse is the ball): the cursor unit only switches it on and off; the
    // screens are pressed as before (a click on the glass = ВВОД), the spot just shows where — so the distance does not matter.
    const CUR = {on: false, hit: null, t: 0};
    const spot = new THREE.Mesh(new THREE.PlaneGeometry(.075, .075), new THREE.MeshBasicMaterial({map: TB.HALO, color: 0xe6f6ff, transparent: true, blending: THREE.AdditiveBlending, depthTest: false, depthWrite: false}));
    const core = new THREE.Mesh(new THREE.CircleGeometry(.0055, 20), new THREE.MeshBasicMaterial({color: 0xffffff, transparent: true, depthTest: false}));
    spot.renderOrder = 10; core.renderOrder = 11; spot.add(core); ctx.group.add(spot);
    function glassAt(e) {   // the point of the glass under the pointer: its screen, its texture (u, v from the top), its place
      const sheets = PH_SHEETS.filter(o => o.mesh.visible).map(o => o.mesh), h = rayAt(e).intersectObjects(sheets)[0]; if (!h) return null;
      const o = PH_SHEETS.find(s => s.mesh === h.object);
      return {key: o.key, o, u: h.uv.x, v: 1 - h.uv.y, p: h.point.clone().addScaledVector(h.point.clone().sub(cam.position).normalize(), -.012)};
    }
    function frontCentre() {   // where it lights up first: the middle of the front glass
      const o = PH_SHEETS.find(s => s.key === "F"); if (!o || !o.sp) return null; const sp = o.sp, y = sp.y0 + sp.h / 2;
      return {key: "F", o, u: (sp.uv0 + sp.uv1) / 2, v: 1 - (sp.vv0 + sp.vv1) / 2, p: P3(sp.mid, sp.R + (y - sp.y0) * Math.tan(sp.lean * Math.PI / 180) - .015, y)};
    }
    function curAt() { return CUR.hit && CUR.hit.o.mesh.visible ? CUR.hit : frontCentre(); }
    function cursorEnter() { const q = curAt(); if (!q || !CUR.on) return; ctx.mockup.click(q.key, q.u, q.v); CUR.t = performance.now(); }
    function stepCursor() {
      const q = curAt(), tt = performance.now() / 1000; spot.visible = CUR.on && !!q && q.o.mesh.visible && VAR === BUILT.V7;
      ballMat.emissive.setHex(CUR.on ? 0x4fb3d6 : 0x1a4352); if (!spot.visible) return;
      spot.position.copy(q.p); spot.lookAt(cam.position);
      let hov = false;   // something to press under it? (the hit areas of the screens)
      try { const rg = ctx.mockup.regions(); if (rg) { const [x, y, w, h] = rg[q.key], px = x + q.u * w, py = y + q.v * h; hov = ctx.mockup.win().eval("hits").some(e => px >= e.x && px <= e.x + e.w && py >= e.y && py <= e.y + e.h); } } catch (e) {}
      const P = ctx.PAL[ctx.STYLE.pal], fresh = performance.now() - CUR.t < 220;
      spot.material.color.set(fresh ? "#ffffff" : hov ? P.acc : P.white); spot.scale.setScalar((fresh ? 1.6 : hov ? 1.3 : 1) * (.95 + .05 * Math.sin(tt * 9)));   // the sun spot shimmers
    }
    // ---- THE THROTTLE QUADRANT in the middle of the left console's front: МАРШ (all the main engines together) inboard, ГОНДОЛЫ
    // (the pods' own thrust) outboard, side by side — one palm moves both or each alone. Slots z 79.94…80.14 (0 at the back, МАКС
    // at the front, a lever stays where it is put): a 95 % man reaches МАКС at adj 0, a 5 % woman at +0.20.
    // THE PODS, physically and logically: they are stowed in bays; the ВЫПУСК / УБОРКА switch under a red guard cover (the cover
    // first, then the key: no deploying by a stray hand in flight). Deploy: the doors open, the pods come out (6 s), the doors close;
    // until they are fully out their lever is gated at 0 and their nozzles held at 0°. Stow: refused while their thrust is not 0;
    // the nozzles first turn to 0° (aft, so they fit the bays), then the pods go in. The nozzle angle: the wheel on the pod lever's
    // head under the thumb, 0° НАЗАД … 90° ВНИЗ … 100°, detents at 0° and 90°, the nozzles follow at 15°/s. Their display in front.
    const THR = {set: .45, cur: .45}, POD = {set: 0, cur: 0, ang: 0, angT: 0, out: 0, want: 0, msg: "", msgT: 0}, slotZ = 80.04, slotL = .2;
    const podMsg = m => { POD.msg = m; POD.msgT = performance.now(); };
    function makeLever(x, kind) {
      const sT = canvasTex(64, 256), sl = new THREE.Mesh(new THREE.PlaneGeometry(.05, slotL), new THREE.MeshBasicMaterial({map: sT.tex}));
      sl.rotation.x = -Math.PI / 2; sl.position.copy(V3(x, Lc.top + .0015, slotZ)); ctx.group.add(sl);
      const lv = new THREE.Group(); lv.position.copy(V3(x, Lc.top - .05, slotZ)); ctx.group.add(lv);
      const arm = new THREE.Mesh(new THREE.BoxGeometry(.012, .13, .014), Mt.trim); arm.position.y = .065; lv.add(arm);
      const parts = [arm]; let wheel = null;
      if (kind === "main") {   // a palm head; under the thumb (inboard) the translation mini-stick
        const head = new THREE.Mesh(new THREE.BoxGeometry(.05, .036, .066), Mt.grip); head.position.y = .14; lv.add(head);
        const cap = new THREE.Mesh(new THREE.CylinderGeometry(.025, .025, .066, 16, 1, false, 0, Math.PI), Mt.grip); cap.rotation.x = Math.PI / 2; cap.rotation.z = Math.PI / 2; cap.position.y = .158; lv.add(cap);
        const ms = new THREE.Mesh(new THREE.CylinderGeometry(.0045, .0045, .014, 10), Mt.trim); ms.rotation.z = -Math.PI / 2; ms.position.set(.031, .15, -.01); lv.add(ms);
        const msCap = new THREE.Mesh(new THREE.SphereGeometry(.0075, 12, 8), Mt.rubber); msCap.position.set(.039, .15, -.01); lv.add(msCap);
        parts.push(head, cap, ms, msCap);
      } else {                 // a knurled bar head (felt different from МАРШ without looking); on its inboard end the nozzle wheel
        const head = new THREE.Mesh(new THREE.CylinderGeometry(.018, .018, .046, 18), Mt.grip); head.rotation.z = Math.PI / 2; head.position.y = .145; lv.add(head);
        for (let k = -2; k <= 2; k++) { const r = new THREE.Mesh(new THREE.TorusGeometry(.0185, .002, 6, 18), Mt.rubber); r.rotation.y = Math.PI / 2; r.position.set(k * .008, .145, 0); lv.add(r); }
        wheel = new THREE.Mesh(new THREE.CylinderGeometry(.017, .017, .009, 24), TB.std(0x9aa6ae, .35, .7)); wheel.rotation.z = Math.PI / 2; wheel.position.set(.029, .145, 0); lv.add(wheel);
        parts.push(head);
      }
      return {sT, lv, parts, wheel};
    }
    const mainL = makeLever(-.56, "main"), podL = makeLever(-.645, "pods");
    function drawSlotOf(L, val, locked) {
      const c = L.sT.ctx, P = ctx.PAL[ctx.STYLE.pal]; c.fillStyle = "#040a0d"; c.fillRect(0, 0, 64, 256); c.fillStyle = "#000"; c.fillRect(26, 10, 12, 236);
      for (let i = 0; i <= 10; i++) { const y = 246 - i * 23.6; c.strokeStyle = i % 5 ? P.dim : P.main; c.lineWidth = i % 5 ? 1.5 : 3; c.beginPath(); c.moveTo(6, y); c.lineTo(i % 5 ? 18 : 22, y); c.stroke(); }
      TB.txt(c, "0", 52, 240, P.dim, 14); TB.txt(c, "МАКС", 48, 16, P.acc, 11);
      const y = 246 - val * 236; c.fillStyle = P.main; c.globalAlpha = .55; c.fillRect(26, y, 12, 246 - y); c.globalAlpha = 1;
      if (locked) { c.fillStyle = P.red; c.fillRect(22, 228, 20, 6); TB.txt(c, "ЗАП", 50, 200, P.red, 12); }
      L.sT.tex.needsUpdate = true;
    }
    // the pods' ВЫПУСК / УБОРКА key and its red guard cover (a click on the cover opens it; it falls shut again after 6 s)
    const podKey = TB.pkey(Lc.plate, -.71, 0, -79.86, .046, .036, "ГОНДОЛЫ", "pods", null, glass);
    const cover = {open: false, t: 0, g: new THREE.Group()}; cover.g.position.copy(V3(-.71, Lc.top + .001, 79.838)); ctx.group.add(cover.g);
    { const cg = new THREE.BoxGeometry(.054, .028, .046); cg.translate(0, .014, -.023);
      const cm = new THREE.Mesh(cg, TB.std(0xff5a4a, .3, 0, null, {transparent: true, opacity: .5})); cover.g.add(cm);
      ctx.clickable(cm, () => { cover.open = !cover.open; cover.t = performance.now(); }); }
    // the pods' display: a small glass slab tilted to him in front of the levers (z 80.18…80.30)
    const podT = canvasTex(360, 230);
    { const g = new THREE.Group(); g.position.copy(V3(-.60, Lc.top, 80.24)); ctx.group.add(g);
      const slab = new THREE.Mesh(new THREE.BoxGeometry(.17, .012, .115), Mt.dark); slab.position.set(0, .045, 0); slab.rotation.x = .6; g.add(slab);
      const sup = new THREE.Mesh(new THREE.BoxGeometry(.17, .08, .05), Mt.desk); sup.position.set(0, .04, -.03); g.add(sup);
      const scr = new THREE.Mesh(new THREE.PlaneGeometry(.16, .1), new THREE.MeshBasicMaterial({map: podT.tex})); scr.position.y = .0065; scr.rotation.x = -Math.PI / 2; slab.add(scr); }
    function podState() { return POD.want ? (POD.out >= 1 ? "ВЫПУЩЕНЫ" : "ВЫПУСК…") : (POD.out <= 0 ? "УБРАНЫ" : POD.ang > 1 ? "СОПЛА В 0°…" : "УБОРКА…"); }
    function drawPods() {
      const c = podT.ctx, P = ctx.PAL[ctx.STYLE.pal], W = 360, H = 230, st = podState(), moving = st.endsWith("…"), blink = Math.sin(performance.now() / 160) > 0;
      c.fillStyle = "#040b10"; c.fillRect(0, 0, W, H);
      TB.txt(c, "ГОНДОЛЫ", 14, 24, P.dim, 22, "left"); TB.txt(c, st, 14, 58, POD.out <= 0 ? P.dim : moving ? (blink ? P.acc : P.dim) : P.main, 30, "left");
      // the nozzle: its angle drawn as it stands (0° aft = to the left, 90° down), the target as a tick
      const cx = 268, cy = 70, R = 52; c.strokeStyle = P.dim; c.lineWidth = 2; c.beginPath(); c.arc(cx, cy, R, Math.PI / 2, Math.PI); c.stroke();
      for (const d of [0, 90]) { const a = Math.PI - d * Math.PI / 180; c.beginPath(); c.moveTo(cx + R * Math.cos(a), cy + R * Math.sin(a)); c.lineTo(cx + (R + 8) * Math.cos(a), cy + (R + 8) * Math.sin(a)); c.stroke(); }
      const at = Math.PI - POD.angT * Math.PI / 180, aa = Math.PI - POD.ang * Math.PI / 180;
      c.strokeStyle = P.acc; c.beginPath(); c.moveTo(cx + (R - 6) * Math.cos(at), cy + (R - 6) * Math.sin(at)); c.lineTo(cx + (R + 10) * Math.cos(at), cy + (R + 10) * Math.sin(at)); c.stroke();
      c.strokeStyle = P.main; c.lineWidth = 6; c.beginPath(); c.moveTo(cx, cy); c.lineTo(cx + R * .85 * Math.cos(aa), cy + R * .85 * Math.sin(aa)); c.stroke();
      TB.txt(c, "СОПЛА " + Math.round(POD.ang) + "°", cx - 10, cy + R + 30, P.main, 22);
      // their thrust; the lock
      c.strokeStyle = P.dim; c.lineWidth = 2; c.strokeRect(14, 96, 150, 22); c.fillStyle = P.main; c.fillRect(16, 98, 146 * POD.cur, 18);
      TB.txt(c, "ТЯГА " + Math.round(POD.cur * 100) + " %", 14, 140, P.main, 22, "left");
      if (POD.out < 1) TB.txt(c, "РУКОЯТЬ ЗАПЕРТА", 14, 176, P.red, 20, "left");
      if (performance.now() - POD.msgT < 3000) TB.txt(c, POD.msg, 14, 210, P.acc, 18, "left");
      podT.tex.needsUpdate = true;
    }
    function stepThrust(dt) {
      THR.cur += (THR.set - THR.cur) * Math.min(1, dt * .8);
      if (POD.want === 0) POD.angT = 0;                                                   // stowing: the nozzles to 0° first
      const tgt = POD.want === 0 && POD.ang > 1 ? POD.out : POD.want;                      // they go in only with the nozzles aft
      POD.out += Math.sign(tgt - POD.out) * Math.min(Math.abs(tgt - POD.out), dt / 6);
      if (POD.out < 1) { POD.set = 0; if (POD.want) POD.angT = 0; }                         // gated at 0 until fully out
      POD.ang += Math.sign(POD.angT - POD.ang) * Math.min(Math.abs(POD.angT - POD.ang), dt * 15);
      POD.cur += (POD.set - POD.cur) * Math.min(1, dt * .8);
      mainL.lv.rotation.x = -(THR.set - .5) * 2 * .62; podL.lv.rotation.x = -(POD.set - .5) * 2 * .62; podL.wheel.rotation.x = -POD.angT * .05;
      if (cover.open && performance.now() - cover.t > 6000) cover.open = false;
      cover.g.rotation.x += ((cover.open ? 1.9 : 0) - cover.g.rotation.x) * Math.min(1, dt * 8);
      const st = podState(), lab = "ГОНД. " + (POD.want ? (POD.out >= 1 ? "ВЫП." : "ВЫХОД") : POD.out <= 0 ? "УБР." : "УБОРКА");
      if (podKey.label !== lab || podKey.on !== (POD.out > 0)) { podKey.label = lab; podKey.on = POD.out > 0; paintKey(podKey); }
      drawSlotOf(mainL, THR.set, false); drawSlotOf(podL, POD.set, POD.out < 1); drawPods();
    }
    // the mouse on the ball and on the lever (the view does not turn meanwhile)
    let drag = null;
    renderer.domElement.addEventListener("pointerdown", e => {
      if (VAR !== BUILT.V7) return; const rc = rayAt(e);
      if (rc.intersectObjects([ball, bez, housing]).length) { drag = {kind: "ball", x: e.clientX, y: e.clientY, moved: 0}; ctl.enabled = false; }
      else if (rc.intersectObjects([podL.wheel]).length) { drag = {kind: "wheel", x: e.clientX, y: e.clientY, moved: 0}; ctl.enabled = false; }
      else if (rc.intersectObjects(podL.parts).length) { drag = {kind: "pod", x: e.clientX, y: e.clientY, moved: 0}; ctl.enabled = false; }
      else if (rc.intersectObjects(mainL.parts).length) { drag = {kind: "thr", x: e.clientX, y: e.clientY, moved: 0}; ctl.enabled = false; }
    });
    window.addEventListener("pointermove", e => { if (!drag) return; const dx = e.clientX - drag.x, dy = e.clientY - drag.y; drag.x = e.clientX; drag.y = e.clientY; drag.moved += Math.abs(dx) + Math.abs(dy);
      if (drag.kind === "ball") { ball.rotation.z -= dx * .02; ball.rotation.x += dy * .02; }
      else if (drag.kind === "thr") THR.set = Math.max(0, Math.min(1, THR.set - dy * .004));
      else if (drag.kind === "pod") { if (POD.out >= 1) POD.set = Math.max(0, Math.min(1, POD.set - dy * .004)); else podMsg("гондолы убраны: рукоять заперта"); }
      else if (drag.kind === "wheel") { if (POD.out >= 1) { let a = Math.max(0, Math.min(100, POD.angT - dy * .5)); for (const d of [0, 90]) if (Math.abs(a - d) < 3) a = d; POD.angT = a; } else podMsg("гондолы убраны: сопла заперты"); } });
    window.addEventListener("pointerup", () => { if (!drag) return; if (drag.kind === "ball" && drag.moved < 5) CUR.on = !CUR.on; drag = null; ctl.enabled = true; });
    let pdown = null;
    renderer.domElement.addEventListener("pointermove", e => {   // the spot under the pointer (and the ball turns as if rolled)
      if (VAR !== BUILT.V7 || !CUR.on || drag || e.buttons) return; const g = glassAt(e); if (g) CUR.hit = g;
      if (CUR.lx !== undefined) { ball.rotation.z -= (e.clientX - CUR.lx) * .01; ball.rotation.x += (e.clientY - CUR.ly) * .01; } CUR.lx = e.clientX; CUR.ly = e.clientY; });
    renderer.domElement.addEventListener("pointerdown", e => { pdown = [e.clientX, e.clientY]; });
    renderer.domElement.addEventListener("pointerup", e => {   // a click on the glass presses (the page does it) — the spot flashes
      if (VAR !== BUILT.V7 || !CUR.on || !pdown || Math.hypot(e.clientX - pdown[0], e.clientY - pdown[1]) > 4) return; const g = glassAt(e); if (g) { CUR.hit = g; CUR.t = performance.now(); } });
    { // the machine's screen: a slab tilted 34° to him on a support at the console's front, z 80.01…80.21
      const g = new THREE.Group(); g.position.copy(V3(.585, R.top, 80.11)); ctx.group.add(g);
      const slab = new THREE.Mesh(new THREE.BoxGeometry(.21, .016, .2), Mt.dark); slab.position.set(0, .06, 0); slab.rotation.x = .6; g.add(slab);
      const sup = new THREE.Mesh(new THREE.BoxGeometry(.21, .11, .09), Mt.desk); sup.position.set(0, .055, -.055); g.add(sup);
      const scr = new THREE.Mesh(new THREE.PlaneGeometry(.19, .172), new THREE.MeshBasicMaterial({map: scrT.tex})); scr.position.y = .0085; scr.rotation.x = -Math.PI / 2; slab.add(scr);
      // the machine itself: an optical block filling the console under its screen (seen with the desk cut away)
      const mb = new THREE.Mesh(new THREE.BoxGeometry(.25, .62, .7), TB.std(0x1b2226, .4, .6)); mb.position.copy(V3(.63, 1.40, 79.72)); ctx.group.add(mb);
      const lm = new THREE.LineBasicMaterial({color: 0x4dff88, transparent: true, opacity: .6});
      for (let k = 0; k < 6; k++) { const pts = []; for (let i = 0; i <= 20; i++) pts.push(V3(.505, 1.14 + k * .1 + .02 * Math.sin(i * .9 + k), 79.38 + .68 * i / 20)); ctx.group.add(new THREE.Line(new THREE.BufferGeometry().setFromPoints(pts), lm)); }
    }
    // the seat's position sensor, sunk into the right armrest's inner ledge, its bezel round it
    const strip = new THREE.Mesh(new THREE.PlaneGeometry(wid, len), new THREE.MeshBasicMaterial({map: stripT.tex}));
    strip.rotation.x = -Math.PI / 2; strip.position.copy(V3(adjC.x, adjC.y - .0003, adjC.z)); ctx.seatGroup.add(strip);   // just over the ledge (1.670); its frame stands 5 mm higher: sunk
    for (const [dx, dz, w, d] of [[0, len / 2 + .004, wid + .016, .008], [0, -len / 2 - .004, wid + .016, .008], [wid / 2 + .004, 0, .008, len + .016], [-wid / 2 - .004, 0, .008, len + .016]]) {
      const b = new THREE.Mesh(new THREE.BoxGeometry(w, .007, d), Mt.trim); b.position.copy(V3(adjC.x + dx, adjC.y + .002, adjC.z + dz)); ctx.seatGroup.add(b); }
    ctx.clickable(strip, hit => { const v = Math.max(-.10, Math.min(.20, -.10 + .30 * hit.uv.y)), el = document.getElementById("adj"); el.value = v.toFixed(2); el.oninput({target: el}); });
    drawStrip();
    const adjOld = seatGrp.children.find(m => m.name === "bridge_seat_adj");   // the game's flat slider: the sunken sensor takes its place
    A.beh.bind(BT.keys);
    for (const k of BT.keys) if (["МФД 1", "МЕХАНИЗАЦИЯ", "ПОЛЁТ", "HUD ГОРИЗ.", "РАБОТА", "РУЧН."].includes(k.label)) setKey(k, true);
    A.drawAll();
    return {onKey: A.beh.onKey, step: A.step, cutMats: A.cutMats, cut: A.cut,
      onActive(on) { window.THR = on ? THR : undefined; window.POD = on ? POD : undefined; if (adjOld) adjOld.visible = !on; LOOK.lightK = on ? .8 : 1; if (on && STYLE.pal !== "B") setPal("B"); },
      repaint() { BUILT.V7 && BUILT.V7.keys.forEach(paintKey); A.drawAll(); drawStrip(); drawCalcScreen(); }};
  }});
