@echo off
rem Headless checks of the «Грань» Т1Б-А systems core (core/Lander*), no Orbiter needed.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
if not exist lander_obj mkdir lander_obj
cl /nologo /EHsc /std:c++17 /O2 /W3 /utf-8 /Fe:lander_core_test.exe /Folander_obj\ lander_core_test.cpp ..\core\LanderCore.cpp ..\core\LanderPropulsion.cpp ..\core\LanderAvionics.cpp || exit /b 1
chcp 65001 >nul
"%~dp0lander_core_test.exe"
set R=%errorlevel%
exit /b %R%
