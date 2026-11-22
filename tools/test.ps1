#requires -Version 7
<#
.SYNOPSIS
    Builds Debug and runs every test through CTest.

.PARAMETER Filter
    Optional regular expression; only tests whose name matches are run.
#>
[CmdletBinding()]
param(
    [string]$Filter
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')

Invoke-Build -Config Debug

Push-Location (Get-RepoRoot)
try {
    # Static checks for every QML file in the module.
    Invoke-Checked cmake --build --preset debug --target all_qmllint

    if ($Filter) {
        Invoke-Checked ctest --preset debug -R $Filter
    }
    else {
        Invoke-Checked ctest --preset debug
    }
}
finally {
    Pop-Location
}
Write-Host 'All tests passed' -ForegroundColor Green
