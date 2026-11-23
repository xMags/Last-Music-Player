#requires -Version 7
<#
.SYNOPSIS
    Builds Release and assembles a self-contained application folder in
    .build\qt\package\, then proves it runs without Qt on the PATH.
.DESCRIPTION
    Copies Last_Music_Player.exe and Assets, runs windeployqt (release, QML
    imports found from src\ui\qml, no translations, no software OpenGL, no
    compiler runtime installer, no QML debugging plugins), and copies the
    MSVC runtime DLLs next to the exe so the folder also runs where the Visual
    C++ redistributable is not installed. Then starts the packaged exe with --smoke-test from an
    environment whose PATH has no Qt folder, and prints the folder size.
.PARAMETER SkipBuild
    Packages the Release build that is already there.
#>
[CmdletBinding()]
param([switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')

$root = Get-RepoRoot
if (-not $SkipBuild) {
    Invoke-Build -Config Release
}
Enter-BuildEnvironment
$qtRoot = Get-QtRoot
$exe = Get-AppPath -Config Release
if (-not (Test-Path -LiteralPath $exe)) {
    throw "The Release build was not found at '$exe'."
}

$package = Join-Path $root '.build\qt\package'
if (Test-Path -LiteralPath $package) {
    Remove-Item -LiteralPath $package -Recurse -Force
}
New-Item -ItemType Directory -Path $package | Out-Null
Copy-Item -LiteralPath $exe -Destination $package
Copy-Item -LiteralPath (Join-Path $root 'Assets') -Destination (Join-Path $package 'Assets') -Recurse

$packagedExe = Join-Path $package 'Last_Music_Player.exe'
Invoke-Checked (Join-Path $qtRoot 'bin\windeployqt.exe') `
    --release `
    --qmldir (Join-Path $root 'src\ui\qml') `
    --no-translations `
    --no-opengl-sw `
    --no-compiler-runtime `
    --no-system-d3d-compiler `
    --skip-plugin-types qmltooling `
    $packagedExe

# The MSVC runtime, app-local (Microsoft's redistributable DLLs).
if (-not $env:VCToolsRedistDir) {
    throw 'VCToolsRedistDir is not set; the MSVC environment did not load.'
}
$crt = Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory |
    Where-Object { $_.Name -like 'Microsoft.VC*.CRT' } | Select-Object -First 1
if (-not $crt) {
    throw "No MSVC runtime folder was found under '$($env:VCToolsRedistDir)x64'."
}
Get-ChildItem -LiteralPath $crt.FullName -Filter '*.dll' | Copy-Item -Destination $package

# The smoke test, with every Qt folder taken off the PATH.
$cleanPath = ($env:PATH -split ';' | Where-Object {
        $_ -and ($_ -notmatch '(?i)\\Qt\\') -and ($_ -notmatch '(?i)qt6') -and
        -not (Test-Path -LiteralPath (Join-Path $_ 'Qt6Core.dll'))
    }) -join ';'
$savedPath = $env:PATH
$errorLog = Join-Path $root '.build\qt\package-smoke.err.txt'
try {
    $env:PATH = $cleanPath
    $process = Start-Process -FilePath $packagedExe -ArgumentList '--smoke-test' -PassThru `
        -RedirectStandardError $errorLog -WindowStyle Normal
    if (-not $process.WaitForExit(30000)) {
        $process.Kill()
        throw 'Packaged smoke test failed: the application did not exit within 30 seconds.'
    }
    if ($process.ExitCode -ne 0) {
        throw "Packaged smoke test failed: exit code $($process.ExitCode). See $errorLog."
    }
}
finally {
    $env:PATH = $savedPath
}
Write-Host 'Packaged smoke test passed with no Qt folder on the PATH' -ForegroundColor Green

$bytes = (Get-ChildItem -LiteralPath $package -Recurse -File | Measure-Object -Property Length -Sum).Sum
$files = (Get-ChildItem -LiteralPath $package -Recurse -File).Count
Write-Host ("Package: {0} ({1} files, {2:N1} MB)" -f $package, $files, ($bytes / 1MB)) -ForegroundColor White
