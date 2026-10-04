// VARIANT 6 · the KEYS and the MFDs (the user: "мне нравится режим кнопок, но мфд уже написаны… с управлением с кнопок"):
// the uniform keys he liked, but on the two side consoles beside him (the horseshoe of variant 4, dark), and they DRIVE the
// screens that exist: the MFD select 1–6, power, page (ВЫБ.), menu, the side softkeys Л1–Л6 / П1–П6 of the selected MFD; the
// left lower zone's modes, the front tabs, the HUD, РАБОТА / ЛЕНТА, the opacity; the orientation keys; the machine's keypad.
// The side-stick and РУД at the consoles' fronts, as in variant 4.
VARIANTS.push({id: "V6", name: "6 · клавиши + MFD", info: "Клавиши одного размера (6 × 4,2 см) — на двух боковых пультах рядом с человеком, и они управляют уже написанными экранами: МФД 1–6 (выбранный обведён), ПИТ., ВЫБ. (страница), МЕНЮ, боковые Л1–Л6 / П1–П6 выбранного МФД; режимы нижней зоны левой панели, вкладки переднего экрана, HUD, РАБОТА / ЛЕНТА, непрозрачность; клавиши ориентации; клавиатура машины. Боковая ручка РУО и РУД — на передних концах пультов, как в варианте 4. Тёмный стол — только приборы и лампы.",
  build(ctx) {
    const Mt = TB.M("dark");
    const S6 = {sel: 1, page: {1: 0, 2: 1, 3: 2, 4: 3, 5: 4, 6: 5}, off: {}, menu: {}, hl: null};
    function install() {   // the mockup's MFDs answer the keys (only while this variant is shown)
      const w = ctx.mockup.win(); if (!w || !w.fakeMfd || w.__v6) return; w.__v6 = true;
      const orig = w.fakeMfd, C = w.eval("C");
      w.fakeMfd = function (x, y, ww, h, n) {
        if (!VAR || VAR.def.id !== "V6") return orig(x, y, ww, h, n);
        const bw = 46, dx = x + bw + 10, dy = y + 30, dw = ww - 2 * (bw + 10), dh = h - 90;
        if (S6.off[n]) { w.rect(x, y, ww, h, C.bg, C.fr, 2); w.text("MFD " + n + " · ВЫКЛ", x + ww / 2, y + h / 2, C.dim, 22, "center", 700); }
        else {
          orig(x, y, ww, h, S6.page[n] + 1);
          w.rect(x + ww / 2 - 70, y + 4, 140, 26, C.bg); w.text("MFD " + n, x + ww / 2, y + 22, C.dim, 18, "center", 700);
          if (S6.menu[n]) { w.rect(dx, dy, dw, dh, "#050b0a", "#2c6a54", 1.5); ["Orbit", "Surface", "HSI / Map", "Docking", "Transfer", "Systems"].forEach((t, i) => w.text((i === S6.page[n] ? "> " : "   ") + t, dx + 14, dy + 34 + i * 30, i === S6.page[n] ? "#ffd29a" : C.tx, 22, "left", 600)); }
        }
        if (n === S6.sel) w.rect(x - 3, y - 3, ww + 6, h + 6, null, "#ffc46a", 4);
        const hl = S6.hl;
        if (hl && hl.n === n && performance.now() - hl.t < 400) {
          if (hl.side) w.rect(hl.side === "L" ? x + 4 : x + ww - bw - 4, dy + hl.i * dh / 6 + 4, bw, dh / 6 - 8, "#3b2a14", "#ffc46a", 3);
          else w.rect(dx + hl.i * dw / 3 + 8, y + h - 50, dw / 3 - 16, 38, "#3b2a14", "#ffc46a", 3);
        }
      };
    }
    const extra = k => {
      const L = k.label, g = k.group;
      if (g === "mfdsel") S6.sel = +L.slice(-1);
      else if (g === "mfd") {
        const n = S6.sel;
        if (L === "ПИТ.") { S6.off[n] = !S6.off[n]; S6.hl = {n, i: 0, t: performance.now()}; }
        else if (L === "ВЫБ.") { S6.page[n] = (S6.page[n] + 1) % 6; S6.hl = {n, i: 1, t: performance.now()}; }
        else if (L === "МЕНЮ") { S6.menu[n] = !S6.menu[n]; S6.hl = {n, i: 2, t: performance.now()}; }
        else S6.hl = {n, side: L[0] === "Л" ? "L" : "R", i: +L.slice(1) - 1, t: performance.now()};
        flash(k); return true;
      }
      else if (g === "lmode") A.beh.evalFw("mode3 = " + ["МЕХАНИЗАЦИЯ", "ТЕПЛО", "АВТОПИЛОТ"].indexOf(L));
      else if (g === "ftab") A.beh.evalFw("frontTab = " + ["ПОЛЁТ", "ДВИГАТЕЛИ", "ПАРАМЕТРЫ"].indexOf(L));
      else if (g === "mode") ctx.LOOK.modeT = L === "ЛЕНТА" ? 1 : 0;
      else if (g === "opq") { ctx.setOpq(Math.max(0, Math.min(1, ctx.LOOK.opq + (L.endsWith("+") ? .1 : -.1)))); flash(k); return true; }
      return false;
    };
    const A = TB.assemble(ctx, Mt, {extra, step: install});
    const R = TB.sideConsole(ctx, 1, Mt), Lc = TB.sideConsole(ctx, -1, Mt);
    TB.sideStick(ctx.group, .63, R.top, 80.0, Mt);
    TB.rud(ctx.group, -.63, Lc.top, 80.0, Mt, "t");
    const kw = .06, kd = .042, px = .07, pz = .052, ori = {paint: TB.paintOrient, side: Mt.dark, h: .01};
    const _ = null;
    TB.grid(R.plate, .51, -79.86, 4, kw, kd, px, pz, [
      ["МФД 1", "mfdsel"], ["МФД 2", "mfdsel"], ["МФД 3", "mfdsel"], ["ПИТ.", "mfd"],
      ["МФД 4", "mfdsel"], ["МФД 5", "mfdsel"], ["МФД 6", "mfdsel"], ["ВЫБ.", "mfd"],
      ["Л1", "mfd"], ["Л2", "mfd"], ["Л3", "mfd"], ["МЕНЮ", "mfd"],
      ["Л4", "mfd"], ["Л5", "mfd"], ["Л6", "mfd"], ["ВЫЗ", "calc"],
      ["П1", "mfd"], ["П2", "mfd"], ["П3", "mfd"], ["ЗАП", "calc"],
      ["П4", "mfd"], ["П5", "mfd"], ["П6", "mfd"], ["C", "calc", RED],
      ["7", "calc"], ["8", "calc"], ["9", "calc"], ["÷", "calc"], ["4", "calc"], ["5", "calc"], ["6", "calc"], ["×", "calc"],
      ["1", "calc"], ["2", "calc"], ["3", "calc"], ["−", "calc"], ["0", "calc"], [",", "calc"], ["=", "calc", AMB], ["+", "calc"]], {h: .012});
    TB.grid(Lc.plate, -.72, -79.86, 4, kw, kd, px, pz, [
      ["МЕХАНИЗАЦИЯ", "lmode"], ["ТЕПЛО", "lmode"], ["АВТОПИЛОТ", "lmode"], _,
      ["ПОЛЁТ", "ftab"], ["ДВИГАТЕЛИ", "ftab"], ["ПАРАМЕТРЫ", "ftab"], _,
      ["HUD ГОРИЗ.", "hud"], ["HUD ОРБИТА", "hud"], ["HUD СТЫК.", "hud"], ["HUD ВЫКЛ", "hud"],
      ["РАБОТА", "mode"], ["ЛЕНТА", "mode"], ["НЕПРОЗР. −", "opq"], ["НЕПРОЗР. +", "opq"]], {h: .012});
    TB.grid(Lc.plate, -.72, -79.86 + 4 * pz, 4, kw, kd, px, pz, [
      ["ПРОГРАД", "orient"], ["РЕТРОГРАД", "orient"], ["НОРМ. +", "orient"], ["НОРМ. −", "orient"],
      ["РАД. +", "orient"], ["РАД. −", "orient"], ["ГОРИЗОНТ", "orient"], ["СТОП ВРАЩ.", "orient"],
      ["ВЫСОТА", "orient"], ["РУЧН.", "orient", AMB]], ori);
    A.beh.bind(BT.keys);
    for (const k of BT.keys) if (["МФД 1", "МЕХАНИЗАЦИЯ", "ПОЛЁТ", "HUD ГОРИЗ.", "РАБОТА", "РУЧН."].includes(k.label)) setKey(k, true);
    A.drawAll();
    return {onKey: A.beh.onKey, step: A.step, cutMats: A.cutMats, cut: A.cut, repaint() { BUILT.V6 && BUILT.V6.keys.forEach(paintKey); A.drawAll(); }};
  }});
