@echo off
rem Offline checks of core/TantraCore with core/Plant and core/Damage (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
if not exist core_obj mkdir core_obj
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /Fe:core_test.exe /Focore_obj\ core_test.cpp ..\core\TantraCore.cpp ..\core\Plant.cpp ..\core\Damage.cpp ..\core\Impact.cpp ..\core\Aero.cpp >nul || exit /b 1
chcp 65001 >nul
"%~dp0core_test.exe"
set R=%errorlevel%
exit /b %R%
