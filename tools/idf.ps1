#Requires -Version 5.1
<#
    Run idf.py with ESP-IDF activated, wherever it happens to be installed.

    The Windows twin of tools/idf.sh, and it exists for the same reason: a
    freshly cloned project has nothing on PATH, so the VS Code tasks cannot
    call idf.py directly. The two scripts search the same way, prefer the same
    version and print the same lines - a change to one belongs in the other.

        powershell -ExecutionPolicy Bypass -File tools/idf.ps1 build
        powershell -ExecutionPolicy Bypass -File tools/idf.ps1 flash monitor
#>

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot

# main/idf_component.yml asks for >=5.5,<5.6. Several versions side by side is
# the normal state of an ESP-IDF machine, so a candidate whose path says 5.5 is
# taken ahead of one that does not, rather than the newest winning.
$want = '5.5'

function Test-IdfPath([string] $path) {
    if ([string]::IsNullOrWhiteSpace($path)) { return $false }
    return (Test-Path (Join-Path $path 'export.ps1')) -and
           (Test-Path (Join-Path $path 'tools/idf.py'))
}

$found = New-Object System.Collections.Generic.List[string]
function Add-Candidate([string] $path) {
    if (Test-IdfPath $path) {
        $full = (Resolve-Path -LiteralPath $path).Path
        if (-not $found.Contains($full)) { $found.Add($full) }
    }
}

Add-Candidate $env:IDF_PATH

# The build directory remembers the framework it was configured with, which is
# the one that can rebuild it without a full reconfigure.
$cache = Join-Path $root 'build/CMakeCache.txt'
if (Test-Path $cache) {
    $line = Select-String -Path $cache -Pattern '^IDF_PATH:PATH=(.*)$' | Select-Object -First 1
    if ($line) { Add-Candidate $line.Matches[0].Groups[1].Value }
}

# Already activated in this shell: idf.py sits in $IDF_PATH\tools.
$onPath = Get-Command idf.py -ErrorAction SilentlyContinue
if ($onPath) { Add-Candidate (Split-Path -Parent (Split-Path -Parent $onPath.Source)) }

# The usual install locations: the ESP-IDF Installation Manager, the VS Code
# extension, the Windows offline installer, and a hand-cloned framework.
foreach ($pattern in @(
    (Join-Path $env:USERPROFILE 'esp/v*/esp-idf'),
    (Join-Path $env:USERPROFILE 'esp/esp-idf-v*'),
    (Join-Path $env:USERPROFILE 'esp/esp-idf'),
    (Join-Path $env:USERPROFILE '.espressif/v*/esp-idf'),
    (Join-Path $env:USERPROFILE '.espressif/frameworks/esp-idf-v*'),
    'C:/Espressif/frameworks/esp-idf-v*',
    'C:/esp/esp-idf'
)) {
    # Resolve-Path rather than Get-ChildItem: a pattern with no wildcard in it
    # must resolve to the directory itself, not to what is inside it.
    foreach ($match in (Resolve-Path -Path $pattern -ErrorAction SilentlyContinue)) {
        Add-Candidate $match.Path
    }
}

$idf = $found | Where-Object { $_ -like "*$want*" } | Select-Object -First 1
if (-not $idf -and $found.Count -gt 0) {
    $idf = $found[0]
    Write-Host "tools/idf.ps1: using $idf; this project is built with ESP-IDF $want.x"
}

if (-not $idf) {
    Write-Host @'
tools/idf.ps1: no ESP-IDF installation found.

In VS Code: open the command palette (Ctrl+Shift+P) and run
"ESP-IDF: Configure ESP-IDF extension" - it downloads the framework and its
toolchain. Choose version 5.5.x.

Outside VS Code, install it by hand and either set IDF_PATH or run its
export.ps1 before this script:
https://docs.espressif.com/projects/esp-idf/en/v5.5.4/esp32s3/get-started/
'@
    exit 1
}

Write-Host "tools/idf.ps1: ESP-IDF $idf"

# export.ps1 prints a dozen lines about tool versions every time. Held back
# rather than discarded: it is also where a framework that was cloned but never
# had install.bat run for it says so, and that message is the whole diagnosis.
$log = [System.IO.Path]::GetTempFileName()
$strict = $ErrorActionPreference
# Relaxed across the dot-source only: export.ps1 writes non-terminating errors
# of its own on installations that work perfectly well, and under Stop each of
# them would abort activation. Whether it worked is decided below, by looking
# for the interpreter it is supposed to have put on PATH.
$ErrorActionPreference = 'Continue'
try {
    . (Join-Path $idf 'export.ps1') *> $log
} catch {
    Get-Content $log | Write-Host
    Remove-Item $log -ErrorAction SilentlyContinue
    Write-Host "tools/idf.ps1: export.ps1 failed - run install.bat in $idf first"
    exit 1
} finally {
    $ErrorActionPreference = $strict
}

# idf.py is run through Python rather than as a command of its own: whether a
# bare `idf.py` is executable depends on PATHEXT and on the .py association,
# and export.ps1 puts the framework's own interpreter first on PATH either way.
$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    Get-Content $log | Write-Host
    Remove-Item $log -ErrorAction SilentlyContinue
    Write-Host "tools/idf.ps1: no python on PATH after export.ps1 - run install.bat in $idf"
    exit 1
}
Remove-Item $log -ErrorAction SilentlyContinue

# Without a port, esptool probes every COM port in turn. One obvious candidate
# is taken as the answer; with several, idf.py is left to do its own thing,
# because guessing which board is the radio is worse than a slow probe.
if (-not $env:ESPPORT) {
    $serialcomm = 'HKLM:\HARDWARE\DEVICEMAP\SERIALCOMM'
    $ports = @()
    if (Test-Path $serialcomm) {
        $ports = @((Get-ItemProperty $serialcomm).PSObject.Properties |
                   Where-Object { $_.Value -is [string] -and $_.Value -match '^COM\d+$' } |
                   ForEach-Object { $_.Value } | Sort-Object -Unique)
    }
    if ($ports.Count -eq 1) {
        $env:ESPPORT = $ports[0]
        Write-Host "tools/idf.ps1: port $($ports[0])"
    } elseif ($ports.Count -gt 1) {
        Write-Host "tools/idf.ps1: several ports ($($ports -join ', ')); set ESPPORT to choose"
    }
}

Set-Location $root
& $python.Source (Join-Path $idf 'tools/idf.py') @args
exit $LASTEXITCODE
