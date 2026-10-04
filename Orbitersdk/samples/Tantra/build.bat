@echo off
rem Build Tantra.dll and TantraTrap.dll for Orbiter 2024 (x86) and install them into Modules ("build.bat noinstall" only builds).
rem The 2024 SDK: dynamic CRT (/MD - its Orbitersdk.lib and XRSound.lib are /MD), XRSound needs ATL (Build Tools component
rem VC.ATL), no gcAPI.lib (gcCoreAPI.h binds to D3D9Client at run time).
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
if errorlevel 1 (echo vcvars32 failed & exit /b 1)

set ROOT=%~dp0
set ORB=%ROOT%..\..\..
set SDK=%ORB%\Orbitersdk
set OUT=%ROOT%build
if not exist "%OUT%" mkdir "%OUT%"

set CFLAGS=/nologo /O2 /MD /EHsc /std:c++17 /W3 /LD /D_CRT_SECURE_NO_WARNINGS /wd4828 /Zc:strictStrings- ^
 /source-charset:utf-8 /execution-charset:windows-1251 ^
 /I"%SDK%\include" /I"%SDK%\XRSound" /I"%SDK%\samples\ShipView"

set LFLAGS=/LIBPATH:"%SDK%\lib" orbiter.lib Orbitersdk.lib "%SDK%\XRSound\XRSound.lib" kernel32.lib user32.lib gdi32.lib

set A=%ROOT%orbiter2016
cl %CFLAGS% /Fo"%OUT%\\" /Fe"%OUT%\Tantra.dll" ^
 "%ROOT%core\Ignition.cpp" "%ROOT%core\Drive.cpp" "%ROOT%core\ExhaustModel.cpp" "%ROOT%core\Carriage.cpp" "%ROOT%core\Legs.cpp" "%ROOT%core\Aero.cpp" "%ROOT%core\Damage.cpp" "%ROOT%core\Radiation.cpp" "%ROOT%core\Plant.cpp" ^
 "%A%\Tantra.cpp" "%A%\TantraPanel.cpp" "%A%\TantraExhaust.cpp" "%A%\TantraSafety.cpp" "%A%\TantraGear.cpp" "%A%\TantraPort.cpp" "%A%\TantraCrew.cpp" "%A%\TantraLift.cpp" "%A%\TantraWalk.cpp" "%A%\TantraScreen.cpp" "%A%\TantraInterior.cpp" "%A%\TantraDisplays.cpp" "%SDK%\samples\ShipView\ViewScreen.cpp" "%SDK%\samples\ShipView\ShipMfd.cpp" ^
 /link %LFLAGS%
if errorlevel 1 (echo BUILD FAILED & exit /b 1)

rem Trap cassette vessel (TantraTrap.dll).
cl /nologo /O2 /MD /EHsc /std:c++17 /W3 /LD /D_CRT_SECURE_NO_WARNINGS /Zc:strictStrings- /I"%SDK%\include" /Fo"%OUT%\\" /Fe"%OUT%\TantraTrap.dll" ^
 "%A%\TantraTrap.cpp" /link /LIBPATH:"%SDK%\lib" orbiter.lib Orbitersdk.lib kernel32.lib user32.lib
if errorlevel 1 (echo BUILD FAILED: TantraTrap & exit /b 1)

if /i "%1"=="noinstall" (echo Built to build\ only - not installed & endlocal & exit /b 0)
copy /y "%OUT%\TantraTrap.dll" "%ORB%\Modules\TantraTrap.dll" >nul
copy /y "%OUT%\Tantra.dll" "%ORB%\Modules\Tantra.dll" >nul
if errorlevel 1 (echo INSTALL FAILED: Modules\Tantra.dll is in use - close Orbiter and run build.bat again & exit /b 1)
echo Installed: Modules\Tantra.dll, Modules\TantraTrap.dll
endlocal
