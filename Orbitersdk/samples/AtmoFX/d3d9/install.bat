@echo off
rem AtmoFX volumetric deck: install the forked NewPlanet.hlsl into Modules\D3D9Client (compiled by the client at start).
rem The stock file is kept once as NewPlanet.hlsl.stock; "install.bat stock" puts it back.
setlocal
set ORB=%~dp0..\..\..\..
set DST=%ORB%\Modules\D3D9Client\NewPlanet.hlsl
if not exist "%DST%.stock" copy /y "%DST%" "%DST%.stock" >nul
if /i "%1"=="stock" (copy /y "%DST%.stock" "%DST%" >nul & echo Stock NewPlanet.hlsl restored & exit /b 0)
copy /y "%~dp0NewPlanet.hlsl" "%DST%" >nul
if errorlevel 1 (echo INSTALL FAILED & exit /b 1)
echo Installed AtmoFX NewPlanet.hlsl (stock copy: NewPlanet.hlsl.stock)
endlocal
