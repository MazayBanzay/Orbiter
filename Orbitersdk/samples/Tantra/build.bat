@echo off
rem Build Tantra.dll and TantraTrap.dll for Orbiter 2016 (x86) and install them into Modules.
rem Static CRT: XRSound.lib is built with /MT.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
if errorlevel 1 (echo vcvars32 failed & exit /b 1)

set ROOT=%~dp0
set ORB=%ROOT%..\..\..
set SDK=%ORB%\Orbitersdk
set OUT=%ROOT%build
if not exist "%OUT%" mkdir "%OUT%"

set CFLAGS=/nologo /O2 /MT /EHsc /std:c++17 /W3 /LD /D_CRT_SECURE_NO_WARNINGS /wd4828 /Zc:strictStrings- ^
 /source-charset:utf-8 /execution-charset:windows-1251 ^
 /I"%SDK%\include" /I"%SDK%\XRSound"

set LFLAGS=/LIBPATH:"%SDK%\lib" orbiter.lib Orbitersdk.lib "%SDK%\XRSound\XRSound.lib" kernel32.lib user32.lib gdi32.lib

set A=%ROOT%orbiter2016
cl %CFLAGS% /Fo"%OUT%\\" /Fe"%OUT%\Tantra.dll" ^
 "%ROOT%core\Ignition.cpp" "%ROOT%core\Drive.cpp" "%ROOT%core\ExhaustModel.cpp" "%ROOT%core\Carriage.cpp" "%ROOT%core\Radiation.cpp" ^
 "%A%\Tantra.cpp" "%A%\TantraPanel.cpp" "%A%\TantraExhaust.cpp" "%A%\TantraSafety.cpp" "%A%\TantraGear.cpp" "%A%\TantraPort.cpp" "%A%\TantraCrew.cpp" ^
 /link %LFLAGS%
if errorlevel 1 (echo BUILD FAILED & exit /b 1)

rem Trap cassette vessel (TantraTrap.dll).
cl /nologo /O2 /MT /EHsc /std:c++17 /W3 /LD /D_CRT_SECURE_NO_WARNINGS /Zc:strictStrings- /I"%SDK%\include" /Fo"%OUT%\\" /Fe"%OUT%\TantraTrap.dll" ^
 "%A%\TantraTrap.cpp" /link /LIBPATH:"%SDK%\lib" orbiter.lib Orbitersdk.lib kernel32.lib user32.lib
if errorlevel 1 (echo BUILD FAILED: TantraTrap & exit /b 1)

copy /y "%OUT%\TantraTrap.dll" "%ORB%\Modules\TantraTrap.dll" >nul
copy /y "%OUT%\Tantra.dll" "%ORB%\Modules\Tantra.dll" >nul
if errorlevel 1 (echo INSTALL FAILED: Modules\Tantra.dll is in use - close Orbiter and run build.bat again & exit /b 1)
echo Installed: Modules\Tantra.dll, Modules\TantraTrap.dll
endlocal
