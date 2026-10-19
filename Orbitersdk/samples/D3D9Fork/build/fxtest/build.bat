@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
cd /d "%~dp0"
cl /nologo /O2 /MD /EHsc fxtest.cpp /I"%~dp0..\..\deps\d3dx\build\native\include" /link /LIBPATH:"%~dp0..\..\deps\d3dx\build\native\release\lib\x86" d3d9.lib d3dx9.lib user32.lib
