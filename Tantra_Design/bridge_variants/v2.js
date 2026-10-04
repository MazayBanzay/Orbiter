// VARIANT 2 · the white panel of a nuclear plant's control room (light enamel) with a MNEMONIC DIAGRAM of the main systems:
// the plant → the field (the traps) → the chambers К1–К4 → the nozzles, the flow running along the lines as light, the lamps
// in the nodes — monitoring first. The controllers ride with the seat on the armrest pods: a ball РУО, a mushroom РУД.
VARIANTS.push({id: "V2", name: "2 · белый АЭС · мнемосхема", info: "Белая эмаль, как на щите АЭС. Слева на столе мнемосхема главных систем: энергоустановка → поле (ловушки) → камеры К1–К4 → сопла; поток бежит по линиям светом, в узлах лампы. Спереди приборы в окнах белой панели, справа люминофорный экран оптической машины. РУО-шар и РУД-гриб на пультах подлокотников — едут с креслом. Клавиши: машина 20, ориентация 10, вызов систем 5.",
  build(ctx) {
    const Mt = TB.M("aes");
    function mnemo(B, P, st, t) {
      const c = B.c, W = B.w, H = B.h, L = TB.look("aes");
      c.fillStyle = L.bg; c.fillRect(0, 0, W, H);
      c.strokeStyle = L.grid; c.lineWidth = 1; for (let x = 0; x < W; x += 50) { c.beginPath(); c.moveTo(x, 0); c.lineTo(x, H); c.stroke(); } for (let y = 0; y < H; y += 50) { c.beginPath(); c.moveTo(0, y); c.lineTo(W, y); c.stroke(); }   // the mosaic
      const N = {plant: [360, 200], field: [900, 200], k1: [1400, 90], k2: [1400, 165], k3: [1400, 240], k4: [1400, 315], noz: [1950, 200]};
      const line = (a, b, col) => { c.strokeStyle = col; c.lineWidth = 10; c.beginPath(); c.moveTo(a[0], a[1]); c.lineTo((a[0] + b[0]) / 2, a[1]); c.lineTo((a[0] + b[0]) / 2, b[1]); c.lineTo(b[0], b[1]); c.stroke();
        c.strokeStyle = P.main; c.lineWidth = 4; c.setLineDash([18, 22]); c.lineDashOffset = -t * 60; c.stroke(); c.setLineDash([]); };
      for (const k of ["k1", "k2", "k3", "k4"]) { line(N.field, N[k], "#3d4a52"); line(N[k], N.noz, "#3d4a52"); }
      line(N.plant, N.field, "#3d4a52");
      const node = (p, w, h, label, s) => TB.lampTile(c, p[0] - w / 2, p[1] - h / 2, w, h, label, s, P, L, "aes");
      node(N.plant, 300, 150, "ЭНЕРГОУСТ. 412 МВт", 1); node(N.field, 260, 150, "ПОЛЕ 38,2 Тл", 1);
      ["k1", "k2", "k3", "k4"].forEach((k, i) => node(N[k], 220, 64, "К" + (i + 1) + " " + (2480 + i * 9) + " К", i === 1 && Math.sin(t * 3) > 0 ? 2 : 1));
      node(N.noz, 260, 150, "СОПЛА · ТЯГА 46 %", 1);
      TB.txt(c, "МНЕМОСХЕМА · ЭНЕРГИЯ → ПОЛЕ → КАМЕРЫ → СОПЛА", 40, 30, "#2a3036", 26, "left");
      B.tex.needsUpdate = true;
    }
    const A = TB.assemble(ctx, Mt, {drawLeft: mnemo});
    const R = TB.pod(ctx, 1, Mt, .62), L = TB.pod(ctx, -1, Mt, .62);
    TB.ruo(ctx.seatGroup, .36, R.top, 79.27, Mt, "ball");
    TB.rud(ctx.seatGroup, -.36, L.top, 79.27, Mt, "mush");
    const cap = {paint: TB.paintCap, style: "aes", side: Mt.key};
    TB.grid(R.plate, .445, -79.25, 4, .038, .036, .044, .046,
      [["7", "calc"], ["8", "calc"], ["9", "calc"], ["÷", "calc"], ["4", "calc"], ["5", "calc"], ["6", "calc"], ["×", "calc"], ["1", "calc"], ["2", "calc"], ["3", "calc"], ["−", "calc"],
       ["0", "calc"], [",", "calc"], ["=", "calc", AMB], ["+", "calc"], ["C", "calc", RED], ["±", "calc"], ["ЗАП", "calc"], ["ВЫЗ", "calc"]], cap);
    TB.grid(L.plate, -.511, -79.25, 2, .05, .038, .056, .046,
      [["ПРОГРАД", "orient"], ["РЕТРОГРАД", "orient"], ["НОРМ. +", "orient"], ["НОРМ. −", "orient"], ["РАД. +", "orient"], ["РАД. −", "orient"], ["ГОРИЗОНТ", "orient"], ["СТОП ВРАЩ.", "orient"], ["ВЫСОТА", "orient"], ["РУЧН.", "orient", AMB]],
      {paint: TB.paintOrient, side: Mt.dark, h: .008});
    TB.grid(L.plate, -.584, -79.25, 1, .05, .038, .056, .046,
      [["МЕХАН.", "sys"], ["ТЕПЛО", "sys"], ["АВТОП.", "sys"], ["ПОЛЁТ", "sys"], ["ДВИГАТ.", "sys"]], {paint: TB.paintLampKey, style: "aes", side: Mt.key});
    A.beh.bind(BT.keys);
    for (const k of BT.keys) if (k.label === "РУЧН." || k.label === "ПОЛЁТ") setKey(k, true);
    A.drawAll();
    return {onKey: A.beh.onKey, step: A.step, cutMats: A.cutMats, cut: A.cut, repaint() { BUILT.V2 && BUILT.V2.keys.forEach(paintKey); A.drawAll(); }};
  }});
