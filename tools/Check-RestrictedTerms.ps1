#requires -Version 7
<#
.SYNOPSIS
    Fails when text contains a term from the restricted-terms list.

.DESCRIPTION
    The list is a file of case-insensitive regular expressions that lives outside
    the repository. This script never prints the list itself, only the location
    of a match.

    -Staged       Scan staged file names and added lines (used by the pre-commit hook).
    -MessageFile  Scan a commit message file (used by the commit-msg hook).
    -All          Scan every tracked text file.
    -Path         Scan every file under a folder on disk, tracked or not.
#>
[CmdletBinding(DefaultParameterSetName = 'Staged')]
param(
    [Parameter(ParameterSetName = 'Staged')]
    [switch]$Staged,

    [Parameter(ParameterSetName = 'Message', Mandatory)]
    [string]$MessageFile,

    [Parameter(ParameterSetName = 'All', Mandatory)]
    [switch]$All,

    [Parameter(ParameterSetName = 'Path', Mandatory)]
    [string]$Path
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Folders whose content is third-party text that this project does not author.
$excludedPathPattern = '^(licenses/|third_party/)'

$termsFile = if ($env:LMP_RESTRICTED_TERMS_FILE) {
    $env:LMP_RESTRICTED_TERMS_FILE
} else {
    Join-Path $env:USERPROFILE '.config\last-music-player\restricted-terms.txt'
}

if (-not (Test-Path -LiteralPath $termsFile)) {
    Write-Host "Restricted-terms check cannot run: list not found at $termsFile" -ForegroundColor Red
    exit 2
}

$patterns = @(Get-Content -LiteralPath $termsFile |
    ForEach-Object { $_.Trim() } |
    Where-Object { $_ -and -not $_.StartsWith('#') })

if ($patterns.Count -eq 0) {
    Write-Host "Restricted-terms check cannot run: the list at $termsFile is empty" -ForegroundColor Red
    exit 2
}

$options = [System.Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
    [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
$regex = [regex]::new((($patterns | ForEach-Object { "(?:$_)" }) -join '|'), $options)

$findings = [System.Collections.Generic.List[string]]::new()

function Add-Finding {
    param([string]$Location, [string]$Text)
    if ($regex.IsMatch($Text)) {
        $findings.Add($Location)
    }
}

function Invoke-Git {
    param([string[]]$Arguments)
    $output = & git -c core.quotepath=off @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "git $($Arguments -join ' ') failed with exit code $LASTEXITCODE"
    }
    return @($output)
}

switch ($PSCmdlet.ParameterSetName) {
    'Message' {
        $lineNumber = 0
        foreach ($line in Get-Content -LiteralPath $MessageFile) {
            $lineNumber++
            if ($line.StartsWith('#')) { continue }
            Add-Finding "commit message, line $lineNumber" $line
        }
    }

    'Staged' {
        $names = Invoke-Git @('diff', '--cached', '--name-only', '--diff-filter=ACMR')
        foreach ($name in $names) {
            if ($name -match $excludedPathPattern) { continue }
            Add-Finding "file name: $name" $name
        }

        $currentFile = ''
        $skipFile = $true
        $newLine = 0
        $diff = Invoke-Git @('diff', '--cached', '--unified=0', '--no-color', '--diff-filter=ACMR')
        foreach ($line in $diff) {
            if ($line.StartsWith('+++ ')) {
                $currentFile = $line.Substring(4) -replace '^b/', ''
                $skipFile = $currentFile -match $excludedPathPattern
                continue
            }
            if ($line -match '^@@ -\d+(?:,\d+)? \+(\d+)(?:,\d+)? @@') {
                $newLine = [int]$Matches[1]
                continue
            }
            if ($line.StartsWith('+')) {
                if (-not $skipFile) {
                    Add-Finding "${currentFile}:$newLine" $line.Substring(1)
                }
                $newLine++
            }
        }
    }

    'All' {
        # git grep -I lists only files git treats as text.
        $files = Invoke-Git @('grep', '-I', '-l', '-e', '')
        foreach ($file in $files) {
            if ($file -match $excludedPathPattern) { continue }
            Add-Finding "file name: $file" $file
            $lineNumber = 0
            foreach ($line in Get-Content -LiteralPath $file) {
                $lineNumber++
                Add-Finding "${file}:$lineNumber" $line
            }
        }
    }

    'Path' {
        $root = (Resolve-Path -LiteralPath $Path).Path
        foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
            $relative = $file.FullName.Substring($root.Length).TrimStart('\', '/')
            Add-Finding "file name: $relative" $relative
            $lineNumber = 0
            foreach ($line in Get-Content -LiteralPath $file.FullName) {
                $lineNumber++
                Add-Finding "${relative}:$lineNumber" $line
            }
        }
    }
}

if ($findings.Count -eq 0) {
    exit 0
}

Write-Host "Restricted term found in $($findings.Count) place(s):" -ForegroundColor Red
foreach ($finding in $findings) {
    Write-Host "  $finding" -ForegroundColor Yellow
}
Write-Host 'Reword these before committing. See the project secrecy rule.' -ForegroundColor Red
exit 1
