@echo off
rem D3D11Client for Orbiter 2024 (32-bit): VS 2022 BuildTools, x86, Windows SDK 10.0.26100. Output: build\D3D11Client.dll
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" 10.0.26100.0 >nul || exit /b 1
cd /d "%~dp0"
rem each session builds in its own folder (set D3D11_BUILD_TAG, else "main"), so parallel builds don't share objects or a
rem PDB; the finished DLLs are copied to build\ for install.bat
if "%D3D11_BUILD_TAG%"=="" set D3D11_BUILD_TAG=main
set OUT=build\%D3D11_BUILD_TAG%
if not exist %OUT% mkdir %OUT%
rc /nologo /fo %OUT%\D3D11Client.res src\D3D11Client.rc || exit /b 1
set ORB=%~dp0..\..
cl /nologo /std:c++17 /O2 /Zi /EHsc /W3 /MD /utf-8 /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /DWIN32 /D_WINDOWS ^
   /I"%ORB%\Orbitersdk\include" /Fo%OUT%\ /Fd%OUT%\ /LD /Fe%OUT%\D3D11Client.dll ^
   src\Client.cpp src\Mesh.cpp src\Planet.cpp src\Dds.cpp src\Draw.cpp src\Ext.cpp src\Liquid.cpp src\VideoTab.cpp src\Particles.cpp src\Stars.cpp src\Fx.cpp src\FxAtmo.cpp src\FxWaves.cpp src\AA.cpp src\Ipi.cpp src\GpuPad.cpp src\Ar.cpp src\HudApi.cpp src\Wake.cpp ^
   %OUT%\D3D11Client.res /link /DEBUG /OPT:REF /OPT:ICF /LIBPATH:"%ORB%\Orbitersdk\lib" Orbiter.lib Orbitersdk.lib user32.lib gdi32.lib || exit /b 1
cl /nologo /O2 /LD shim\shim.c /Fo%OUT%\shim.obj /Fe%OUT%\D3D9Client.dll /link /NOENTRY || exit /b 1
copy /y %OUT%\D3D11Client.dll build\ >nul || exit /b 1
copy /y %OUT%\D3D11Client.pdb build\ >nul
copy /y %OUT%\D3D9Client.dll build\ >nul || exit /b 1
echo built build\D3D11Client.dll (from %OUT%)
