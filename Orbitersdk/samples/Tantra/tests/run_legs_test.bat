@echo off
rem Offline checks of core/Legs (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /Fe:legs_test.exe legs_test.cpp ..\core\Legs.cpp >nul || exit /b 1
"%~dp0legs_test.exe"
set R=%errorlevel%
del /q legs_test.obj Legs.obj legs_test.exe
exit /b %R%
