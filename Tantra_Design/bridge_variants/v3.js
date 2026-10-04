// VARIANT 3 · gunmetal with a YOKE («рога») for the atmosphere and space: its column comes out of the desk's inner face only
// after the seat has stopped, and stops at his knees + 0.16 m (0.62 m ahead of the hip for a 95 % man, 0.52 for a 5 % woman);
// while the seat travels the yoke is stowed against the desk, so the seat can never push him into it. The orientation keys are
// on the yoke's hub (under the thumbs); РУД and the system keys on the left pod, the machine's keypad on the right pod.
VARIANTS.push({id: "V3", name: "3 · металл · штурвал", info: "Тёмный металл. Штурвал «рога»: колонка выходит из внутренней стенки стола только когда кресло встало, и останавливается в 16 см перед коленями (0,62 м от таза у 95 % мужчины, 0,52 м у 5 % женщины); пока кресло едет — штурвал убран к столу, упереться некуда. Хаб ниже линии взгляда на нижний край переднего стекла. Клавиши ориентации — на хабе под большими пальцами; РУД и вызов систем — левый пульт подлокотника, клавиатура машины — правый. Клавиш 33.",
  build(ctx) {
    const Mt = TB.M("metal"); Mt.desk.color.setHex(0x434b52); Mt.panel.color.setHex(0x3b4248);
    const yoke = {d: 0};
    const A = TB.assemble(ctx, Mt, {step: dt => {
      const want = ctx.SEAT.target === 1 && ctx.SEAT.cur > .985 ? 1 : 0;   // out only when the seat has stopped
      yoke.d += Math.sign(want - yoke.d) * Math.min(Math.abs(want - yoke.d), dt / (want ? 1.6 : .5));
      const e = yoke.d * yoke.d * (3 - 2 * yoke.d), hip = 78.95 + ctx.SEAT.offset(), ext = MAN.show === "f05" ? .52 : .62;
      const hz = 80.62 - e * (80.62 - (hip + ext)), B = V3(0, 1.60, 80.66), H = V3(0, 1.92, hz), dir = H.clone().sub(B);
      col.scale.y = Math.max(.01, dir.length()); col.position.copy(B).addScaledVector(dir, .5); col.quaternion.setFromUnitVectors(new THREE.Vector3(0, 1, 0), dir.normalize());
      hub.position.copy(H);
    }});
    const col = new THREE.Mesh(new THREE.CylinderGeometry(.028, .028, 1, 18), Mt.trim); ctx.group.add(col);
    const hub = new THREE.Group(); hub.rotation.x = -.6; ctx.group.add(hub);
    hub.add(new THREE.Mesh(new THREE.BoxGeometry(.27, .07, .05), Mt.panel));
    for (const s of [-1, 1]) {   // the horns: out and down to the grips
      const h = new THREE.Mesh(new THREE.CylinderGeometry(.016, .016, .1, 12), Mt.grip); h.position.set(s * .165, -.015, 0); h.rotation.z = s * 1.1; hub.add(h);
      const g = new THREE.Mesh(new THREE.CylinderGeometry(.019, .019, .085, 14), Mt.grip); g.position.set(s * .205, -.07, 0); hub.add(g);
      const cap = new THREE.Mesh(new THREE.SphereGeometry(.02, 12, 8), Mt.grip); cap.position.set(s * .205, -.03, 0); hub.add(cap);
    }
    const face = new THREE.Group(); face.position.set(0, 0, .026); face.rotation.x = Math.PI / 2; hub.add(face);
    TB.grid(face, -.087, -.018, 4, .052, .03, .058, .036,
      [["ПРОГРАД", "orient"], ["РЕТРОГРАД", "orient"], ["НОРМ. +", "orient"], ["НОРМ. −", "orient"], ["РАД. +", "orient"], ["РАД. −", "orient"], ["ГОРИЗОНТ", "orient"], ["СТОП ВРАЩ.", "orient"]],
      {paint: TB.paintOrient, side: Mt.dark, h: .006});
    const R = TB.pod(ctx, 1, Mt, .62), L = TB.pod(ctx, -1, Mt, .56);
    TB.rud(ctx.seatGroup, -.36, L.top, 79.27, Mt);
    TB.grid(R.plate, .445, -79.25, 4, .038, .036, .044, .046,
      [["7", "calc"], ["8", "calc"], ["9", "calc"], ["÷", "calc"], ["4", "calc"], ["5", "calc"], ["6", "calc"], ["×", "calc"], ["1", "calc"], ["2", "calc"], ["3", "calc"], ["−", "calc"],
       ["0", "calc"], [",", "calc"], ["=", "calc", AMB], ["+", "calc"], ["C", "calc", RED], ["±", "calc"], ["ЗАП", "calc"], ["ВЫЗ", "calc"]], {paint: TB.paintCap, style: "metal", side: Mt.key});
    TB.grid(L.plate, -.48, -79.25, 1, .05, .038, .056, .046,
      [["МЕХАН.", "sys"], ["ТЕПЛО", "sys"], ["АВТОП.", "sys"], ["ПОЛЁТ", "sys"], ["ДВИГАТ.", "sys"]], {paint: TB.paintLampKey, style: "metal", side: Mt.key});
    A.beh.bind(BT.keys);
    for (const k of BT.keys) if (k.label === "ПОЛЁТ") setKey(k, true);
    A.drawAll();
    return {onKey: A.beh.onKey, step: A.step, cutMats: A.cutMats, cut: A.cut, repaint() { BUILT.V3 && BUILT.V3.keys.forEach(paintKey); A.drawAll(); }};
  }});
