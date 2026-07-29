param(
    [string]$Port,
    [switch]$Menuconfig,
    [switch]$Flash,
    [switch]$Monitor,
    [switch]$Fullclean,
    [switch]$SkipPreflight
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $ProjectRoot

if (-not $SkipPreflight) {
    & (Join-Path $ProjectRoot "tools\preflight_check.ps1")
}

$Idf = Get-Command idf.py -ErrorAction SilentlyContinue
if (-not $Idf) {
    throw "idf.py not found. Open ESP-IDF PowerShell, then run this script again."
}

if ($Fullclean) {
    idf.py fullclean
    if ($LASTEXITCODE -ne 0) {
        throw "idf.py fullclean failed."
    }
}

idf.py set-target esp32p4
if ($LASTEXITCODE -ne 0) {
    throw "idf.py set-target esp32p4 failed."
}

if ($Menuconfig) {
    idf.py menuconfig
    if ($LASTEXITCODE -ne 0) {
        throw "idf.py menuconfig failed."
    }
}

idf.py build
if ($LASTEXITCODE -ne 0) {
    throw "idf.py build failed."
}

if ($Flash -or $Monitor) {
    if (-not $Port) {
        throw "Port is required when using -Flash or -Monitor. Example: -Port COM8"
    }

    $PreviousEspPort = $env:ESPPORT
    if ($PreviousEspPort -and ($PreviousEspPort -ne $Port)) {
        Write-Host "Clearing ESPPORT=$PreviousEspPort because -Port $Port was provided."
        Remove-Item Env:\ESPPORT -ErrorAction SilentlyContinue
    }

    if ($Flash -and $Monitor) {
        idf.py -p $Port flash monitor
    } elseif ($Flash) {
        idf.py -p $Port flash
    } else {
        idf.py -p $Port monitor
    }

    if ($LASTEXITCODE -ne 0) {
        throw "idf.py flash/monitor command failed."
    }
}
