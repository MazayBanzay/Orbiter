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

namespace tantra::foot {

constexpr int kLegs = 7, kCells = 12;   // legs: blade port, starboard, stern 0..3, kangaroo
enum Kind { kBlade, kStern, kKang };
inline Kind KindOf(int leg) { return leg < 2 ? kBlade : (leg == 6 ? kKang : kStern); }

struct Rating {
    double cell;     // design force of one cell [N]
    double ankle;    // design load of the ankle (the whole foot) [N]
    int sections;    // telescopic sections of the leg (the lowest joint is between the last two)
};
// blades 722 MN, stern legs 481 MN (2.5 g x 1.5 of the full mass, standing on four), kangaroo 103 MN (its column)
constexpr Rating kRating[3] = {{722e6 / kCells, 722e6, 6}, {481e6 / kCells, 481e6, 4}, {103e6 / kCells, 103e6, 9}};
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
