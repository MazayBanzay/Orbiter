@echo off
rem Offline check of the effective exhaust and the drive's radiation (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /Fe:radiation_test.exe radiation_test.cpp ..\core\Drive.cpp ..\core\Radiation.cpp >nul || exit /b 1
"%~dp0radiation_test.exe"
del /q *.obj radiation_test.exe
