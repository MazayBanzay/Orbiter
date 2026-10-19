@echo off
rem Build the AtmoFX fork of D3D9Client (x86) from _src\orbiter\OVP\D3D9Client. Output: obj\D3D9ClientFX.dll
setlocal
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars32.bat" >nul 2>nul
if errorlevel 1 (echo vcvars32 failed & exit /b 1)
set B=%~dp0
set SRC=%B%..\orbiter\OVP\D3D9Client
set ORB=%B%..\..
set DX=%B%..\deps\d3dx\build\native
cd /d "%B%obj"
rc /nologo /i "%SRC%" /i "%ORB%\Orbitersdk\include" /fo D3D9Client.res "%SRC%\D3D9Client.rc"
if errorlevel 1 (echo RC FAILED & exit /b 1)
cl /nologo /c /MP /O2 /MD /EHsc /std:c++17 /W1 /DNDEBUG /DNOMINMAX /DD3D9CLIENT_EXPORTS /D_CRT_SECURE_NO_WARNINGS /DWIN32 /D_WINDOWS ^
 /I"%B%shim" /I"%SRC%" /I"%ORB%\Orbitersdk\include" /I"%DX%\include" "%SRC%\*.cpp"
if errorlevel 1 (echo COMPILE FAILED & exit /b 1)
link /nologo /DLL /OUT:D3D9ClientFX.dll *.obj D3D9Client.res /LIBPATH:"%ORB%\Orbitersdk\lib" /LIBPATH:"%DX%\release\lib\x86" ^
 orbiter.lib Orbitersdk.lib d3d9.lib d3dx9.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib version.lib msimg32.lib comctl32.lib psapi.lib /NODEFAULTLIB:LIBCMT
if errorlevel 1 (echo LINK FAILED & exit /b 1)
set FXC=C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe
"%FXC%" /nologo /T fx_2_0 /Fo AtmoFX_Volume.fxo "%SRC%\shaders\AtmoFX_Volume.fx" >nul
if errorlevel 1 (echo FXC FAILED & exit /b 1)
"%FXC%" /nologo /T fx_2_0 /Fo ExhaustVolume.fxo "%SRC%\shaders\ExhaustVolume.fx" >nul
if errorlevel 1 (echo FXC FAILED: ExhaustVolume & exit /b 1)
"%FXC%" /nologo /T fx_2_0 /Fo TitanFX_Volume.fxo "%SRC%\shaders\TitanFX_Volume.fx" >nul
if errorlevel 1 (echo FXC FAILED: TitanFX_Volume & exit /b 1)
"%FXC%" /nologo /T fx_2_0 /Fo HeatShimmer.fxo "%SRC%\shaders\HeatShimmer.fx" >nul
if errorlevel 1 (echo FXC FAILED: HeatShimmer & exit /b 1)
"%FXC%" /nologo /T fx_2_0 /Fo JetEjecta.fxo "%SRC%\shaders\JetEjecta.fx" >nul
if errorlevel 1 (echo FXC FAILED: JetEjecta & exit /b 1)
"%FXC%" /nologo /T fx_2_0 /Fo LiquidVolume.fxo "%SRC%\shaders\LiquidVolume.fx" >nul
if errorlevel 1 (echo FXC FAILED: LiquidVolume & exit /b 1)
"%FXC%" /nologo /T fx_2_0 /Fo HudOverlay.fxo "%SRC%\shaders\HudOverlay.fx" >nul
if errorlevel 1 (echo FXC FAILED: HudOverlay & exit /b 1)
"%FXC%" /nologo /T fx_2_0 /Fo LiquidWake.fxo "%SRC%\shaders\LiquidWake.fx" >nul
if errorlevel 1 (echo FXC FAILED: LiquidWake & exit /b 1)
echo Built: %B%obj\D3D9ClientFX.dll
endlocal
