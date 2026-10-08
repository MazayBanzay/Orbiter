// Written by Tantra_Design/blender/build_mpu.py (VARIANT=EMPU) - do not edit.
#pragma once

namespace empu {
constexpr int kLampInMat = 16;   // the cabin's lamps (0-based material): off / standby red / full
constexpr int kTermTex = 1, kYokeScrTex = 2;   // 1-based mesh textures replaced by the drawn surfaces
constexpr double kTermC[3] = {-0.0000, 1.6000, 3.7400}, kTermR[3] = {1.0000, 0.0000, -0.0000}, kTermU[3] = {-0.0000, 0.8660, 0.5000}, kTermN[3] = {-0.0000, 0.5000, -0.8660};
constexpr double kTermSW = 0.520, kTermSH = 0.390, kTermRecess = 0.06;   // the screen; the keys at +-(SW/2 + 0.065) across, 0.16 - k*0.08 up
constexpr int kLadderGrp[4] = {94, 0, 0, 0};
constexpr int kLadderN = 1;
constexpr double kLadderHinge[3] = {0.0, 0.590, -4.300};   // folds about x
constexpr int kYokeGrp[8] = {88, 89, 90, 91, 93, 0, 0, 0};
constexpr int kYokeN = 5;
}  // namespace empu
