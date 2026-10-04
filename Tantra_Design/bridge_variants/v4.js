// VARIANT 4 · white АЭС enamel, a HORSESHOE: two fixed side consoles stand at the seat's lane (x ±0.46…0.80, from z 79.30 to
// the desk's inner face), the seat travels between them (its armrests end at ±0.40: 6 cm clear) and the forearms lie on them.
// The side-stick (РУО) at the right console's front, РУД at the left one's (z 79.98): a 95 % man reaches them at adj 0, a 5 %
// woman at adj +0.20. The keys are beside him (z 79.44…79.62): the machine's keypad right, orientation + systems left.
TB.sideConsole = function (ctx, sx, Mt, top = 1.79) {
  const pts = [[.46, 79.30], [.80, 79.30]];
  for (let i = 0; i <= 12; i++) { const x = .80 - (.80 - .46) * i / 12; pts.push([x, 79.75 + Math.sqrt(.92 * .92 - x * x) - .005]); }
  const sh = new THREE.Shape(pts.map(([x, z]) => new THREE.Vector2(sx * x, z)));
  const m = new THREE.Mesh(new THREE.ExtrudeGeometry(sh, {depth: top - ctx.FLOOR, bevelEnabled: true, bevelThickness: .012, bevelSize: .012, bevelSegments: 3}), Mt.desk);
  m.rotation.x = -Math.PI / 2; m.position.y = ctx.FLOOR - .012; ctx.group.add(m);
  const plate = new THREE.Group(); plate.position.copy(V3(0, top, 0)); ctx.group.add(plate);
  return {m, plate, top};
};
TB.sideStick = function (parent, x, y, z, Mt) {   // a side-stick: a boot, a forward-canted grip with a hand rest
  const g = TB.ruo(parent, x, y, z, Mt); g.scale.set(1.1, 1.15, 1.1);
  const rest = new THREE.Mesh(new THREE.BoxGeometry(.07, .02, .16), Mt.rubber); rest.position.copy(V3(x, y + .01, z - .14)); parent.add(rest);
  return g;
};
VARIANTS.push({id: "V4", name: "4 · белый АЭС · подкова", info: "Белая эмаль АЭС. Подкова: два неподвижных боковых пульта по сторонам хода кресла (x ±0,46…0,80 м, от z 79,30 до стола); кресло едет между ними (подлокотники до ±0,40 — зазор 6 см), предплечья лежат на пультах. Боковая ручка РУО справа и РУД слева на передних концах пультов: 95 % мужчина достаёт при регулировке 0, 5 % женщина — при +0,20. Клавиши сбоку от человека: справа машина, слева ориентация и вызов систем. Стол — только приборы и лампы.",
  build(ctx) {
    const Mt = TB.M("aes"), A = TB.assemble(ctx, Mt);
    const R = TB.sideConsole(ctx, 1, Mt), L = TB.sideConsole(ctx, -1, Mt);
    A.cutMats.push(Mt.desk);
    TB.sideStick(ctx.group, .63, R.top, 79.98, Mt);
    TB.rud(ctx.group, -.63, L.top, 79.98, Mt, "t");
    TB.grid(R.plate, .50, -79.62, 4, .038, .036, .044, .046,
      [["7", "calc"], ["8", "calc"], ["9", "calc"], ["÷", "calc"], ["4", "calc"], ["5", "calc"], ["6", "calc"], ["×", "calc"], ["1", "calc"], ["2", "calc"], ["3", "calc"], ["−", "calc"],
       ["0", "calc"], [",", "calc"], ["=", "calc", AMB], ["+", "calc"], ["C", "calc", RED], ["±", "calc"], ["ЗАП", "calc"], ["ВЫЗ", "calc"]], {paint: TB.paintCap, style: "aes", side: Mt.key});
    TB.grid(L.plate, -.556, -79.62, 2, .05, .038, .056, .046,
      [["ПРОГРАД", "orient"], ["РЕТРОГРАД", "orient"], ["НОРМ. +", "orient"], ["НОРМ. −", "orient"], ["РАД. +", "orient"], ["РАД. −", "orient"], ["ГОРИЗОНТ", "orient"], ["СТОП ВРАЩ.", "orient"], ["ВЫСОТА", "orient"], ["РУЧН.", "orient", AMB]],
      {paint: TB.paintOrient, side: Mt.dark, h: .008});
    TB.grid(L.plate, -.69, -79.62, 1, .05, .038, .056, .046,
      [["МЕХАН.", "sys"], ["ТЕПЛО", "sys"], ["АВТОП.", "sys"], ["ПОЛЁТ", "sys"], ["ДВИГАТ.", "sys"]], {paint: TB.paintLampKey, style: "aes", side: Mt.key});
    A.beh.bind(BT.keys);
    for (const k of BT.keys) if (k.label === "РУЧН." || k.label === "ПОЛЁТ") setKey(k, true);
    A.drawAll();
    return {onKey: A.beh.onKey, step: A.step, cutMats: A.cutMats, cut: A.cut, repaint() { BUILT.V4 && BUILT.V4.keys.forEach(paintKey); A.drawAll(); }};
  }});
