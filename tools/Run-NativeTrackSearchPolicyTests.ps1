param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64")]
    [string]$Platform = "x64"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$project = Join-Path $repoRoot "tests\native\TrackSearchPolicyTests.vcxproj"

$msbuildCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\amd64\MSBuild.exe",
    "C:\Program Files\Microsoft Visual Studio\18\Professional\MSBuild\Current\Bin\MSBuild.exe"
)

$msbuild = $msbuildCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $msbuild) {
    $command = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($command) {
        $msbuild = $command.Source
    }
}

if (-not $msbuild) {
    throw "MSBuild was not found. Install Visual Studio Build Tools with the C++ workload."
}

& $msbuild $project /p:Configuration=$Configuration /p:Platform=$Platform /m
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$exe = Join-Path $repoRoot "tests\native\$Platform\$Configuration\TrackSearchPolicyTests.exe"
& $exe
exit $LASTEXITCODE
