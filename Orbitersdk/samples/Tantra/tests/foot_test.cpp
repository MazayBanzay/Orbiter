// Offline check of core/Foot: the cells, the ribs, the ankle, the stage joint.
#include "../core/Foot.h"

#include <cstdio>
#include <cstring>

using namespace tantra::foot;

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

static void Fill(LegIn* in, int leg, double force, double vin, double pen, double stroke) {
    in[leg].inPlay = true;
    for (int c = 0; c < kCells; ++c) in[leg].cell[c] = {true, force, vin, pen, stroke};
}

int main() {
    LegIn in[kLegs];
    Feet f;
    // a blade standing loaded at 1 g: 192 MN on 12 cells - nothing happens
    Fill(in, 0, 192e6 / 12, 0.0, 0.45, 1.5);
    for (int i = 0; i < 1000; ++i) f.Step(0.02, in, true);
    CHECK(f.CellsAlive(0) == 12 && !f.LegBroken(0));
    // one cell on a rock with twice its design force: it tears at once, its rib then bears
    in[0].cell[3].force = 2.0 * kRating[kBlade].cell;
    f.Step(0.02, in, true);
    CHECK(f.CellLost(0, 3) && !f.RibLost(0, 3));
    // the bare rib with 1.6 x the cell's force breaks at the hinge
    in[0].cell[3].force = 1.6 * 1.5 * kRating[kBlade].cell;
    f.Step(0.02, in, true);
    CHECK(f.RibLost(0, 3));
    Event ev[32];
    const int n = f.TakeEvents(ev, 32);
    CHECK(n == 2 && ev[0].kind == kCellBurst && ev[1].kind == kRibBroken && ev[0].cell == 3);
    char buf[160];
    Feet::Describe(ev[0], "Лопасть левая", true, buf, sizeof buf);
    std::printf("%s\n", buf);
    // a hard landing that bottoms the stern cells out at 3 m/s bursts them
    Fill(in, 2, 20e6, 3.0, 1.0, 1.0);
    f.Step(0.02, in, true);
    CHECK(f.CellsAlive(2) == 0);
    // the kangaroo overloaded as a whole: the ankle goes at 1.6 x its load
    Fill(in, 6, 1.6 * kRating[kKang].ankle / 12, 0.0, 0.3, 1.0);
    in[6].cell[0].force = 1.6 * kRating[kKang].ankle / 12;   // every cell at 1.6x its own force too
    f.Step(0.02, in, true);
    CHECK(f.AnkleLost(6) && f.LegBroken(6));
    // the stage joint by the fibre sensors
    Fill(in, 1, 1e6, 0.0, 0.1, 1.5);
    in[1].ratio = 1.6;
    f.Step(0.02, in, true);
    CHECK(f.CollarLost(1));
    // damage off: nothing breaks
    Feet g;
    Fill(in, 0, 10.0 * kRating[kBlade].cell, 9.0, 1.5, 1.5);
    g.Step(0.02, in, false);
    CHECK(g.CellsAlive(0) == 12);
    // save / load round trip
    char s[400];
    f.Save(s, sizeof s);
    Feet h;
    h.Load(s);
    CHECK(h.CellLost(0, 3) && h.RibLost(0, 3) && !h.CellLost(0, 4) && h.AnkleLost(6) && h.CollarLost(1) && h.CellsAlive(2) == 0);
    std::printf("%s\n", s);
    std::printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
