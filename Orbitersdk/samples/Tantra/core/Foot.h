// Tantra core: the feet and the joints of the legs. No Orbiter dependencies (tests/foot_test.cpp).
//
// Every foot stands on 12 separate rigid petals, each on its own hinge at the hub and its own MR strut from the collar
// under the ankle (DESIGN_LOCAL «Стопа: раздельные лепестки»). The struts are the gear's suspension; the post is solid.
// What breaks, joint by joint (a "cell" here = one petal's strut, a "rib" = the petal on its hinge):
//   strut  - over its design load (the leg's 2.5 g x 1.5 load / 12 at the petal) it fails; bottomed out faster than
//            2.5 m/s it hits its end stop and breaks. The petal then sits on its stop: rigid, the full stroke up.
//   petal  - a petal on its stop breaks at its hub hinge over 1.5 x the strut's load, or when it hits the ground
//            fast; it then carries nothing.
//   ankle  - the post and its ball over the leg's design load (fatigue over 1x, at once over 1.5x): the foot is lost.
//   collar - the lowest stage joint of the leg (the fibre sensors' load / rating, as before): the leg folds there.
#pragma once
#include <cmath>

#include "Spec.h"

namespace tantra::foot {

constexpr int kLegs = 7, kCells = 12;   // legs: blade port, starboard, stern 0..3, kangaroo
enum Kind { kBlade, kStern, kKang };
inline Kind KindOf(int leg) { return leg < 2 ? kBlade : (leg == 6 ? kKang : kStern); }

struct Rating {
    double cell;     // design force of one cell [N]
    double ankle;    // design load of the ankle (the whole foot) [N]
    int sections;    // telescopic sections of the leg (the lowest joint is between the last two)
};
// (2026-10-09) the feet carry the full ship like their columns (Spec kFullMass at 2.5 g x1.5; they had stayed at the old
// 722 / 481 / 103 MN and broke at once under 276 kt on Earth): a blade foot the turn on the blades alone (W/2), a stern foot
// standing on four with the splay, the front support its share of the tripod; the MR struts of the petals run ~7x the
// old force at the same size (the gas column and the valve at ~300 MPa)
constexpr double kFootW = spec::kFullMass * 9.80665 * spec::kDesignG * spec::kSafety;
constexpr double kBladeFoot = kFootW / 2.0, kSternFoot = kFootW / 4.0 / 0.9613, kKangFoot = kFootW * (58.1 - 53.4) / (111.4 - 53.4);
constexpr Rating kRating[3] = {{kBladeFoot / kCells, kBladeFoot, 6}, {kSternFoot / kCells, kSternFoot, 4}, {kKangFoot / kCells, kKangFoot, 9}};
constexpr double kBurstV = 2.5;   // bottomed out faster than this: the rim pinches the cell [m/s]
constexpr double kRibV = 5.0;     // a bare rib hitting the ground faster than this breaks [m/s]

struct CellIn {
    bool on = false;       // this cell's pad is on the ground this step
    double force = 0.0;    // what it carries [N]
    double vin = 0.0;      // compression speed [m/s], + in
    double pen = 0.0;      // compression [m] (0 unloaded .. stroke bottomed)
    double stroke = 1.0;
};
struct LegIn {
    bool inPlay = false;   // the leg is in the ground set and on the ground
    double ratio = 0.0;    // the leg's load / rating from the fibre sensors
    CellIn cell[kCells];
};

enum EventKind { kCellBurst, kRibBroken, kAnkleBroken, kCollarBroken };
struct Event { int leg, cell, kind; };

class Feet {
public:
    Feet() { Repair(); }
    // enabled = Orbiter's damage setting: nothing breaks without it
    void Step(double dt, const LegIn* legs, bool enabled);
    void Repair();

    bool CellLost(int l, int c) const { return cell_[l][c] <= 0.0; }
    bool RibLost(int l, int c) const { return rib_[l][c] <= 0.0; }
    double Cell(int l, int c) const { return cell_[l][c]; }
    bool AnkleLost(int l) const { return ankle_[l] <= 0.0; }
    bool CollarLost(int l) const { return collar_[l] <= 0.0; }
    bool LegBroken(int l) const { return AnkleLost(l) || CollarLost(l); }
    int CellsAlive(int l) const;

    int TakeEvents(Event* out, int max);
    // "Лопасть левая: ячейка 5 разорвана" - the leg's name is passed in (the damage model names the legs)
    static void Describe(const Event& e, const char* legName, bool ru, char* buf, int size);

    // persistence: "cells ribs ankles collars", each integrity 0..9 as one digit
    void Save(char* buf, int size) const;
    void Load(const char* s);

private:
    void Push(int leg, int cell, int kind);
    double cell_[kLegs][kCells], rib_[kLegs][kCells], ankle_[kLegs], collar_[kLegs];
    Event events_[32];
    int nEvents_ = 0;
};

}  // namespace tantra::foot
