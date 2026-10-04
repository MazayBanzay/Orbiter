@echo off
rem Offline check of core/Plant (the planetary power plant) with Config/Tantra/plant.cfg.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /O2 /EHsc /std:c++17 /utf-8 /Fe"%~dp0plant_test.exe" /Fo"%~dp0\" "%~dp0plant_test.cpp" "%~dp0..\core\Plant.cpp" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%~dp0plant_test.exe"
