#requires -Version 7
<#
.SYNOPSIS
    Builds the application, closes a running copy, and starts the new build.

.PARAMETER Config
    Debug (default) or Release.

.PARAMETER SmokeTest
    Starts the application with --smoke-test, waits for it to exit on its own
    and fails if it did not exit cleanly. Used to prove the window loads.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [switch]$SmokeTest
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')

# A running copy keeps the executable locked, so close it before building.
$running = @(Get-Process -Name 'Last_Music_Player' -ErrorAction SilentlyContinue)
foreach ($process in $running) {
    if (-not $process.CloseMainWindow()) {
        $process.Kill()
    }
    if (-not $process.WaitForExit(5000)) {
        $process.Kill()
        $process.WaitForExit()
    }
}

Invoke-Build -Config $Config

$appPath = Get-AppPath -Config $Config
if (-not (Test-Path -LiteralPath $appPath)) {
    throw "The application was not found at '$appPath'."
}

if ($SmokeTest) {
    $errorLog = Join-Path (Get-RepoRoot) '.build\qt\smoke-test.err.txt'
    $env:QT_FORCE_STDERR_LOGGING = '1'
    $process = Start-Process -FilePath $appPath -ArgumentList '--smoke-test' -PassThru `
        -RedirectStandardError $errorLog
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw 'Smoke test failed: the application did not exit within 30 seconds.'
    }
    $messages = Get-Content -LiteralPath $errorLog -ErrorAction SilentlyContinue
    if ($messages) {
        Write-Host 'Messages logged by the application:' -ForegroundColor Yellow
        $messages | ForEach-Object { Write-Host "  $_" -ForegroundColor Yellow }
    }
    if ($process.ExitCode -ne 0) {
        throw "Smoke test failed: exit code $($process.ExitCode)."
    }
    Write-Host 'Smoke test passed: the window loaded and closed cleanly' -ForegroundColor Green
    return
}

$started = Start-Process -FilePath $appPath -PassThru
Write-Host "Started $appPath (process $($started.Id))" -ForegroundColor Green
