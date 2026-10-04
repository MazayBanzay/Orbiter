// VARIANT 1 · military grey metal (hammer-tone enamel, cast trim). The hand controllers sit on pods over the seat's armrests
// and ride with the seat, so its 0.6 m travel (+ adj) can never push him into them: РУО (orientation) under the right hand,
// РУД (translation) under the left. Right pod: the machine's keypad; left pod: the photonic orientation keys + the system keys
// (they call the system's page onto the screens). The desk is for looking: the instruments under the front glass, the state
// lamps on the left, the machine's own phosphor screen and the plant on the right.
VARIANTS.push({id: "V1", name: "1 · металл · РУО/РУД", info: "Серый военный металл. РУО (правая рука) и РУД (левая) на пультах подлокотников — едут вместе с креслом, поэтому подъезд кресла никогда не упирает человека в рули. Правый пульт: клавиатура расчётной машины; левый: фотонные клавиши ориентации и вызов систем на экраны. Стол — для слежения: под передним стеклом авиагоризонт, скорость, высота, температуры камер К1–К4, поле, тяга; слева — лампы механизации; справа — люминофорный экран оптической расчётной машины (она стоит в столе прямо под ним) и энергоустановка. Клавиш 35 (было 131), рычага нет.",
  build(ctx) {
    const Mt = TB.M("metal"), A = TB.assemble(ctx, Mt);
    const R = TB.pod(ctx, 1, Mt, .62), L = TB.pod(ctx, -1, Mt, .62);
    TB.ruo(ctx.seatGroup, .36, R.top, 79.28, Mt);
    TB.rud(ctx.seatGroup, -.36, L.top, 79.28, Mt);
    const cap = {paint: TB.paintCap, style: "metal", side: Mt.key};
    TB.grid(R.plate, .445, -79.25, 4, .038, .036, .044, .046,
      [["7", "calc"], ["8", "calc"], ["9", "calc"], ["÷", "calc"], ["4", "calc"], ["5", "calc"], ["6", "calc"], ["×", "calc"], ["1", "calc"], ["2", "calc"], ["3", "calc"], ["−", "calc"],
       ["0", "calc"], [",", "calc"], ["=", "calc", AMB], ["+", "calc"], ["C", "calc", RED], ["±", "calc"], ["ЗАП", "calc"], ["ВЫЗ", "calc"]], cap);
    const ori = {paint: TB.paintOrient, side: Mt.dark, h: .008};
    TB.grid(L.plate, -.511, -79.25, 2, .05, .038, .056, .046,
      [["ПРОГРАД", "orient"], ["РЕТРОГРАД", "orient"], ["НОРМ. +", "orient"], ["НОРМ. −", "orient"], ["РАД. +", "orient"], ["РАД. −", "orient"], ["ГОРИЗОНТ", "orient"], ["СТОП ВРАЩ.", "orient"], ["ВЫСОТА", "orient"], ["РУЧН.", "orient", AMB]], ori);
    TB.grid(L.plate, -.584, -79.25, 1, .05, .038, .056, .046,
      [["МЕХАН.", "sys"], ["ТЕПЛО", "sys"], ["АВТОП.", "sys"], ["ПОЛЁТ", "sys"], ["ДВИГАТ.", "sys"]], {paint: TB.paintLampKey, style: "metal", side: Mt.key});
    A.beh.bind(BT.keys);
    for (const k of BT.keys) if (k.label === "РУЧН." || k.label === "ПОЛЁТ") setKey(k, true);
    A.drawAll();
    return {onKey: A.beh.onKey, step: A.step, cutMats: A.cutMats, cut: A.cut, repaint() { BUILT.V1 && BUILT.V1.keys.forEach(paintKey); A.drawAll(); }};
  }});
