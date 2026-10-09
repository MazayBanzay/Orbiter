# Tantra crew zone: living space for 14 by the norms, the current deficit, and how to close it without touching the hangar or the CG

**Summary.**
- **Building forward does not solve this.** The radiation screen fills s 144–152. Everything forward of s 152 is on the motor side of it.
- **The shortfall comes from the layout, not from a lack of hull.**
  - The lower deck has 5.27 m of clear height, the upper deck 4.28 m.
  - Adding one mezzanine floor inside the lower deck gives about 686–700 m² in the same hull. Today there are 443–448 m².
  - That is enough for the recommended norm for 14 people.
- **The hangar stays as it is.**
- **The CG is held** by moving about 13–37 t of keel-bay equipment to the stern. With that move, the dry CG ends up unchanged (0.000 m).

**Process note.**
- No project files were changed, nothing was built and Orbiter was not launched.
- The verification passes left files outside the project:
  - pdftotext extracts and copies of `gen_mesh.py` / `_hull_profile.json`, plus scripts `q1–q7.py`, in `C:\Temp\claude\C--Games-orbiter100830\c23f616b-87c9-4f49-82e2-e5dfc0b904f9\scratchpad\` (`gm\` subfolder for the copies and scripts);
  - two PDFs (MLC 2006 and the NASA NHV report) that WebFetch saved automatically under `...\c23f616b-...\tool-results\`.
- One slow background calculation from the geometry pass may still be running. It stops when that session ends.

## 0. Basis and assumptions

- **Coordinates.** s = station from the stern, measured forward. "CG" means the s-station of the centre of mass.
- **CG model.** This is `Tantra::UpdateCG` (orbiter2016/Tantra.cpp:987–1013), with defaults from core/Spec.h:104–110 and Params.h:16–20. Every key in Tantra.cfg is commented out. All five states below were re-checked:

  | State | Mass | CG |
  |---|---|---|
  | Dry | 4 203 t | s 65.60 |
  | Empty with traps | 4 859 t | s 64.07 |
  | Loaded | 52 339 t | s 58.05 |
  | c1: traps empty, argon and iron aboard | 14 859 t | s 67.51 |
  | c2: traps empty, iron used, argon full | 11 059 t | s 72.19 |

- **Norm basis.** Artificial gravity of about 1 g, so floor area is what governs. The maritime size class used is ≥10 000 GT.
- **Floor area to volume.** Net habitable volume (NHV) = floor × 2.1 m × 0.65. This is my own convention, not a codified figure.
- **Fit-out mass.** 0.18–0.36 t/m², nominal 0.27 t/m². A floor slab with services alone is 0.07–0.12 t/m². These are assumptions.
- **The dry CG of s 65.6 cannot be reproduced.**
  - Rebuilding it from the Spec.h budget gives **s 62.3–64.1**, an uncertainty of 2–3 m.
  - Reaching s 65.6 would need the 486 t "crew and systems" item at s ≈152.6, which is not plausible.
  - So "CG unchanged" below means unchanged relative to the code model.
  - Each tonne of error in the as-built mass moves the dry CG by ±0.015 m.
- **The screen core is not in the mass model.**
  - The core at s 144.8–151.2 is about **963–966 m³**. The two iridium plates add another 256 m³.
  - If the core were water at about 1 t/m³, it would move the dry CG by **+15.4 m** and the loaded CG by **+1.63 m**. The plates would add thousands of tonnes.
  - This outweighs everything else in this report and needs its own decision.
- **State c2 is already outside two limits, before any habitat is added.**
  - Its CG is at s 72.19.
  - Full standing height needs CG ≤ s 70.0 (Carriage.cpp:136–137, with footH 5.2 from Tantra.cpp:193).
  - The trunnion track ends at s 71.6 (Carriage.h:36).
  - So any mass added forward must be compensated.

## 1. Requirements for 14 people vs. usable today

**What counts as "usable today":**
- **Lower deck counts as 0.** It has 14 shaft cabins about 1.71 m wide with a ladder up to the bed, wet rooms inside the sleeping ring, and hangar traffic passing through.
- **Keel bay is not habitable** (1.78 m clear).
- **Only upper-deck rooms count**, minus 13.6 m² with headroom under 2.1 m.
- **Left out of the norm:** the EVA rooms (suit-up 16.6 m² + lift zone 18.7 m² = 35.3 m²) and the bridge (about 60 m²).

| # | Function | Min, m² | Rec., m² | Source | Usable today, m² | Short of min | Short of rec. |
|---|---|---|---|---|---|---|---|
| 1 | Private cabins ×14 | 98 (14×7) | 140 (14×10) | MLC A3.1 §9(f)(iii) / §9(k)(iii) ✔ | 0 | 98 | 140 |
| 2 | Day rooms ×3 (master, chief engineer, chief navigator) | 18 | 24 | MLC §9(m) ✔ (no area given; my estimate) | 0 | 18 | 24 |
| 3 | Mess, 14 seated | 21 | 27 | MLC B3.1.6 §3, 1.5 m²/seat ✔ | 27.8 | 0 | 0 |
| 4 | Galley | 10 | 15 | estimate | 28.6 | +18.6 surplus | +13.6 surplus |
| 5 | Recreation | 17.5 | 43 | ABS 1.25 m²/seat ✘ unverified | 34.7 | 0 | 8.3 |
| 6 | Exercise + sauna | 15 | 32 | NASA: 2 devices per 6 crew ✔, scaled; sauna SP 2641-82 ✘ | 0 | 15 | 32 |
| 7 | Sanitary | 16 | 52 (en-suite) | MLC §11(c): 1 set per ≤6 ✔; men/women separate §11(a) ✔; unit areas SP 2641-82 ✘ | 0 | 16 | 52 |
| 8 | Medical bay | 25.5 | 43 | 46 CFR 72.20-35 ✔ requires a hospital, but gives **no area**; areas from SP 2641-82 only ✘ | 28.6 | 0 | 14.4 |
| 9 | Laundry | 4 | 6 | MLC §13 ✔ (no area given; my estimate) | 0 | 4 | 6 |
| 10 | Office / workspace | 10 | 20 | MLC §15 ✔; NASA 4 of 6 crew working at once ✔ | 23.6 | surplus | surplus |
| — | Circulation (+20 %) | 47 | 80 | estimate | 39.2 | 7.8 | 40.8 |
| | **Total** | **282** | **482** | | **182.5** | **158.8** | **317.5** |

✔ = checked against the source text. ✘ = could not be checked.

Notes on the table:
- **Medical bay, two legal points.**
  - Under MLC §12 a hospital is mandatory only from 15 seafarers.
  - Under 46 CFR, the hospital-berth rule (1 per 12 crew *not in single rooms*) gives 0 berths when all 14 have single cabins.
  - The medical figures are therefore design choices, not legal minimums.
- **Gross vs. net deficit.**
  - Net, after trimming the galley and reassigning its surplus: about **100 m² short of the minimum and about 300 m² short of the recommended level**.
  - I use the gross figures (≈160 / ≈318 m²) as the design target.
- **Which column should apply.** The recommended one, because the crew has no shore leave for years.
- **Stowage is not in the norm table.**
  - Today: 21.6 m² of keel stores at 1.78 m, plus the workshop at 21.6 m² (43.1 m² together), plus about 1 m³ of wardrobe per cabin.
  - I add an assumed **≈30 m²** of on-deck stowage.

**Net habitable volume:**

| | Value |
|---|---|
| Today | 182.5 × 2.1 × 0.65 ≈ **249 m³** (17.8 m³ per person) |
| Required, minimum | 385 m³ (NASA HRP minimum is 25 m³ × 14 = 350 m³ ✔) |
| Required, 3–5 years | 424–459 m³ (extrapolated NASA fit ✘) |
| Required, recommended | ≈658 m³ (about 47 m³ per person, "Mir level" ✘) |
| **Shortfall** | **≈136 m³ to the minimum, ≈175–210 m³ to the 3–5-year level, ≈410 m³ to the recommended level** |

**How much of the lower deck can be saved:**
- **As built: nothing usable for sleeping.** About 47 m² (gym 20.2 + hygiene 26.9) can still serve its own function once the cabins leave the deck.
- **By re-decking: all of it.**
  - The 5.27 m clear height holds two levels.
  - The floor is 224 m². The second level at y ≈ −1.6 is 271 m² gross (253 m² where it is narrowest), about **245 m² net**. The hull bulges, so this level is wider than the floor.
  - Together that is about 470 m², against today's 224 m² of floor plus 130 m² of lofts.

## 2. Expansion paths

Line references are to tools\gen_mesh.py.

### (a1) Mezzanine in the lower deck, upper deck unchanged — RANK 1

**Where.** s 121.6–134 (D_S0/D_S1, :1646). One new slab inside the lower deck (L_Y −4.5..1.0, :1644). Clear height between the existing floor top (−4.425) and the ceiling underside (0.84) is 5.265 m.

| Slab thickness | New slab top | New slab underside | Clear height per level |
|---|---|---|---|
| 0.15 m | −1.72 | −1.87 | 2.56 m |
| 0.30 m | −1.64 | −1.94 | 2.48 m |

Both heights exceed the MLC 2.03 m headroom (§6(a)) and the 2.4 m needed for a treadmill or a surgical lamp.

**Area gained.**
- By hull measure: ≈686–700 m² in total, against 443–448 m² today, so **+≈245–250 m²**.
  - Lower deck: 224 m² (existing floor) + ≈245 m² net (mezzanine).
  - Upper deck: 217 m².
- The mezzanine net figure already deducts about 10 m² of stair and lift wells and about 16 m² of hull edge with low headroom.
- For the norm functions (total minus 35 m² EVA): about **650 m² ≥ 482 recommended + 30 stowage**, a margin of about 135 m².
- NHV ≈ **887 m³**, above the recommended level.

**What must change.**
- Remove the 14 lofts, their ladders and the shaft partitions (129.7 m² of lofts).
- Rebuild the lower deck as two levels.
- Add one inner-lift stop at about −1.64. The existing stops at −4.425 / 1.075 / 5.575 (:2762) stay.
- **Stair: no extra flight is needed.** The existing stair already has a landing at y −1.675, s 121.65–122.6 (:2343–2349). Open that landing onto the mezzanine and cut a well of about 5.7 m² for the flights. A 0.3 m slab puts the floor 3.5 cm above the landing; a 0.15 m slab puts it 4.5 cm below.
- Rework the lighting: the night-material swap at :4627–4632 and the bed lamps at y 0.82 (5.25 m above the floor) go.

**What stays as it is.**
- Hangar-lock sill at −4.5.
- Bridge and main-airlock sills at 1.0, and LOCK_TOP 3.9 (:878).
- Feed lines at y 6.1.
- `tech_link` and the technical level.

**Mass.** +18–70 t at s ≈127.8, nominal 25 t.
- The low end (18.4 t) is the itemised minimum:
  - net new slab of about 134 m² × 0.07 t/m²: 9.4 t;
  - 14 en-suite units: 6 t;
  - extra medical and gym equipment: 3–5 t.
- The high end assumes all ~254 m² of new floor fully fitted at 0.27 t/m².

**CG effect without compensation:**

| Mass | Dry | Empty + traps | Loaded | c1 | c2 |
|---|---|---|---|---|---|
| 25 t | +0.368 m | +0.326 | +0.033 | +0.101 | +0.125 |
| 70 t | +1.019 m | +0.905 | +0.093 | +0.283 | +0.350 |

**Holding the CG.**
- Move **13.1 t (nominal) to 36.6 t (high)** of keel-bay equipment from s ≈139 to s ≈20, the aft end of the technical passage (:1519).
- If it goes to s 9 instead, 12.0–33.5 t is enough.
- Candidates: power-room equipment (26.9 m² room) and workshop spares. Both sit inside the unitemised "crew and systems 486 t".

**Residual CG change after the counter-move:**

| Case | Dry | Empty + traps | Loaded | c1 | c2 |
|---|---|---|---|---|---|
| Nominal | 0.000 m | +0.008 | +0.004 | −0.003 | −0.015 |
| High | 0.000 m | +0.022 | +0.010 | −0.009 | −0.041 |

**Risks.**
- Impact noise through the new slab: the gym must not sit under the cabins.
- The hull bulge narrows the ceiling edge of level 2.
- The lift needs a new stop.
- Nothing changes outside the crew zone: shielding, nose iridium, anamezon units, drum and gear pockets are all untouched.

**Hangar:** untouched.

### (g) Aft slice s 120.6–121.6 added to the crew zone — RANK 2 (optional reserve)

**Where.** From HANGAR_S end 120.6 (:868) to D_S0 121.6. The bulkhead goes at s 120.6, not 120.5, so the hangar structure is not touched. The hangar room itself ends at s 120.3 (:1521).

**Area gained.** About 268 m³ free (282 gross, minus the lock and `tech_link`). That is 18.6–25.8 m² per level, about **65 m² over 3 levels**, or about 85 m² with a2.

**What it must work around.**
- Hangar lock: x −1.35..0.45, y −4.5..−2.4.
- Feed lines from (±8.4, 6.1, s 121).
- `tech_link`: s 119–125, x −2.8..2.4, y 5.5–7.5.

**Mass and CG.** About 18 t at s ≈121.1. That moves the dry CG +0.237 m and the loaded CG +0.022 m. Holding it takes about 8.4 t moved from s 139 to s 20.

**Unknown.** Whether this gap is pressurised today.

**Use.** A boot / decontamination room at the hangar lock. It also takes hangar traffic out of the night zone.

### (e) Move technical rooms out of the keel bay or aft — RANK 3 (a CG tool, not a source of space)

**The keel bay can never be living space.**
- It spans s 134–143.8, |x| ≤ 6.2, y −4.9..−2.9 (:1647), with 1.78 m of clear height.
- It sits under the drum. The drum bottom (−2.6) is only 0.225 m above the bay's ceiling slab.
- Its forward bottom corner comes within about 0.18 m of the hull skin.
- Emptying it gains **0 m²** of habitable area.

**What it does provide.**
- A counter-moment of 119 t·m for every tonne moved from s 139 to s 20.
- Less machinery noise near the forward cabins.

**What goes where.**

| Item | Where | Why / effect |
|---|---|---|
| Life support | Stays forward | Short duct runs |
| Workshop + stores (43 m²) | Up onto the decks | About 20 t moved 14 m aft = 280 t·m |
| Power and power conversion | Stern | Main counter-move. The 2 GW plant is not in the mesh yet (DESIGN_LOCAL) |

### (a2) Four decks of 2.65 m clear — RANK 4 (only if more than a1 is wanted)

**Where.** s 121.6–134:

| Deck | Floor | Ceiling | Area, m² |
|---|---|---|---|
| 1 | −4.9 | −2.25 | ≈219 |
| 2 | −1.95 | 0.70 | ≈254 |
| 3 | 1.0 | 3.65 | ≈231 |
| 4 | 3.95 | 6.60 | ≈195 (measured at the 6.6 ceiling; 228 at the floor) |
| **Total** | | | **≈899 m² (+≈450 vs. today, ≈+200 vs. a1)** |

**Extra work and clashes beyond a1.**
- The upper deck is rebuilt too.
- The feed lines at (±5.6, 6.1) run through deck 4 and must be rerouted.
- `tech_link` is absorbed.
- The technical passage floor (5.5–6.0) arrives 1.55–2.05 m above the deck-4 floor, so a stair or ramp is needed.
- All four lift stops are new.
- LOCK_TOP 3.9 falls inside the 3.65–3.95 slab, so the slab needs a local cut-out.
- The hangar-lock sill (−4.5) is 0.4 m above the deck-1 floor (−4.9).
- The tall upper-deck rooms (mess, lounge) are lost.

**Mass and CG.** +30–110 t at s 127.8, nominal 45 t.

| Mass | Dry | Loaded | c2 | Counter-move (s 139 → 20) |
|---|---|---|---|---|
| 45 t (nominal) | +0.659 m | +0.060 m | — | 23.5 t |
| 110 t (high) | +1.586 m | — | +0.548 m | 57.5 t |

The norms do not need it: a1 already covers them.

### (c) Strips beside the bridge drum, s 134–143.8, from x 4.9 out to the skin — RANK 5

**Area gained.** Two levels per side, **97 m² in total**. The levels line up with the a1/a2 floors.

| Level | Area per side | Width, aft → forward |
|---|---|---|
| y −1.95..0.70 | 27.7 m² | 3.7 → 2.2 m |
| y 1.0..3.65 | 20.9 m² | 2.9 → 1.6 m |

**Use.** Stores, laundry or pharmacy only. The wedges above the drum hold nothing.

**Mass and CG.** About 15 t at s 139: dry +0.261 m, loaded +0.023 m.

**Risks.**
- **Drum rotation clearance is 0.10 m.** The drum is at |x| ≤ 4.8 (BR_HX, :1510). Any lining or frame on the inboard wall eats into this.
- Other drum clearances:
  - 0.54 m to the hull skin at the drum's top corners;
  - 0.25 m to the upper-deck forward wall;
  - 0.4 m to the screen.
- The feed lines at (±5.6, 4.5) over s 135.8–144.3 have their coils' underside at about 4.1. That is above the upper level and clear of it, but in the way of access.
- Access is only through the s 134 bulkhead.

### (d) Technical level, y 5.5–7.85, above the crew zone — RANK 6 (stowage only)

- **Volume:** 364 m³ gross, about 312 m³ net of `tech_link` and the feed lines.
- **Headroom:** at most about 2.3 m, on the centreline. **0 m² habitable.**
- **With a1** it stays as plenum and stowage space (wardrobes, bulk stores). Store mass moves aft from s 139 to s ≈128, a small help.
- **With a2** deck 4 takes it over.
- **The passage over the hangar** (|x| ≤ 0.55, s 20–119) is a 1.1 m-wide walkway with 2.0 m of headroom; its floor steps up to 6.0 at s 105–112. It is circulation, not living space and not hangar.

### (b) Forward of the keel bay, s 143.8–156 — RANK 7 (not recommended)

**What is there.**

| Stations | What | Usable? |
|---|---|---|
| s 143.8–144 | 33 m³ gap | Too small |
| s 144–152 | Full-section screen (:849), "between the crew and the nose motors" | **Not available** |
| s 152–154.6 | 422 m³ free (feed lines at x ±3, y −1) | **Motor side of the screen, not habitable** |

**The only forward option is to move the screen forward by up to 2.6 m.**
- MIRROR_S = (154.8, 155.6) are the ring centres. Each ring is ±0.2 m long (:4831), so the first ring's face is at s 154.6.
- A 2.6 m move therefore touches it with **zero margin**. The mass check's figure of "2.8 m with 0.2 m margin" ignored the ring length and is wrong.
- The rings are only annuli of R 2.2–2.8 m around (±3.0, −1.0); they do not fill the section.
- Gain: s 144–146.6, about 428 m³, roughly **120 m²** over 4 levels.

**Cost of moving the screen.**
- The crew moves 2.6 m closer to the dose traps at s 156.5 (:843).
- DESIGN_LOCAL.md:36 requires 15–20 m of shield + deflector + water/charges; this cuts into that margin.
- The new slice is reachable only past the 0.10 m drum clearance.

**CG.**
- Fit-out of about 32 t at s 145.3: dry +0.602 m, loaded +0.053 m.
- The screen itself costs +0.062 m dry for every 100 t of screen mass. That mass is **not in the model**; the plates and core as drawn (:4811–4827) would be thousands of tonnes.

**The nose (s 152–178)** is about 2 534 m³ (2 780 m³ to the skin), all on the motor side: traps, neck, R 2.2 cups, retro skin openings at s 165–171.5.

### (f) Lengthening the hull with an insert — RANK 8 (rejected)

- **Area per metre:** about 84 m² with 4 decks, about 44 m² with 2 decks.
- **Mass:** 10–14 t per metre, and everything ahead of the cut moves forward.
- **Example:** a 10 m insert at s 144 gives about dry +2.7…+3.5 m, loaded +0.23…+0.31 m, c2 +1.0…+1.27 m. These are plausible but cannot be checked exactly, because the mass ahead of the cut is not in the model.
- **Compensation:** about 100–120 t moved aft.
- It changes the hull and is not needed.

### Recommended combination: a1 + e, with g as an optional reserve

This allocation corrects the synthesis.
- The earlier plan grew the medical bay from the galley and the recreation room from the lab. In both cases the rooms are on opposite sides of the lobby or corridor, so they are not adjacent.
- That plan also left the office at 17.5–18.8 m², below the recommended 20 m².

| Space | Area, m² | Contents |
|---|---|---|
| **Level 2** (mezzanine; quiet night deck, no through traffic) | ≈245 | 14 cabins × 10 = 140; 14 en-suites × 3.5 = 49; corridor ≈45. Total 234, ≈10 spare |
| **Level 1** (existing lower floor) | ≈215 net | Gym + sauna 32 (under the level-2 corridor and stores, not under cabins); 3 day rooms 24; laundry 6; 2 public toilets 3; workshop + stores up from the keel 43; circulation ≈45; **medical annex 14.4** (second ward / isolation, next to the lift for stretchers); **recreation annex 8.3**. Total ≈176, ≈39 spare |
| **Upper deck** (kept, 216.5 usable) | 216.5 | Mess 27.8 (unchanged); galley cut to 15 usable; **freed galley ≈13.6 → pantry + on-deck stowage**; the remaining ≈16 m² of stowage goes into the level-1 spare; medical 28.6 (unchanged room); recreation 34.7; **lab and office 23.6 (kept whole, ≥20)**; toilet near the bridge 3.5 (MLC §11(b)) where the layout allows; EVA rooms unchanged |

An alternative to the split medical bay: move the whole bay (43 m²) to level 1 and turn the upper-deck medical room into recreation, so that recreation is in one place. That is a decision for you (item 4 below).

**Coverage.**
- Every row of the recommended column is met.
- Area: about 685–700 m² against 482 + 35 + 30 = 547 m² required, a margin of about 135–150 m².
- NHV: about 887 m³ against 658 m³ recommended.

**Mass budget.**
- a1: 25 t at s 127.8.
- g, if built: 18 t at s 121.1.
- Workshop and stores up from the keel: 20 t moved from s 139 to s 125.
- Net moment about the dry CG: 2 274 t·m with g, 1 275 t·m without.
- **Counter-move:** 19.1 t with g, or 10.7 t without, of power-room equipment from s 139 to s 20.

**Residual CG change:**

| | Dry | Empty + traps | Loaded | c1 | c2 |
|---|---|---|---|---|---|
| With counter-move | **0.000 m** | +0.013 | +0.006 | −0.006 | −0.026 |
| No counter-move, with g | +0.536 m | — | +0.050 | — | +0.179 |
| No counter-move, without g | +0.302 m | — | +0.028 | — | +0.100 |

The "no counter-move" rows already include the workshop move.

## 3. Decisions for you

1. **Norm level:** minimum (282 m²) or recommended (482 m²). I recommend the recommended level for a multi-year mission without shore leave.
2. **Re-decking variant:**
   - **a1:** 3 levels; the tall upper deck stays; no changes to feed lines, lock or lift top.
   - **a2:** 4 decks; about +200 m² more than a1; needs feed-line rerouting, has the 0.4 m lock step and the LOCK_TOP slab clash.
3. **a1 slab thickness:** 0.15 m (2.56 m clear per level) or 0.3 m (2.48 m clear).
4. **Medical bay:** split (28.6 m² upper + 14.4 m² annex on level 1), or the whole bay (43 m²) on level 1 with the upper-deck room given to recreation.
5. **Counter-move to the stern:** which equipment (power room, power conversion, spares; 11–37 t), and where it goes (s 20 or the stern frame at s 9).
6. **Slice g (s 120.6–121.6):** build the boot / decontamination room with the bulkhead at s 120.6? Whether the slice is pressurised today also needs confirming.
7. **Forward options (b, f):** I recommend rejecting them because of the screen, the zero ring margin and the dose margin. Do you agree?
8. **Mass-model housekeeping, separate from the layout:**
   - The dry CG of s 65.6 cannot be reproduced; the budget gives s 62.3–64.1.
   - Is the screen (core about 963–966 m³ plus 256 m³ of iridium plates) real mass?
   - State c2 is already past the s 70.0 and s 71.6 limits.

**Not verified:**
- ABS HAB++ figures (1.25 m² per recreation seat, §8.5).
- SP 2641-82 areas for sauna, wards and sanitary units.
- Mir level of 47 m³ per person.
- The NASA 3–5-year extrapolation.
- The fit-out t/m² values and the 2.1 × 0.65 NHV factor, which are my own assumptions.

**Files cited:**
- C:\Games\Orbiter-2024\Orbitersdk\samples\Tantra\tools\gen_mesh.py
  - :843, :849–853, :868, :878, :1509–1510, :1519–1528, :1644–1647, :2343–2349, :2762, :4627–4632, :4811–4832
- C:\Games\Orbiter-2024\Orbitersdk\samples\Tantra\core\Spec.h
- C:\Games\Orbiter-2024\Orbitersdk\samples\Tantra\core\Params.h
- C:\Games\Orbiter-2024\Orbitersdk\samples\Tantra\core\Carriage.h
- C:\Games\Orbiter-2024\Orbitersdk\samples\Tantra\core\Carriage.cpp
- C:\Games\Orbiter-2024\Orbitersdk\samples\Tantra\orbiter2016\Tantra.cpp
  - :193, :987–1013
- C:\Games\Orbiter-2024\Orbitersdk\samples\Tantra\orbiter2016\MeshLayout.h
- C:\Games\Orbiter-2024\Tantra_Design\DESIGN_LOCAL.md
  - :36

**Norm sources:**
- MLC 2006: https://www.ilo.org/sites/default/files/wcmsp5/groups/public/@ed_norm/@normes/documents/normativeinstrument/wcms_763684.pdf
- 46 CFR 72.20-35: https://www.law.cornell.edu/cfr/text/46/72.20-35
- NASA NTRS 20140016951: https://ntrs.nasa.gov/api/citations/20140016951/downloads/20140016951.pdf