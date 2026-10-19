@echo off
rem Install the AtmoFX fork of D3D9Client (Orbiter must be closed). "install_client.bat stock" restores the stock client.
rem The fork keeps the name D3D9Client.dll so gcCore add-ons (MPU, IoLava, DrawOrbits...) still find it.
setlocal
set ORB=%~dp0..\..
set DLL=%ORB%\Modules\Plugin\D3D9Client.dll
if not exist "%DLL%.stock" copy /y "%DLL%" "%DLL%.stock" >nul
set NP=%ORB%\Modules\D3D9Client\NewPlanet.hlsl
if not exist "%NP%.stock" copy /y "%NP%" "%NP%.stock" >nul
set SC=%ORB%\Modules\D3D9Client\Scatter.hlsl
if not exist "%SC%.stock" copy /y "%SC%" "%SC%.stock" >nul
if /i "%1"=="stock" (copy /y "%DLL%.stock" "%DLL%" >nul && copy /y "%NP%.stock" "%NP%" >nul && copy /y "%SC%.stock" "%SC%" >nul && echo Stock D3D9Client.dll, NewPlanet.hlsl and Scatter.hlsl restored & exit /b 0)
copy /y "%~dp0obj\D3D9ClientFX.dll" "%DLL%" >nul
if errorlevel 1 (echo INSTALL FAILED: D3D9Client.dll in use - close Orbiter & exit /b 1)
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\AtmoFX_Volume.fx" "%ORB%\Modules\D3D9Client\AtmoFX_Volume.fx" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\NewPlanet.hlsl" "%NP%" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\Scatter.hlsl" "%SC%" >nul
copy /y "%~dp0obj\AtmoFX_Volume.fxo" "%ORB%\Modules\D3D9Client\AtmoFX_Volume.fxo" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\ExhaustVolume.fx" "%ORB%\Modules\D3D9Client\ExhaustVolume.fx" >nul
copy /y "%~dp0obj\ExhaustVolume.fxo" "%ORB%\Modules\D3D9Client\ExhaustVolume.fxo" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\TitanFX_Volume.fx" "%ORB%\Modules\D3D9Client\TitanFX_Volume.fx" >nul
copy /y "%~dp0obj\TitanFX_Volume.fxo" "%ORB%\Modules\D3D9Client\TitanFX_Volume.fxo" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\HeatShimmer.fx" "%ORB%\Modules\D3D9Client\HeatShimmer.fx" >nul
copy /y "%~dp0obj\HeatShimmer.fxo" "%ORB%\Modules\D3D9Client\HeatShimmer.fxo" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\JetEjecta.fx" "%ORB%\Modules\D3D9Client\JetEjecta.fx" >nul
copy /y "%~dp0obj\JetEjecta.fxo" "%ORB%\Modules\D3D9Client\JetEjecta.fxo" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\LiquidVolume.fx" "%ORB%\Modules\D3D9Client\LiquidVolume.fx" >nul
copy /y "%~dp0obj\LiquidVolume.fxo" "%ORB%\Modules\D3D9Client\LiquidVolume.fxo" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\HudOverlay.fx" "%ORB%\Modules\D3D9Client\HudOverlay.fx" >nul
copy /y "%~dp0obj\HudOverlay.fxo" "%ORB%\Modules\D3D9Client\HudOverlay.fxo" >nul
copy /y "%~dp0..\orbiter\OVP\D3D9Client\shaders\LiquidWake.fx" "%ORB%\Modules\D3D9Client\LiquidWake.fx" >nul
copy /y "%~dp0obj\LiquidWake.fxo" "%ORB%\Modules\D3D9Client\LiquidWake.fxo" >nul
echo Installed AtmoFX D3D9Client fork (stock copy: Modules\Plugin\D3D9Client.dll.stock)
endlocal
