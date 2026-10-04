@echo off
rem Offline check of core/Foot (no Orbiter needed).
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /utf-8 /Fe:foot_test.exe foot_test.cpp ..\core\Foot.cpp >nul || exit /b 1
"%~dp0foot_test.exe"
del /q foot_test.obj Foot.obj
