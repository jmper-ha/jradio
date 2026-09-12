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

# The version this project is built and verified with, and the one
# dependencies.lock names.
#
# Pinned exactly rather than by family, because "5.5" is not one answer on a
# machine that has ever upgraded: two patch versions sit side by side, the
# globs below find both, and taking whichever came first meant the VS Code
# tasks quietly built on the older one while an activated terminal built on the
# newer. See tools/idf.sh, which this mirrors.
$want = '5.5.5'
# What main/idf_component.yml actually allows, >=5.5,<5.6. The fallback when
# the pinned version is not installed.
$wantFamily = '5.5'

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
        return $full
    }
    return $null
}
# What EIM's manifest says about each install it made, by resolved path -
# the activation script it wrote for that version, used below.
$manifestEntry = @{}

# A candidate like any other, and deliberately not an override: inside VS Code
# this variable is not a person's choice at all - the ESP-IDF extension exports
# whatever idf.currentSetup happens to name into the task's environment.
# Letting it win is what made the pin above useless in the one place it was
# written for. JRADIO_IDF below is the override, and nothing sets that by
# accident.
Add-Candidate $env:IDF_PATH

# The build directory used to be a candidate too: it recorded IDF_PATH in its
# CMakeCache, and reusing that avoided a reconfigure. As of 5.5.5 the cache no
# longer carries the variable at all, and what it does carry can name two
# different versions at once after a build on each - which is the state that
# motivated the pin above, not a source to trust.

# Already activated in this shell: idf.py sits in $IDF_PATH\tools.
$onPath = Get-Command idf.py -ErrorAction SilentlyContinue
if ($onPath) { Add-Candidate (Split-Path -Parent (Split-Path -Parent $onPath.Source)) }

# What the ESP-IDF Installation Manager wrote down: eim_idf.json lists every
# version it installed with its path, wherever the user pointed it. This is the
# file the VS Code extension reads to know the same thing, and it is what
# catches an install on another drive that no pattern below would - a user's
# build failed with "no ESP-IDF installation found" while Doctor showed 5.5.5,
# because the framework was on the disk the project was on, not under C:. The
# extension exports IDF_TOOLS_PATH into the task, so a manifest kept somewhere
# other than the default is found through that.
$manifests = New-Object System.Collections.Generic.List[string]
if ($env:IDF_TOOLS_PATH) { $manifests.Add((Join-Path $env:IDF_TOOLS_PATH 'eim_idf.json')) }
$manifests.Add('C:/Espressif/tools/eim_idf.json')
$manifests.Add((Join-Path $env:USERPROFILE '.espressif/tools/eim_idf.json'))
foreach ($manifest in $manifests) {
    if (-not (Test-Path -LiteralPath $manifest)) { continue }
    try {
        $listed = (Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json).idfInstalled
    } catch {
        Write-Host "tools/idf.ps1: could not read $manifest; looking elsewhere"
        continue
    }
    foreach ($entry in @($listed)) {
        if (-not $entry.path) { continue }
        $full = Add-Candidate $entry.path
        if ($full -and -not $manifestEntry.ContainsKey($full)) { $manifestEntry[$full] = $entry }
    }
}

# The usual install locations: the ESP-IDF Installation Manager (C:\esp\v*
# by default, .espressif under the profile when told), the VS Code extension,
# the Windows offline installer, and a hand-cloned framework.
foreach ($pattern in @(
    'C:/esp/v*/esp-idf',
    'C:/Espressif/v*/esp-idf',
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

# JRADIO_IDF is the way to build with another version on purpose: a variable of
# this project's own, because every general-purpose one - IDF_PATH first among
# them - is already being set by somebody else's tooling.
$idf = $null
if ($env:JRADIO_IDF) {
    if (-not (Test-IdfPath $env:JRADIO_IDF)) {
        Write-Host "tools/idf.ps1: JRADIO_IDF=$($env:JRADIO_IDF) is not an ESP-IDF checkout"
        exit 1
    }
    $idf = (Resolve-Path -LiteralPath $env:JRADIO_IDF).Path
} else {
    $idf = $found | Where-Object { $_ -like "*$want*" } | Select-Object -First 1
    if (-not $idf) {
        $idf = $found | Where-Object { $_ -like "*$wantFamily*" } | Select-Object -First 1
        if ($idf) {
            Write-Host "tools/idf.ps1: ESP-IDF $want is not installed; using $idf"
        } elseif ($found.Count -gt 0) {
            $idf = $found[0]
            Write-Host "tools/idf.ps1: using $idf; this project is built with ESP-IDF $want"
        }
    }
}

# Says so rather than leaving the difference to be discovered in a build error:
# a stale IDF_PATH is exactly what this script now steps around.
if ($idf -and $env:IDF_PATH -and $env:IDF_PATH -ne $idf) {
    Write-Host "tools/idf.ps1: ignoring IDF_PATH=$($env:IDF_PATH); set JRADIO_IDF to override"
}

if (-not $idf) {
    Write-Host @'
tools/idf.ps1: no ESP-IDF installation found.

In VS Code: open the command palette (F1) and run
"ESP-IDF: Open ESP-IDF Installation Manager" - it downloads the installer,
which installs the framework and its toolchain. Choose version 5.5.5, then
run "ESP-IDF: Select Current ESP-IDF Version" and pick it.

Outside VS Code, install it by hand and either set IDF_PATH or run its
export.ps1 before this script:
https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32s3/get-started/
'@
    exit 1
}

Write-Host "tools/idf.ps1: ESP-IDF $idf"

# An install made by EIM is activated by the script EIM wrote for it
# (Microsoft.PowerShell_profile.ps1 beside the framework), not by export.ps1.
# The two do not agree about the layout: EIM keeps the tools in the directory
# it calls IDF_TOOLS_PATH (C:\Espressif\tools) with the venv under python\
# and its constraints file beside them, while idf_tools.py appends \tools to
# that variable and looks for a python_env under .espressif in the profile -
# a user's build stopped in export.ps1 with "Python virtual environment ...
# not found" while Doctor listed the venv in C:\Espressif. Run with -e, the
# script prints its environment as KEY=VALUE lines and changes nothing, which
# is how the VS Code extension reads it too. It prints some of them with
# Write-Host, hence the merge of every stream.
$activated = $false
if ($manifestEntry.ContainsKey($idf)) {
    $script = $manifestEntry[$idf].activationScript
    if ($script -and (Test-Path -LiteralPath $script)) {
        $lines = @()
        try { $lines = @(& $script -e *>&1 | ForEach-Object { "$_" }) } catch { $lines = @() }
        foreach ($line in $lines) {
            $at = $line.IndexOf('=')
            if ($at -lt 1) { continue }
            $key = $line.Substring(0, $at)
            $value = $line.Substring($at + 1)
            if ($key -eq 'PATH') { $env:PATH = "$value;$env:PATH" }
            elseif ($key -match '^[A-Z_]+$' -and $value) { Set-Item -Path "env:$key" -Value $value }
        }
        if ($env:IDF_PYTHON_ENV_PATH -and (Test-Path -LiteralPath (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'))) {
            Write-Host "tools/idf.ps1: activated by $script"
            $activated = $true
        }
    }
}

if (-not $activated) {
    # export.ps1 prints a dozen lines about tool versions every time. Held back
    # rather than discarded: it is also where a framework that was cloned but
    # never had install.bat run for it says so, and that message is the whole
    # diagnosis.
    $log = [System.IO.Path]::GetTempFileName()
    $strict = $ErrorActionPreference
    # Relaxed across the dot-source only: export.ps1 writes non-terminating
    # errors of its own on installations that work perfectly well, and under
    # Stop each of them would abort activation. Whether it worked is decided
    # below, by looking for the interpreter it is supposed to have set up.
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
    if (-not $env:IDF_PYTHON_ENV_PATH -or
        -not (Test-Path -LiteralPath (Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'))) {
        Get-Content $log | Write-Host
        Remove-Item $log -ErrorAction SilentlyContinue
        Write-Host "tools/idf.ps1: no Python environment after export.ps1 - run install.bat in $idf"
        exit 1
    }
    Remove-Item $log -ErrorAction SilentlyContinue
}

# idf.py is run through the framework's own interpreter rather than as a
# command of its own: whether a bare `idf.py` is executable depends on PATHEXT
# and on the .py association, and EIM's activation defines it as an alias,
# which a script does not see.
$python = Join-Path $env:IDF_PYTHON_ENV_PATH 'Scripts/python.exe'

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
& $python (Join-Path $idf 'tools/idf.py') @args
exit $LASTEXITCODE
