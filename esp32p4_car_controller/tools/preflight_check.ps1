$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$RepoRoot = Split-Path -Parent $ProjectRoot

function Require-File([string]$Path) {
    if (-not (Test-Path $Path)) {
        throw "Missing required file: $Path"
    }
}

Write-Host "ESP32-P4 preflight check"
Write-Host "Project: $ProjectRoot"

$RequiredFiles = @(
    "CMakeLists.txt",
    "main\CMakeLists.txt",
    "main\Kconfig.projbuild",
    "main\idf_component.yml",
    "main\main.c",
    "main\network_manager.c",
    "main\qwen_realtime_client.c",
    "main\camera_ov5647.c",
    "main\board_diagnostics.c",
    "config\nvs_secrets.example.csv",
    "tools\provision_secrets.ps1",
    "tools\build_esp32p4.ps1"
)

foreach ($File in $RequiredFiles) {
    Require-File (Join-Path $ProjectRoot $File)
}

$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $ProjectRoot "tools\provision_secrets.ps1"), [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) {
    $errors | ForEach-Object { Write-Host $_.Message }
    throw "provision_secrets.ps1 syntax check failed"
}

$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseFile((Join-Path $ProjectRoot "tools\build_esp32p4.ps1"), [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) {
    $errors | ForEach-Object { Write-Host $_.Message }
    throw "build_esp32p4.ps1 syntax check failed"
}

$ForbiddenPatterns = @(
    "sk-[A-Za-z0-9_\.-]{20,}",
    "Authorization: Bearer [A-Za-z0-9_\.-]+"
)

$KnownSecretMarkers = @(
    (-join ([char[]](66, 52, 49, 48, 95, 53, 71))),
    (-join ([char[]](56, 56, 56, 56, 56, 56, 56, 56))),
    (-join ([char[]](72, 46, 82, 89, 76, 88, 88, 77, 77)))
)

$TextExtensions = @(
    ".c", ".h", ".cpp", ".hpp", ".py", ".ps1", ".md", ".txt", ".csv",
    ".ini", ".yml", ".yaml", ".json", ".cmake", ".xml", ".html"
)

$SearchFiles = Get-ChildItem -Path $RepoRoot -Recurse -File |
    Where-Object {
        $TextExtensions -contains $_.Extension.ToLowerInvariant() -and
        $_.FullName -notmatch "\\build\\" -and
        $_.FullName -notmatch "\\managed_components\\" -and
        $_.FullName -notmatch "nvs_secrets(\.local)?\.(csv|bin)$"
    }

foreach ($Pattern in $ForbiddenPatterns) {
    $Hit = $SearchFiles | Select-String -Pattern $Pattern -List -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Hit) {
        throw "Secret-like text found in tracked project area: $($Hit.Path)"
    }
}

foreach ($Marker in $KnownSecretMarkers) {
    $Hit = $SearchFiles | Select-String -SimpleMatch $Marker -List -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($Hit) {
        throw "Known local secret marker found in tracked project area: $($Hit.Path)"
    }
}

$Idf = Get-Command idf.py -ErrorAction SilentlyContinue
if ($Idf) {
    Write-Host "idf.py found: $($Idf.Source)"
} else {
    Write-Host "idf.py not found in this shell. Open ESP-IDF PowerShell before build."
}

Write-Host "Preflight OK"
