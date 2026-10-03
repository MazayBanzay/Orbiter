@echo off
rem Build the module, then install it and the meshes TOGETHER: the meshes only if the DLL could be replaced (Orbiter closed),
rem so the game never runs new meshes with an old module (the group indices are a contract between them).
setlocal
cd /d "%~dp0"
python tools\gen_mesh.py >nul || (echo MESH BUILD FAILED & exit /b 1)
copy /y "build\mesh_t9\InteriorLayout.h" "orbiter2016\InteriorLayout.h" >nul
copy /y "build\mesh_t9\MeshLayout.h" "orbiter2016\MeshLayout.h" >nul
call "%~dp0build.bat" noinstall || (echo BUILD FAILED & exit /b 1)
copy /y "build\Tantra.dll" "..\..\..\Modules\Tantra.dll" >nul 2>nul || (echo NOT INSTALLED: Orbiter is running - close it and run install_all.bat again & exit /b 1)
copy /y "build\TantraTrap.dll" "..\..\..\Modules\TantraTrap.dll" >nul
python tools\gen_mesh.py --install >nul || (echo MESH INSTALL FAILED & exit /b 1)
python tools\make_lift_textures.py >nul
echo Installed: Tantra.dll + meshes + lift textures
endlocal
