param(
    [Parameter(Mandatory)]
    [string]$ApiOrigin,

    [string]$FrontendOrigin = "",

    [string]$MediaOrigin = "",

    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [ValidateSet("x64")]
    [string]$Platform = "x64",

    [switch]$ValidateOnly
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

function Get-ValidatedAccountOrigin {
    param(
        [Parameter(Mandatory)]
        [string]$Value,

        [Parameter(Mandatory)]
        [string]$Name,

        [Parameter(Mandatory)]
        [bool]$AllowLoopbackHttp
    )

    if ([string]::IsNullOrWhiteSpace($Value)) {
        throw "$Name cannot be empty."
    }
    if ($Value -match '[^\x21-\x7E]' -or $Value.Contains('"') -or $Value.Contains("'") -or $Value.Contains('\')) {
        throw "$Name contains unsupported characters."
    }

    $uri = $null
    if (-not [Uri]::TryCreate($Value, [UriKind]::Absolute, [ref]$uri)) {
        throw "$Name must be an absolute URL origin."
    }
    if (-not [string]::IsNullOrEmpty($uri.UserInfo) -or
        -not [string]::IsNullOrEmpty($uri.Query) -or
        -not [string]::IsNullOrEmpty($uri.Fragment) -or
        ($uri.AbsolutePath -ne "" -and $uri.AbsolutePath -ne "/")) {
        throw "$Name must contain only a scheme, host, and optional port."
    }

    $https = $uri.Scheme -eq [Uri]::UriSchemeHttps
    $loopbackHttp = (
        $AllowLoopbackHttp -and
        $uri.Scheme -eq [Uri]::UriSchemeHttp -and
        ($uri.Host -eq "localhost" -or $uri.Host -eq "127.0.0.1" -or $uri.Host -eq "::1")
    )
    if (-not $https -and -not $loopbackHttp) {
        $requirement = "HTTPS"
        if ($AllowLoopbackHttp) {
            $requirement = "HTTPS, or loopback HTTP in Debug"
        }
        throw "$Name must use $requirement."
    }

    return $Value.TrimEnd('/')
}

$allowLoopbackHttp = $Configuration -eq "Debug"
$validatedApiOrigin = Get-ValidatedAccountOrigin `
    -Value $ApiOrigin `
    -Name "ApiOrigin" `
    -AllowLoopbackHttp $allowLoopbackHttp

$validatedFrontendOrigin = ""
if (-not [string]::IsNullOrWhiteSpace($FrontendOrigin)) {
    $validatedFrontendOrigin = Get-ValidatedAccountOrigin `
        -Value $FrontendOrigin `
        -Name "FrontendOrigin" `
        -AllowLoopbackHttp $allowLoopbackHttp
}

$validatedMediaOrigin = ""
if (-not [string]::IsNullOrWhiteSpace($MediaOrigin)) {
    $validatedMediaOrigin = Get-ValidatedAccountOrigin `
        -Value $MediaOrigin `
        -Name "MediaOrigin" `
        -AllowLoopbackHttp $allowLoopbackHttp
}

if ($ValidateOnly) {
    exit 0
}

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")
$project = Join-Path $repoRoot "Last Music Player.vcxproj"

$msbuild = Get-MSBuildPath

$properties = @(
    "/p:Configuration=$Configuration",
    "/p:Platform=$Platform",
    "/p:LastMusicAccountApiOrigin=$validatedApiOrigin",
    "/p:LastMusicAccountFrontendOrigin=$validatedFrontendOrigin",
    "/p:LastMusicAccountMediaOrigin=$validatedMediaOrigin"
)

& $msbuild $project /t:Rebuild @properties /m
exit $LASTEXITCODE
