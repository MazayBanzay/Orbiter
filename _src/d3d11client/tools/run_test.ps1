# Test run of the D3D11 client: Orbiter's NG server, minimised (no focus change), no sound (XRSound left out for the run),
# the client loaded from the command line. Afterwards Orbiter_NG.cfg and "(Current state).scn" are put back as they were.
# -Set "Key = value", ...: Client.cfg keys for this run only (e.g. -Set "MicroTextures = 0")
param([string]$Scenario = "(Current state)", [int]$Seconds = 45, [int]$AA = -1, [string[]]$Set = @(), [string]$Script = "")
$orb = "C:\Games\Orbiter-2024"
# never alongside the user's own Orbiter: the restore at the end would overwrite their cfg and state
# one test at a time across all sessions: a named mutex queues them (released when this script's process ends)
$runLock = New-Object System.Threading.Mutex($false, "Local\D3D11ClientTestRun")
try { if (-not $runLock.WaitOne([TimeSpan]::FromMinutes(15))) { "another test is still running - test skipped"; exit 2 } } catch [System.Threading.AbandonedMutexException] { }
if (Get-Process -Name Orbiter, Orbiter_ng -ErrorAction SilentlyContinue) { "Orbiter is running - test skipped"; exit 2 }
$cs = Join-Path $orb "Scenarios\(Current state).scn"
$cfg = Join-Path $orb "Orbiter_NG.cfg"
$csBak = Join-Path $env:TEMP "d3d11client_current_state.scn"
$cfgBak = Join-Path $env:TEMP "d3d11client_Orbiter_NG.cfg"
Copy-Item -LiteralPath $cs -Destination $csBak -Force
Copy-Item -LiteralPath $cfg -Destination $cfgBak -Force
$ccfg = Join-Path $orb "Config\D3D11Client\Client.cfg"
$ccfgBak = Join-Path $env:TEMP "d3d11client_Client.cfg"
$hadCcfg = Test-Path -LiteralPath $ccfg
if ($hadCcfg) { Copy-Item -LiteralPath $ccfg -Destination $ccfgBak -Force }
try {
    if ($AA -ge 0) {   # the anti-aliasing mode for this run only
        $lines = @(); if ($hadCcfg) { $lines = Get-Content -LiteralPath $ccfg | Where-Object { $_ -notmatch '^\s*AntiAliasing' } }
        ($lines + "AntiAliasing = $AA") | Set-Content -LiteralPath $ccfg -Encoding ascii
    }
    foreach ($kv in $Set) {   # further keys for this run only
        $k = ($kv -split '=')[0].Trim()
        $lines = @(); if (Test-Path -LiteralPath $ccfg) { $lines = Get-Content -LiteralPath $ccfg | Where-Object { $_ -notmatch ('^\s*' + $k + '\s*=') } }
        ($lines + $kv) | Set-Content -LiteralPath $ccfg -Encoding ascii
    }
    (Get-Content -LiteralPath $cfg) | Where-Object { $_ -notmatch '^\s*XRSound\s*$' -and $_ -notmatch '^\s*D3D11Client\s*$' } | Set-Content -LiteralPath $cfg -Encoding ascii
    Copy-Item -LiteralPath (Join-Path $orb ("Scenarios\" + $Scenario + ".scn")) -Destination (Join-Path $orb "Scenarios\Tests\D3D11_test.scn") -Force
    $env:D3D11CLIENT_TEST = "1"     # the client keeps its window hidden and inactive
    if ($Script) { $env:D3D11CLIENT_TEST_SCRIPT = $Script }   # timed actions in the run (Client.cpp: TestScript)
    $p = Start-Process -FilePath (Join-Path $orb "Modules\Server\Orbiter.exe") -WorkingDirectory $orb `
        -ArgumentList "-p D3D11Client -s `"Tests\D3D11_test`" --maxsystime=$Seconds -x" -WindowStyle Minimized -PassThru
    if (-not $p.WaitForExit(($Seconds + 120) * 1000)) { Stop-Process -Id $p.Id -Force; "killed (hang)" }
    "exit code: " + $p.ExitCode
    # this run's own copy of the log (other sessions' runs overwrite Orbiter.log) and of the shot
    $logs = Join-Path $PSScriptRoot "logs"; New-Item -ItemType Directory -Force -Path $logs | Out-Null
    $stamp = (Get-Date -Format "HHmmss") + "_" + ($Scenario -replace '[^A-Za-z0-9]', '_')
    Copy-Item -LiteralPath (Join-Path $orb "Orbiter.log") -Destination (Join-Path $logs "$stamp.log") -Force
    $shot = Join-Path $orb "Modules\D3D11Client\shots\start.png"
    if (Test-Path $shot) { Copy-Item -LiteralPath $shot -Destination (Join-Path $logs "$stamp.png") -Force }
    "log: " + (Join-Path $logs "$stamp.log")
} finally {
    Remove-Item Env:D3D11CLIENT_TEST -ErrorAction SilentlyContinue
    Remove-Item Env:D3D11CLIENT_TEST_SCRIPT -ErrorAction SilentlyContinue
    Copy-Item -LiteralPath $csBak -Destination $cs -Force
    Copy-Item -LiteralPath $cfgBak -Destination $cfg -Force
    if ($hadCcfg) { Copy-Item -LiteralPath $ccfgBak -Destination $ccfg -Force }
    elseif ($AA -ge 0 -or $Set.Count) { Set-Content -LiteralPath $ccfg -Value "; D3D11Client settings (written by a test run; Launchpad > Video > Advanced rewrites it)" -Encoding ascii }
}
