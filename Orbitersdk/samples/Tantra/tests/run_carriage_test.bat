@echo off
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cl /nologo /O2 /EHsc /std:c++17 /Fe"%~dp0carriage_test.exe" /Fo%~dp0 "%~dp0carriage_test.cpp" "%~dp0..\core\Carriage.cpp" >nul
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
"%~dp0carriage_test.exe"
