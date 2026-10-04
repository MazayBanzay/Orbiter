#include "Foot.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace tantra::foot {

void Feet::Repair() {
    for (int l = 0; l < kLegs; ++l) {
        for (int c = 0; c < kCells; ++c) cell_[l][c] = rib_[l][c] = 1.0;
        ankle_[l] = collar_[l] = 1.0;
    }
    nEvents_ = 0;
}

int Feet::CellsAlive(int l) const {
    int n = 0;
    for (int c = 0; c < kCells; ++c) n += cell_[l][c] > 0.0 && rib_[l][c] > 0.0;
    return n;
}

void Feet::Push(int leg, int cell, int kind) {
    if (nEvents_ < 32) events_[nEvents_++] = {leg, cell, kind};
}

void Feet::Step(double dt, const LegIn* legs, bool enabled) {
    if (!enabled || dt <= 0.0) return;
    // fatigue over the rating: 10 % over wears a part through in ~20 s; over 1.5x it goes at once
    auto wear = [dt](double& integ, double ratio) {
        if (integ <= 0.0) return false;
        if (ratio > 1.5) integ = 0.0;
        else if (ratio > 1.0) integ = std::max(0.0, integ - (ratio - 1.0) * 0.5 * dt);
        return integ <= 0.0;
    };
    for (int l = 0; l < kLegs; ++l) {
        const LegIn& in = legs[l];
        if (!in.inPlay || LegBroken(l)) continue;
        const Rating& r = kRating[KindOf(l)];
        double sum = 0.0;
        for (int c = 0; c < kCells; ++c) {
            const CellIn& ci = in.cell[c];
            if (!ci.on || rib_[l][c] <= 0.0) continue;
            sum += ci.force;
            if (cell_[l][c] > 0.0) {
                const bool pinched = ci.pen >= 0.98 * ci.stroke && ci.vin > kBurstV;
                if (pinched) cell_[l][c] = 0.0;
                if (pinched || wear(cell_[l][c], ci.force / r.cell)) Push(l, c, kCellBurst);
            } else {   // the bare rib on the ground
                const bool hit = ci.vin > kRibV;
                if (hit) rib_[l][c] = 0.0;
                if (hit || wear(rib_[l][c], ci.force / (1.5 * r.cell))) Push(l, c, kRibBroken);
            }
        }
        if (wear(ankle_[l], sum / r.ankle)) Push(l, -1, kAnkleBroken);
        if (wear(collar_[l], in.ratio)) Push(l, -1, kCollarBroken);
    }
}

int Feet::TakeEvents(Event* out, int max) {
    const int n = std::min(max, nEvents_);
    for (int i = 0; i < n; ++i) out[i] = events_[i];
    nEvents_ = 0;
    return n;
}

void Feet::Describe(const Event& e, const char* legName, bool ru, char* buf, int size) {
    const int sec = kRating[KindOf(e.leg)].sections;
    switch (e.kind) {
        case kCellBurst:
            std::snprintf(buf, size, ru ? "%s: МР-стойка лепестка %d разрушена, лепесток на упоре" : "%s: the MR strut of petal %d failed, the petal on its stop",
                          legName, e.cell + 1);
            break;
        case kRibBroken:
            std::snprintf(buf, size, ru ? "%s: лепесток %d сломан у шарнира ступицы" : "%s: petal %d broken off at its hub hinge", legName, e.cell + 1);
            break;
        case kAnkleBroken:
            std::snprintf(buf, size, ru ? "%s: излом голеностопа, стопа потеряна" : "%s: ankle broken, the foot is lost", legName);
            break;
        default:
            std::snprintf(buf, size, ru ? "%s: разрушено сочленение секций %d-%d" : "%s: the joint of sections %d-%d failed",
                          legName, sec - 1, sec);
            break;
    }
}

void Feet::Save(char* buf, int size) const {
    char s[2 * kLegs * kCells + 2 * kLegs + 8];
    int k = 0;
    auto dig = [](double v) { return static_cast<char>('0' + std::clamp(static_cast<int>(v * 9.0 + 0.999), 0, 9)); };
    for (int l = 0; l < kLegs; ++l) for (int c = 0; c < kCells; ++c) s[k++] = dig(cell_[l][c]);
    s[k++] = ' ';
    for (int l = 0; l < kLegs; ++l) for (int c = 0; c < kCells; ++c) s[k++] = dig(rib_[l][c]);
    s[k++] = ' ';
    for (int l = 0; l < kLegs; ++l) s[k++] = dig(ankle_[l]);
    s[k++] = ' ';
    for (int l = 0; l < kLegs; ++l) s[k++] = dig(collar_[l]);
    s[k] = 0;
    std::snprintf(buf, size, "%s", s);
}

void Feet::Load(const char* s) {
    Repair();
    while (*s == ' ') ++s;
    auto read = [&s](double* dst, int n) {
        for (int i = 0; i < n; ++i) {
            if (*s < '0' || *s > '9') return false;
            dst[i] = (*s++ - '0') / 9.0;
        }
        while (*s == ' ') ++s;
        return true;
    };
    double cells[kLegs * kCells], ribs[kLegs * kCells], ank[kLegs], col[kLegs];
    if (!read(cells, kLegs * kCells) || !read(ribs, kLegs * kCells) || !read(ank, kLegs) || !read(col, kLegs)) return;
    for (int l = 0; l < kLegs; ++l) {
        for (int c = 0; c < kCells; ++c) { cell_[l][c] = cells[l * kCells + c]; rib_[l][c] = ribs[l * kCells + c]; }
        ankle_[l] = ank[l];
        collar_[l] = col[l];
    }
}

}  // namespace tantra::foot
