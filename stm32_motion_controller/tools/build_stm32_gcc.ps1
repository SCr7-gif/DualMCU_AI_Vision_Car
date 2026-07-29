$ErrorActionPreference = "Stop"

$ProjectDir = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$BuildDir = Join-Path $ProjectDir "build_gcc"
$OutElf = Join-Path $BuildDir "STM32_Car.elf"
$OutHex = Join-Path $BuildDir "STM32_Car.hex"
$OutBin = Join-Path $BuildDir "STM32_Car.bin"
$Linker = Join-Path $ProjectDir "STM32F103XB_FLASH.ld"
$ToolchainBin = "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin"
$Gcc = Join-Path $ToolchainBin "arm-none-eabi-gcc.exe"
$Objcopy = Join-Path $ToolchainBin "arm-none-eabi-objcopy.exe"
$Size = Join-Path $ToolchainBin "arm-none-eabi-size.exe"

if (-not (Test-Path $Gcc)) {
    throw "arm-none-eabi-gcc not found: $Gcc"
}

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Includes = @(
    "Core/Inc",
    "User",
    "User/App",
    "User/BSP",
    "User/Devices",
    "User/Protocol",
    "User/Utils",
    "Drivers/STM32F1xx_HAL_Driver/Inc",
    "Drivers/STM32F1xx_HAL_Driver/Inc/Legacy",
    "Drivers/CMSIS/Device/ST/STM32F1xx/Include",
    "Drivers/CMSIS/Include"
) | ForEach-Object { "-I" + (Join-Path $ProjectDir $_) }

$CommonFlags = @(
    "-mcpu=cortex-m3",
    "-mthumb",
    "-DUSE_HAL_DRIVER",
    "-DSTM32F103xB",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "-Wno-unused-parameter",
    "-Os"
) + $Includes

$Sources = @()
$Sources += Get-ChildItem -Path (Join-Path $ProjectDir "Core/Src") -Filter *.c
$Sources += Get-ChildItem -Path (Join-Path $ProjectDir "User") -Recurse -Filter *.c | Where-Object { $_.FullName -notmatch "\\TestOnly\\" }
$Sources += Get-ChildItem -Path (Join-Path $ProjectDir "Drivers/STM32F1xx_HAL_Driver/Src") -Filter *.c |
    Where-Object { $_.Name -notmatch "_template[.]c$" }

$Objects = @()
foreach ($src in $Sources) {
    $rel = Resolve-Path -Relative $src.FullName
    $objName = ($rel -replace '^[.][\\/]', '' -replace '[:\\/]', '_' -replace '[.]c$', '.o')
    $obj = Join-Path $BuildDir $objName
    & $Gcc @CommonFlags -c $src.FullName -o $obj
    if ($LASTEXITCODE -ne 0) {
        throw "compile failed: $($src.FullName)"
    }
    $Objects += $obj
}

$Startup = Join-Path $ProjectDir "Drivers/CMSIS/Device/ST/STM32F1xx/Source/Templates/gcc/startup_stm32f103xb.s"
$StartupObj = Join-Path $BuildDir "startup_stm32f103xb.o"
& $Gcc @CommonFlags -c $Startup -o $StartupObj
if ($LASTEXITCODE -ne 0) {
    throw "compile failed: $Startup"
}
$Objects += $StartupObj

$LinkFlags = @(
    "-mcpu=cortex-m3",
    "-mthumb",
    "-T$Linker",
    "-Wl,--gc-sections",
    "-Wl,-Map=$(Join-Path $BuildDir 'STM32_Car.map')",
    "--specs=nano.specs",
    "--specs=nosys.specs"
)

& $Gcc @Objects @LinkFlags -o $OutElf
if ($LASTEXITCODE -ne 0) {
    throw "link failed"
}

& $Objcopy -O ihex $OutElf $OutHex
& $Objcopy -O binary $OutElf $OutBin
& $Size $OutElf

Write-Host "Built:"
Write-Host "  $OutElf"
Write-Host "  $OutHex"
Write-Host "  $OutBin"
