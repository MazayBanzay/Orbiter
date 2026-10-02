@echo off
rem Builds organism_trace.exe (no Orbiter needed). Run from this folder.
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul
cl /nologo /EHsc /std:c++20 /O2 /utf-8 /Istub organism_trace.cpp ..\src\LifeSupport.cpp /Fe:organism_trace.exe >build.log 2>&1
if errorlevel 1 (type build.log & exit /b 1)
del *.obj 2>nul
