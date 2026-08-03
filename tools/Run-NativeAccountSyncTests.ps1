param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [switch]$ConfiguredMatrix
)

$ErrorActionPreference = "Stop"

function Get-MSBuildPath {
    $vswhereCandidates = @(
        (Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio\Installer\vswhere.exe")
    )
    foreach ($vswhere in $vswhereCandidates) {
        if (-not (Test-Path -LiteralPath $vswhere)) {
            continue
        }

        foreach ($pattern in @(
            "MSBuild\**\Bin\amd64\MSBuild.exe",
            "MSBuild\**\Bin\MSBuild.exe"
        )) {
            $found = & $vswhere -latest -products * `
                -requires Microsoft.Component.MSBuild `
                -find $pattern 2>$null | Select-Object -First 1
            if (-not [string]::IsNullOrWhiteSpace($found)) {
                return $found
            }
        }
    }

    $candidates = foreach ($version in @("18", "2022")) {
        foreach ($edition in @("Community", "Professional", "Enterprise", "BuildTools")) {
            "C:\Program Files\Microsoft Visual Studio\$version\$edition\MSBuild\Current\Bin\amd64\MSBuild.exe"
            "C:\Program Files\Microsoft Visual Studio\$version\$edition\MSBuild\Current\Bin\MSBuild.exe"
        }
    }
    $foundCandidate = $candidates | Where-Object {
        Test-Path -LiteralPath $_
    } | Select-Object -First 1
    if ($foundCandidate) {
        return $foundCandidate
    }

    $command = Get-Command msbuild -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }
    throw "MSBuild was not found. Install Visual Studio 2022 or later with the C++ workload."
}

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$project = Join-Path $repoRoot "tests\native\NativeAccountSyncTests.vcxproj"
$accountBuildScript = Join-Path $repoRoot "tools\Build-WithAccountIntegration.ps1"

$msbuild = Get-MSBuildPath
$stableMsBuildProperties = @(
    "/p:DebugInformationFormat=ProgramDatabase",
    "/p:SupportJustMyCode=false",
    "/p:TrackFileAccess=false",
    "/p:UseMultiToolTask=false"
)

function Invoke-NativeAccountVariant {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string[]]$AdditionalProperties
    )

    $variantRoot = Join-Path $repoRoot "tests\native\$Platform\$Configuration\account-variants\$Name"
    $outDir = (Join-Path $variantRoot "bin") + "\"
    $intDir = (Join-Path $variantRoot "obj") + "\"
    $arguments = @(
        $project,
        "/t:Rebuild",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:OutDir=$outDir",
        "/p:IntDir=$intDir"
    ) + $stableMsBuildProperties + $AdditionalProperties

    & $msbuild @arguments /m
    if ($LASTEXITCODE -ne 0) {
        throw "Native account test variant '$Name' did not build."
    }

    $exe = Join-Path $outDir "NativeAccountSyncTests.exe"
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Native account test variant '$Name' failed."
    }
}

function Assert-NativeAccountBuildFails {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string[]]$AdditionalProperties
    )

    $variantRoot = Join-Path $repoRoot "tests\native\$Platform\$Configuration\account-variants\invalid-$Name"
    $outDir = (Join-Path $variantRoot "bin") + "\"
    $intDir = (Join-Path $variantRoot "obj") + "\"
    $arguments = @(
        $project,
        "/t:Rebuild",
        "/p:Configuration=$Configuration",
        "/p:Platform=$Platform",
        "/p:OutDir=$outDir",
        "/p:IntDir=$intDir"
    ) + $stableMsBuildProperties + $AdditionalProperties

    & $msbuild @arguments /m *> $null
    if ($LASTEXITCODE -eq 0) {
        throw "Invalid native account configuration '$Name' unexpectedly built."
    }
}

function Assert-AccountBuildScriptRejects {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    $pwsh = Join-Path $PSHOME "pwsh.exe"
    & $pwsh -NoProfile -NonInteractive -File $accountBuildScript @Arguments *> $null
    if ($LASTEXITCODE -eq 0) {
        throw "Account build script unexpectedly accepted '$Name'."
    }
}

function Assert-AccountBuildScriptAccepts {
    param(
        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [string[]]$Arguments
    )

    $pwsh = Join-Path $PSHOME "pwsh.exe"
    & $pwsh -NoProfile -NonInteractive -File $accountBuildScript @Arguments *> $null
    if ($LASTEXITCODE -ne 0) {
        throw "Account build script unexpectedly rejected '$Name'."
    }
}

$blankProperties = @(
    "/p:LastMusicAccountApiOrigin=",
    "/p:LastMusicAccountFrontendOrigin=",
    "/p:LastMusicAccountMediaOrigin=",
    "/p:LastMusicAccountTestExpectation=Blank"
)

Invoke-NativeAccountVariant -Name "blank" -AdditionalProperties $blankProperties

if (-not $ConfiguredMatrix) {
    exit 0
}

Invoke-NativeAccountVariant -Name "api-only" -AdditionalProperties @(
    "/p:LastMusicAccountApiOrigin=https://api.account.example.test",
    "/p:LastMusicAccountFrontendOrigin=",
    "/p:LastMusicAccountMediaOrigin=",
    "/p:LastMusicAccountTestExpectation=ApiOnly"
)

Invoke-NativeAccountVariant -Name "full" -AdditionalProperties @(
    "/p:LastMusicAccountApiOrigin=https://api.account.example.test",
    "/p:LastMusicAccountFrontendOrigin=https://account.example.test",
    "/p:LastMusicAccountMediaOrigin=https://media.account.example.test",
    "/p:LastMusicAccountTestExpectation=Full"
)

Assert-NativeAccountBuildFails -Name "frontend-without-api" -AdditionalProperties @(
    "/p:LastMusicAccountApiOrigin=",
    "/p:LastMusicAccountFrontendOrigin=https://account.example.test",
    "/p:LastMusicAccountMediaOrigin="
)

Assert-NativeAccountBuildFails -Name "media-without-api" -AdditionalProperties @(
    "/p:LastMusicAccountApiOrigin=",
    "/p:LastMusicAccountFrontendOrigin=",
    "/p:LastMusicAccountMediaOrigin=https://media.account.example.test"
)

if ($Configuration -eq "Release") {
    Assert-NativeAccountBuildFails -Name "release-loopback-http" -AdditionalProperties @(
        "/p:LastMusicAccountApiOrigin=http://127.0.0.1:8787",
        "/p:LastMusicAccountFrontendOrigin=",
        "/p:LastMusicAccountMediaOrigin="
    )
}

Assert-AccountBuildScriptAccepts -Name "loopback HTTP in Debug" -Arguments @(
    "-ApiOrigin", "http://127.0.0.1:8787",
    "-Configuration", "Debug",
    "-Platform", $Platform,
    "-ValidateOnly"
)
Assert-AccountBuildScriptRejects -Name "public HTTP in Debug" -Arguments @(
    "-ApiOrigin", "http://account.example.test",
    "-Configuration", "Debug",
    "-Platform", $Platform
)
Assert-AccountBuildScriptRejects -Name "origin path" -Arguments @(
    "-ApiOrigin", "https://account.example.test/path",
    "-Configuration", "Release",
    "-Platform", $Platform
)
Assert-AccountBuildScriptRejects -Name "loopback HTTP in Release" -Arguments @(
    "-ApiOrigin", "http://127.0.0.1:8787",
    "-Configuration", "Release",
    "-Platform", $Platform
)

exit 0
