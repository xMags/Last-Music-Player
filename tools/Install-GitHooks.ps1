#requires -Version 7
<#
.SYNOPSIS
    Installs the local git hooks that run the restricted-terms check.

.DESCRIPTION
    Hooks live in .git/hooks, which git never tracks, so this must be run once
    in every clone. It refuses to overwrite a hook it did not write.
#>
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = (& git rev-parse --show-toplevel)
if ($LASTEXITCODE -ne 0) {
    throw 'Not inside a git repository.'
}

$hooksDir = Join-Path $repoRoot '.git/hooks'
$marker = '# installed by tools/Install-GitHooks.ps1'

$hooks = @{
    'pre-commit' = 'exec pwsh -NoProfile -File tools/Check-RestrictedTerms.ps1 -Staged'
    'commit-msg' = 'exec pwsh -NoProfile -File tools/Check-RestrictedTerms.ps1 -MessageFile "$1"'
}

foreach ($name in $hooks.Keys) {
    $hookPath = Join-Path $hooksDir $name
    if ((Test-Path -LiteralPath $hookPath) -and
        -not (Select-String -LiteralPath $hookPath -SimpleMatch $marker -Quiet)) {
        throw "A different $name hook already exists at $hookPath. Remove it by hand first."
    }

    # Git for Windows runs hooks with sh, so the file needs LF line endings.
    $content = "#!/bin/sh`n$marker`n$($hooks[$name])`n"
    [System.IO.File]::WriteAllText($hookPath, $content, [System.Text.UTF8Encoding]::new($false))
    Write-Host "Installed $name hook" -ForegroundColor Green
}
