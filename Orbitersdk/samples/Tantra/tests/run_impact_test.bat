@echo off
rem Offline check of core/Impact (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /Fe:impact_test.exe impact_test.cpp ..\core\Impact.cpp >nul || exit /b 1
"%~dp0impact_test.exe"
del /q impact_test.obj Impact.obj
