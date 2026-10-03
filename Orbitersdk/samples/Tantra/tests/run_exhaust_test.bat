@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /EHsc /std:c++17 /O2 /Fe:exhaust_test.exe exhaust_test.cpp ..\core\ExhaustModel.cpp >nul || exit /b 1
"%~dp0exhaust_test.exe"
del /q *.obj exhaust_test.exe
