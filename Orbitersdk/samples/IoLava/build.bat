@echo off
rem Build IoLava.dll (live effects of the Io lava field, Tantra_Design\io_lava) for Orbiter 2024 (x86) and install it
rem into Modules ("build.bat noinstall" only builds).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
if errorlevel 1 (echo vcvars32 failed & exit /b 1)
set ROOT=%~dp0
set ORB=%ROOT%..\..\..
set SDK=%ORB%\Orbitersdk
set OUT=%ROOT%build
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /O2 /MD /EHsc /std:c++17 /W3 /LD /D_CRT_SECURE_NO_WARNINGS /Zc:strictStrings- ^
 /I"%SDK%\include" /Fo"%OUT%\\" /Fe"%OUT%\IoLava.dll" "%ROOT%IoLava.cpp" /link /LIBPATH:"%SDK%\lib" orbiter.lib Orbitersdk.lib kernel32.lib user32.lib
if errorlevel 1 (echo BUILD FAILED & exit /b 1)
if /i "%1"=="noinstall" (echo Built to build\ only - not installed & endlocal & exit /b 0)
copy /y "%OUT%\IoLava.dll" "%ORB%\Modules\IoLava.dll" >nul
if errorlevel 1 (echo INSTALL FAILED: Modules\IoLava.dll is in use - close Orbiter and run build.bat again & exit /b 1)
echo Installed: Modules\IoLava.dll
endlocal
