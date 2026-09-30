@echo off
rem Offline checks of the core model (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /Fe:drive_test.exe drive_test.cpp ..\core\Drive.cpp >nul || exit /b 1
"%~dp0drive_test.exe"
del /q *.obj drive_test.exe
