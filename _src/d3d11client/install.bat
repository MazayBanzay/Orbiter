@echo off
rem Copies the built client into Orbiter: Modules\Plugin\D3D11Client.dll and the shaders to Modules\D3D11Client\
setlocal
set ORB=%~dp0..\..
if not exist "%ORB%\Modules\D3D11Client" mkdir "%ORB%\Modules\D3D11Client"
copy /y "%~dp0build\D3D11Client.dll" "%ORB%\Modules\Plugin\D3D11Client.dll" >nul || exit /b 1
copy /y "%~dp0shaders\*.hlsl" "%ORB%\Modules\D3D11Client\" >nul || exit /b 1
copy /y "%~dp0shaders\*.hlsli" "%ORB%\Modules\D3D11Client\" >nul || exit /b 1
copy /y "%~dp0build\D3D9Client.dll" "%ORB%\Modules\D3D11Client\D3D9Client.dll" >nul || exit /b 1
echo installed
