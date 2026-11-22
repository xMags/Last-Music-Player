#requires -Version 7
<#
.SYNOPSIS
    Builds the application and its tests.

.PARAMETER Config
    Debug (default) or Release.

.PARAMETER Reconfigure
    Runs the CMake configure step again before building. Needed after the
    private config file changes.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Debug',

    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')

if ($Reconfigure) {
    Enter-BuildEnvironment
    Push-Location (Get-RepoRoot)
    try {
        Invoke-Checked cmake --preset windows
    }
    finally {
        Pop-Location
    }
}

Invoke-Build -Config $Config
Write-Host "Build succeeded ($Config)" -ForegroundColor Green
