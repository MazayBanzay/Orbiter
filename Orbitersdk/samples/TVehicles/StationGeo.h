// Written by Tantra_Design/tools/build_station.py - do not edit. Vessel frame (x right, y up, z forward).
#pragma once
namespace st {
constexpr int kCells = 6;
constexpr int kCellGrp[kCells] = {13, 14, 15, 16, 17, 18};
constexpr double kCellPos[kCells][3] = {{-1.800, -0.127, -0.100}, {-1.300, -0.127, -0.100}, {-0.800, -0.127, -0.100}, {-1.800, 0.423, -0.100}, {-1.300, 0.423, -0.100}, {-0.800, 0.423, -0.100}};   // the cartridges' centres (the rack: lower row first)
constexpr int kCableGrp = 19, kPlugGrp = 20, kCableRings = 33;
constexpr int kCableVtx = 1024, kPlugVtx = 168;   // the vertices the module moves (the rest: the culling box)
constexpr double kExit[3] = {1.275, -0.620, 0.640}, kHolder[3] = {0.920, 0.340, 0.200};
constexpr double kReel[3] = {1.275, -0.140, 0.590};   // the drum's front, where the hand takes the cable
constexpr double kSolid[4] = {-2.05, 2.03, -0.55, 0.70};   // the units' footprint people walk round: x0 x1 z0 z1
constexpr double kPad[4] = {-2.35, 2.25, -1.20, 2.40}, kPadTop = -0.940;   // the concrete pad: x0 x1 z0 z1
constexpr double kGround = -1.000;
}  // namespace st
