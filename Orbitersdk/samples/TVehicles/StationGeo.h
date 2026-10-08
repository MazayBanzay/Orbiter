// Written by Tantra_Design/tools/build_station.py - do not edit. Vessel frame (x right, y up, z forward).
#pragma once
namespace st {
constexpr int kCells = 6;
constexpr int kCellGrp[kCells] = {5, 6, 7, 8, 9, 10};
constexpr double kCellPos[kCells][3] = {{-0.620, -0.450, 0.580}, {-0.300, -0.450, 0.580}, {0.020, -0.450, 0.580}, {-0.620, 0.050, 0.580}, {-0.300, 0.050, 0.580}, {0.020, 0.050, 0.580}};
constexpr int kCableGrp = 13, kPlugGrp = 14, kCableRings = 17;
constexpr double kExit[3] = {0.450, -0.380, 0.900}, kHolder[3] = {0.690, 0.300, 0.550};
constexpr double kGround = -1.000;
}  // namespace st
