@echo off
rem Offline checks of the reentry autopilot (orbiter2016/TantraReentry) with its forecast (TantraEntryForecast.h), the guidance's
rem helpers (TantraGuidance), core/Aero and core/Damage (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
if not exist reentry_obj mkdir reentry_obj
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /Fe:reentry_test.exe /Foreentry_obj\ reentry_test.cpp ..\orbiter2016\TantraReentry.cpp ..\orbiter2016\TantraGuidance.cpp ..\core\Aero.cpp ..\core\Damage.cpp ..\core\Impact.cpp >nul || exit /b 1
chcp 65001 >nul
"%~dp0reentry_test.exe"
set R=%errorlevel%
exit /b %R%
