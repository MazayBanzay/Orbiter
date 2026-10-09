# OrbiterCrew - boarding other ships

An OrbiterCrew person can board ships that are not part of OrbiterCrew: the stock Delta Glider, and any other ship. The ship's own module is never changed.

## How it works

- Walk up to the ship and look at its airlock (or docking port) within reach. Its actions show in the menu beside the aim (mouse wheel picks, F does; Alt shows the places of actions as orange squares).
- **open / close the airlock** - OrbiterCrew presses the ship's own keys for you (as if you pressed them with the ship in focus) and reads its door animations back.
- **go in** (the airlock open) - the person is aboard: the ship gets the focus and the view, and you fly it as usual. The person breathes the cabin air.
- **Getting out** - press **F** with the ship in focus, standing on the ground with the airlock open. The person steps out at the airlock and gets the focus back.

## Any ship, with no file

A ship with no file of its own (see below) can be boarded at its **first docking port**: the menu shows "go in" there (no doors to work). Classes with a menu of their own are left out in `Config\OrbiterCrew\Ships\_default.cfg`:

```
EXCLUDE OrbiterCrew\ TVehicles\ MPU EMPU Tantra    ; class names that start so
OFF                                                ; (on a line of its own) no default boarding at all
```

## Adding a ship: one file

Create `Config\OrbiterCrew\Ships\<ClassName>.cfg`, where `<ClassName>` is the ship's class name (the part after the colon in a scenario line, e.g. `GL-01:DeltaGlider` -> `DeltaGlider.cfg`; a class in a subfolder, `Vendor\Ship`, goes to `Ships\Vendor\Ship.cfg`). UTF-8 text; `;` starts a comment.

| Line | Meaning |
|---|---|
| `LABEL` | The place's name in the menu: the Russian name, a vertical bar, the English one (shown with `LANGUAGE en`) |
| `NODE x y z` | The airlock's outer opening, ship frame, metres |
| `REACH r` | How far from the person's eyes it can be used, metres |
| `OPEN K 17 O 11` | To open: pairs of a key (letter A-Z, pressed on the ship) and the animation it moves |
| `CLOSE O 11 K 17` | To close, the same way |
| `IN 11 17` | The animations that must be open to go in and out |
| `EXIT x y z` | Where the person steps out, ship frame (put on the ground under it) |

Without `OPEN` the menu shows only "go in".

The animation numbers are the ship's own: the order in which its module creates its animations (`CreateAnimation`), from 0. They can be found in the ship's source, or from its author.

### Example: the stock Delta Glider

```
LABEL носовой шлюз | nose airlock
NODE 0 -0.49 10.40
REACH 3.2
OPEN K 17 O 11        ; K the nose cone (animation 17), O the outer door (11)
CLOSE O 11 K 17
IN 11 17
EXIT 0 0 12.5
```

## Notes

- Out only on the ground, stopped, with the airlock open.
- A person aboard is saved with the scenario (`RIDE <ship name>` in the person's block) and is aboard again when it is loaded.
- Scenario to try: **MPU - Earth, KSC, Delta Glider**.
