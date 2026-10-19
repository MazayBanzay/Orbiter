@echo off
rem Build and run heat_test (TVHeat.h's numbers) against the Io lava field's data.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
if errorlevel 1 (echo vcvars32 failed & exit /b 1)
set T=%~dp0
if not exist "%T%build" mkdir "%T%build"
cl /nologo /O2 /EHsc /std:c++17 /W3 /D_CRT_SECURE_NO_WARNINGS /Fo"%T%build\\" /Fe"%T%build\heat_test.exe" "%T%heat_test.cpp" >"%T%build\cl.log"
if errorlevel 1 (type "%T%build\cl.log" & echo BUILD FAILED & exit /b 1)
"%T%build\heat_test.exe" "%T%..\..\..\..\Config\Io\LavaField\LavaField.cfg"
endlocal
