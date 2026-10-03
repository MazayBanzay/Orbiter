@echo off
rem Offline checks of core/Damage (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /Fe:damage_test.exe damage_test.cpp ..\core\Damage.cpp ..\core\Aero.cpp >nul || exit /b 1
"%~dp0damage_test.exe"
set R=%errorlevel%
del /q damage_test.obj Damage.obj Aero.obj
exit /b %R%
