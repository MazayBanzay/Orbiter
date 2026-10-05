@echo off
rem Builds hands_trace.exe: the seated figure's own code (Skin, Motion, SeatArms) without Orbiter. Run from this folder.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /W3 /Istub_pose hands_trace.cpp ..\src\Skin.cpp ..\src\Motion.cpp ..\src\SeatArms.cpp /Fe:hands_trace.exe >build_hands.log 2>&1
if errorlevel 1 (type build_hands.log & exit /b 1)
del *.obj 2>nul
