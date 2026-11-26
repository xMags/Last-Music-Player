#requires -Version 7
# Shared by build.ps1, test.ps1 and run.ps1. Dot-source it, then call
# Enter-BuildEnvironment once.

Set-StrictMode -Version Latest

# The Qt version this project is developed against. Override the location by
# setting LMP_QT_ROOT before running any tool script.
$script:DefaultQtRoot = 'C:\Qt\6.10.3\msvc2022_64'

function Get-RepoRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
}

function Get-QtRoot {
    $qtRoot = if ($env:LMP_QT_ROOT) { $env:LMP_QT_ROOT } else { $script:DefaultQtRoot }
    if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'bin\qmake.exe'))) {
        throw "Qt was not found at '$qtRoot'. Install it there or set LMP_QT_ROOT."
    }
    return $qtRoot
}

# Loads the MSVC x64 compiler environment into this process and puts the
# CMake and Ninja that ship with Visual Studio, and Qt, on the PATH.
function Enter-BuildEnvironment {
    if ($env:LMP_BUILD_ENVIRONMENT_READY -eq '1') {
        return
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) {
        throw 'Visual Studio was not found (vswhere.exe is missing).'
    }

    $vsRoot = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath
    if (-not $vsRoot) {
        throw 'No Visual Studio installation with the C++ x64 tools was found.'
    }

    $vcvars = Join-Path $vsRoot 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars)) {
        throw "vcvars64.bat was not found under '$vsRoot'."
    }

    # Run the batch file in cmd and copy the environment it produces.
    $lines = & cmd.exe /d /c "`"$vcvars`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) {
        throw 'Loading the MSVC environment failed.'
    }
    foreach ($line in $lines) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            Set-Item -LiteralPath "Env:$($line.Substring(0, $separator))" -Value $line.Substring($separator + 1)
        }
    }

    $cmakeRoot = Join-Path $vsRoot 'Common7\IDE\CommonExtensions\Microsoft\CMake'
    $cmakeBin = Join-Path $cmakeRoot 'CMake\bin'
    $ninjaBin = Join-Path $cmakeRoot 'Ninja'
    if (-not (Test-Path -LiteralPath (Join-Path $cmakeBin 'cmake.exe'))) {
        throw 'The Visual Studio "C++ CMake tools for Windows" component is not installed.'
    }

    $qtRoot = Get-QtRoot
    $env:LMP_QT_ROOT = $qtRoot.Replace('\', '/')
    $env:PATH = "$cmakeBin;$ninjaBin;$(Join-Path $qtRoot 'bin');$env:PATH"
    $env:LMP_BUILD_ENVIRONMENT_READY = '1'
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(ValueFromRemainingArguments)][string[]]$Arguments
    )
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
}

# Configures on first use, then builds one configuration.
function Invoke-Build {
    param([ValidateSet('Debug', 'Release')][string]$Config = 'Debug')

    Enter-BuildEnvironment
    Push-Location (Get-RepoRoot)
    try {
        if (-not (Test-Path -LiteralPath '.build\qt\CMakeCache.txt')) {
            Invoke-Checked cmake --preset windows
        }
        Invoke-Checked cmake --build --preset $Config.ToLowerInvariant()
    }
    finally {
        Pop-Location
    }
}

function Get-AppPath {
    param([ValidateSet('Debug', 'Release')][string]$Config = 'Debug')
    return Join-Path (Get-RepoRoot) "x64\$Config\Last_Music_Player.exe"
}
