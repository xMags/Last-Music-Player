#requires -Version 7
<#
.SYNOPSIS
    Checks the packaged application's identity and exits non-zero on any
    mismatch.
.DESCRIPTION
    In .build\qt\package (tools\package.ps1 makes it), checks:
    - the executable name, Last_Music_Player.exe, which installs and
      shortcuts of version 1.1.0 refer to;
    - the version resource: file and product version 2.0.0.0, product name
      "Last Music Player", original file name;
    - an icon group in the executable's resources;
    - the embedded manifest: assembly version 2.0.0.0, Windows 10 declared,
      and no DPI setting (Qt sets per-monitor v2 itself; a declared one makes
      that call fail);
    - the AppUserModelID "LastMusicPlayer.App" and the single-instance mutex
      name "Local\LastMusicPlayer.SingleInstance" (the 1.1.0 values) in the
      executable;
    - at run time, with an empty data folder: the process is per-monitor v2
      DPI aware and holds the single-instance mutex.
#>
[CmdletBinding()]
param([string]$Package)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildEnvironment.ps1')
if (-not $Package) {
    $Package = Join-Path (Get-RepoRoot) '.build\qt\package'
}

$ExpectedVersion = '2.0.0.0'
$ExpectedProduct = 'Last Music Player'
$ExpectedExe = 'Last_Music_Player.exe'
$ExpectedAumid = 'LastMusicPlayer.App'
$ExpectedMutex = 'Local\LastMusicPlayer.SingleInstance'

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class PackageCheck {
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll")] static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr data);
    [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr module, IntPtr resource);
    delegate bool EnumNameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr param);
    [DllImport("kernel32.dll")] static extern bool EnumResourceNames(IntPtr module, IntPtr type, EnumNameProc proc, IntPtr param);
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode)] static extern IntPtr OpenMutex(uint access, bool inherit, string name);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(uint access, bool inherit, int id);
    [DllImport("user32.dll")] static extern IntPtr GetDpiAwarenessContextForProcess(IntPtr process);
    [DllImport("user32.dll")] static extern bool AreDpiAwarenessContextsEqual(IntPtr a, IntPtr b);
    const uint LoadAsData = 0x00000002 | 0x00000020; // DATAFILE | IMAGE_RESOURCE
    static readonly IntPtr RtGroupIcon = new IntPtr(14);
    static readonly IntPtr RtManifest = new IntPtr(24);

    public static int IconGroups(string path) {
        IntPtr module = LoadLibraryEx(path, IntPtr.Zero, LoadAsData);
        if (module == IntPtr.Zero) return -1;
        int count = 0;
        EnumResourceNames(module, RtGroupIcon, (m, t, n, p) => { count++; return true; }, IntPtr.Zero);
        FreeLibrary(module);
        return count;
    }

    public static string Manifest(string path) {
        IntPtr module = LoadLibraryEx(path, IntPtr.Zero, LoadAsData);
        if (module == IntPtr.Zero) return null;
        try {
            IntPtr resource = FindResource(module, new IntPtr(1), RtManifest);
            if (resource == IntPtr.Zero) return null;
            uint size = SizeofResource(module, resource);
            IntPtr data = LockResource(LoadResource(module, resource));
            byte[] bytes = new byte[size];
            Marshal.Copy(data, bytes, 0, (int)size);
            return Encoding.UTF8.GetString(bytes);
        } finally { FreeLibrary(module); }
    }

    public static bool MutexExists(string name) {
        IntPtr handle = OpenMutex(0x00100000, false, name); // SYNCHRONIZE
        if (handle == IntPtr.Zero) return false;
        CloseHandle(handle);
        return true;
    }

    public static bool PerMonitorV2(int processId) {
        IntPtr process = OpenProcess(0x1000, false, processId); // QUERY_LIMITED_INFORMATION
        if (process == IntPtr.Zero) return false;
        try {
            IntPtr context = GetDpiAwarenessContextForProcess(process);
            return AreDpiAwarenessContextsEqual(context, new IntPtr(-4)); // PER_MONITOR_AWARE_V2
        } finally { CloseHandle(process); }
    }
}
'@

$failures = [System.Collections.Generic.List[string]]::new()
function Test-That([bool]$ok, [string]$what) {
    if ($ok) {
        Write-Host "  ok    $what" -ForegroundColor Green
    } else {
        Write-Host "  FAIL  $what" -ForegroundColor Red
        $failures.Add($what)
    }
}

$exe = Join-Path $Package $ExpectedExe
Write-Host "Checking $exe" -ForegroundColor White
Test-That (Test-Path -LiteralPath $exe) "executable is named $ExpectedExe"
if (-not (Test-Path -LiteralPath $exe)) {
    exit 1
}

$info = (Get-Item -LiteralPath $exe).VersionInfo
Test-That ($info.FileVersion -eq $ExpectedVersion) "file version $ExpectedVersion (found $($info.FileVersion))"
Test-That ($info.ProductVersion -eq $ExpectedVersion) "product version $ExpectedVersion (found $($info.ProductVersion))"
Test-That ($info.ProductName -eq $ExpectedProduct) "product name '$ExpectedProduct' (found '$($info.ProductName)')"
Test-That ($info.OriginalFilename -eq $ExpectedExe) "original file name $ExpectedExe (found $($info.OriginalFilename))"

Test-That ([PackageCheck]::IconGroups($exe) -ge 1) 'an application icon in the resources'

$manifest = [PackageCheck]::Manifest($exe)
Test-That ($null -ne $manifest) 'an embedded manifest'
if ($manifest) {
    Test-That ($manifest -match 'assemblyIdentity[^>]*version="2\.0\.0\.0"') 'manifest assembly version 2.0.0.0'
    Test-That ($manifest -match '8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a') 'manifest declares Windows 10 and later'
    Test-That ($manifest -notmatch 'dpiAware') 'manifest leaves DPI awareness to Qt'
}

$text = [System.Text.Encoding]::Unicode.GetString([System.IO.File]::ReadAllBytes($exe))
Test-That ($text.Contains($ExpectedAumid)) "AppUserModelID '$ExpectedAumid' in the executable"
Test-That ($text.Contains($ExpectedMutex)) "single-instance mutex '$ExpectedMutex' in the executable"

# Run time, on an empty data folder so no one's data is touched.
Test-That (-not [PackageCheck]::MutexExists($ExpectedMutex)) 'no other copy of the application is running'
$dataFolder = Join-Path ([System.IO.Path]::GetTempPath()) ("lmp-verify-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dataFolder | Out-Null
$savedLocal = $env:LOCALAPPDATA
try {
    $env:LOCALAPPDATA = $dataFolder
    $process = Start-Process -FilePath $exe -PassThru
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline -and -not [PackageCheck]::MutexExists($ExpectedMutex)) {
        Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Seconds 2
    Test-That ([PackageCheck]::MutexExists($ExpectedMutex)) 'the running application holds the single-instance mutex'
    Test-That ([PackageCheck]::PerMonitorV2($process.Id)) 'the running application is per-monitor v2 DPI aware'
    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
    $process.WaitForExit(10000) | Out-Null
}
finally {
    $env:LOCALAPPDATA = $savedLocal
    Remove-Item -LiteralPath $dataFolder -Recurse -Force -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
    Write-Host "$($failures.Count) check(s) failed" -ForegroundColor Red
    exit 1
}
Write-Host 'Package identity verified' -ForegroundColor Green
exit 0
