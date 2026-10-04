// VARIANT 5 · two-tone (white desk, grey metal pods and trim). The orientation controller is a CENTRE STICK on the seat itself,
// standing at the front of the cushion between his knees (as in an aircraft): it rides with the seat, so the travel never
// pushes him into it; РУД on the left pod. The right pod carries the machine's keypad, the left the orientation and system keys.
VARIANTS.push({id: "V5", name: "5 · двухцвет · ручка на кресле", info: "Двухцветный: белый стол, серый металл пультов и окантовок. РУО — центральная ручка на самом кресле, на переднем краю сиденья между коленями (как в самолёте): едет вместе с креслом, упереться в неё нельзя; рукоять на 0,30 м над подушкой, 0,27 м впереди таза — в досягаемости и 5 % женщины. РУД — на левом пульте подлокотника. Правый пульт — клавиатура машины, левый — ориентация и вызов систем.",
  build(ctx) {
    const Mt = TB.M("two"), A = TB.assemble(ctx, Mt);
    const R = TB.pod(ctx, 1, Mt, .66), L = TB.pod(ctx, -1, Mt, .62);
    { // the centre stick: its gaiter on the cushion's front edge (z 79.24 at rest), the grip leaning forward
      const g = TB.ruo(ctx.seatGroup, 0, 1.635, 79.22, Mt); g.scale.set(1, 1.55, 1); g.rotation.x = -.12;
    }
    TB.rud(ctx.seatGroup, -.36, L.top, 79.27, Mt, "t");
    TB.grid(R.plate, .445, -79.25, 5, .038, .036, .044, .046,
      [["7", "calc"], ["8", "calc"], ["9", "calc"], ["÷", "calc"], ["C", "calc", RED], ["4", "calc"], ["5", "calc"], ["6", "calc"], ["×", "calc"], ["±", "calc"], ["1", "calc"], ["2", "calc"], ["3", "calc"], ["−", "calc"], ["ЗАП", "calc"],
       ["0", "calc"], [",", "calc"], ["=", "calc", AMB], ["+", "calc"], ["ВЫЗ", "calc"]], {paint: TB.paintCap, style: "metal", side: Mt.key});
    TB.grid(L.plate, -.511, -79.25, 2, .05, .038, .056, .046,
      [["ПРОГРАД", "orient"], ["РЕТРОГРАД", "orient"], ["НОРМ. +", "orient"], ["НОРМ. −", "orient"], ["РАД. +", "orient"], ["РАД. −", "orient"], ["ГОРИЗОНТ", "orient"], ["СТОП ВРАЩ.", "orient"], ["ВЫСОТА", "orient"], ["РУЧН.", "orient", AMB]],
      {paint: TB.paintOrient, side: Mt.dark, h: .008});
    TB.grid(L.plate, -.584, -79.25, 1, .05, .038, .056, .046,
      [["МЕХАН.", "sys"], ["ТЕПЛО", "sys"], ["АВТОП.", "sys"], ["ПОЛЁТ", "sys"], ["ДВИГАТ.", "sys"]], {paint: TB.paintLampKey, style: "metal", side: Mt.key});
    A.beh.bind(BT.keys);
    for (const k of BT.keys) if (k.label === "РУЧН." || k.label === "ПОЛЁТ") setKey(k, true);
    A.drawAll();
    return {onKey: A.beh.onKey, step: A.step, cutMats: A.cutMats, cut: A.cut, repaint() { BUILT.V5 && BUILT.V5.keys.forEach(paintKey); A.drawAll(); }};
  }});
