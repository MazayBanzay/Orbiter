@echo off
rem Offline check of core/Carriage. MeshLayout.h: the installed one by default, the working T9 one with "t9".
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
set LAYOUT=\"../orbiter2016/MeshLayout.h\"
if /i "%1"=="t9" set LAYOUT=\"../build/mesh_t9/MeshLayout.h\"
cl /nologo /O2 /EHsc /std:c++17 /DMESH_LAYOUT_H=%LAYOUT% /Fe"%~dp0carriage_test.exe" /Fo"%~dp0\" "%~dp0carriage_test.cpp" "%~dp0..\core\Carriage.cpp" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%~dp0carriage_test.exe"
