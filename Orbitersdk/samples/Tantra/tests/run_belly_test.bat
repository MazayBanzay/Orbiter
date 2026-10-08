@echo off
rem Offline checks of the belly-landing autopilot (orbiter2016/TantraBellyLand) with the guidance's helpers (TantraGuidance; no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
if not exist belly_obj mkdir belly_obj
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /Fe:belly_test.exe /Fobelly_obj\ belly_test.cpp ..\orbiter2016\TantraBellyLand.cpp ..\orbiter2016\TantraGuidance.cpp >nul || exit /b 1
chcp 65001 >nul
"%~dp0belly_test.exe"
set R=%errorlevel%
exit /b %R%
