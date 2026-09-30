# Runs a Blender Python script headless through the Microsoft Store alias.
# Usage: powershell -File run.ps1 <script.py> [extra args passed after --]
# The script should write its own log (Store Blender does not give us stdout).
param([Parameter(Mandatory=$true)][string]$Script, [string[]]$Rest)
$launcher = "$env:LOCALAPPDATA\Microsoft\WindowsApps\blender-launcher.exe"
$args2 = @('--background', '--python', "`"$Script`"")
if ($Rest) { $args2 += '--'; $args2 += ($Rest | ForEach-Object { $_ -split ',' }) }
$p = Start-Process $launcher -ArgumentList $args2 -PassThru -WindowStyle Hidden
$p.WaitForExit(1800000) | Out-Null
