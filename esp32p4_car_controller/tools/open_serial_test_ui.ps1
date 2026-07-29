$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$UiScript = Join-Path $ScriptDir "serial_test_ui.py"
$EspPython = "C:\Users\Administrator\.espressif\python_env\idf5.5_py3.13_env\Scripts\python.exe"

if (-not (Test-Path $EspPython)) {
    throw "ESP-IDF Python was not found: $EspPython"
}

if (-not (Test-Path $UiScript)) {
    throw "UI script was not found: $UiScript"
}

& $EspPython $UiScript
