#requires -Version 7
<#
.SYNOPSIS
    Copies the application's data files to .build\qt\userdata-copy, so the
    upgrade can be tried on real data without touching it.
.DESCRIPTION
    Copies the library database (with its -wal and -shm files), Settings.json,
    LastMusicState.json and Downloads.json from the data folder. The Offline,
    thumbs and cache folders are left out. Refuses to run while the
    application is running, so the database is not copied mid-write. Prints
    file names and sizes only, never their contents.

    Then run the probe on the copy:
        .build\qt\tests\Debug\userdata_probe.exe .build\qt\userdata-copy
.PARAMETER Source
    The data folder to copy. Defaults to "%LOCALAPPDATA%\Last Music Player".
#>
[CmdletBinding()]
param([string]$Source = (Join-Path $env:LOCALAPPDATA 'Last Music Player'))
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')

if (Get-Process -Name 'Last_Music_Player' -ErrorAction SilentlyContinue) {
    throw 'Last Music Player is running. Close it (also from the notification area) and run this again.'
}
if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
    throw "No data folder at '$Source'."
}

$destination = Join-Path (Get-RepoRoot) '.build\qt\userdata-copy'
if (Test-Path -LiteralPath $destination) {
    Remove-Item -LiteralPath $destination -Recurse -Force
}
New-Item -ItemType Directory -Path $destination | Out-Null

$names = 'lastmusic.db', 'lastmusic.db-wal', 'lastmusic.db-shm', 'Settings.json', 'LastMusicState.json', 'Downloads.json'
$copied = 0
foreach ($name in $names) {
    $file = Join-Path $Source $name
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        Write-Host ("  {0,-22} not present" -f $name) -ForegroundColor White
        continue
    }
    Copy-Item -LiteralPath $file -Destination $destination
    $copied++
    Write-Host ("  {0,-22} {1,12:N0} bytes" -f $name, (Get-Item -LiteralPath $file).Length) -ForegroundColor White
}
Write-Host "Copied $copied file(s) to $destination" -ForegroundColor Green
