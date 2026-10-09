@echo off
rem Build TantraLander.dll (Gran 25.4 m) for Orbiter 2024 (x86) and install it with its mesh, textures, config and the
rem test scenarios ("build.bat noinstall" only builds). The mesh and the module go together: the group indices
rem (LanderMesh.h, gen_lander_header.py) are the contract between them. Install only with Orbiter closed.
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
if errorlevel 1 (echo vcvars32 failed & exit /b 1)

set ROOT=%~dp0
set ORB=%ROOT%..\..\..\..
set SDK=%ORB%\Orbitersdk
set OUT=%ROOT%build
set DES=%ORB%\Tantra_Design\lander_mesh\out
if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /O2 /MD /EHsc /std:c++17 /W3 /LD /D_CRT_SECURE_NO_WARNINGS /wd4828 /Zc:strictStrings- ^
 /source-charset:utf-8 /execution-charset:windows-1251 /I"%SDK%\include" /Fo"%OUT%\\" /Fe"%OUT%\TantraLander.dll" ^
 "%ROOT%TantraLander.cpp" "%ROOT%LanderCockpit.cpp" "%ROOT%..\core\LanderCore.cpp" "%ROOT%..\core\LanderPropulsion.cpp" "%ROOT%..\core\LanderAvionics.cpp" ^
 /link /LIBPATH:"%SDK%\lib" orbiter.lib Orbitersdk.lib kernel32.lib user32.lib gdi32.lib
if errorlevel 1 (echo BUILD FAILED & exit /b 1)

if /i "%1"=="noinstall" (echo Built to build\ only - not installed & endlocal & exit /b 0)
tasklist /FI "IMAGENAME eq orbiter.exe" 2>nul | find /i "orbiter.exe" >nul
if not errorlevel 1 (echo Orbiter is running - not installed. Close Orbiter and run build.bat again & endlocal & exit /b 2)
copy /y "%OUT%\TantraLander.dll" "%ORB%\Modules\TantraLander.dll" >nul
if errorlevel 1 (echo INSTALL FAILED: Modules\TantraLander.dll is in use & exit /b 1)
copy /y "%DES%\Lander.msh" "%ORB%\Meshes\Tantra\Lander.msh" >nul
copy /y "%DES%\LanderCabin.msh" "%ORB%\Meshes\Tantra\LanderCabin.msh" >nul
if not exist "%ORB%\Textures\Tantra\Lander" mkdir "%ORB%\Textures\Tantra\Lander"
copy /y "%DES%\textures\*.dds" "%ORB%\Textures\Tantra\Lander\" >nul
copy /y "%ROOT%TantraLander.cfg" "%ORB%\Config\Vessels\TantraLander.cfg" >nul
copy /y "%ROOT%scenarios\*.scn" "%ORB%\Scenarios\Tantra\" >nul
echo Installed: Modules\TantraLander.dll, Meshes\Tantra\Lander.msh + LanderCabin.msh, Textures\Tantra\Lander, Config\Vessels\TantraLander.cfg, Scenarios\Tantra (Gran - *)
endlocal
