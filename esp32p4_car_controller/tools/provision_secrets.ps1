param(
    [Parameter(Mandatory = $true)]
    [string]$Port,

    [string]$WifiSsid,
    [string]$WifiPass,
    [string]$QwenKey,
    [string]$QwenModel = "qwen3-omni-flash-realtime-2025-09-15",
    [string]$QwenEndpoint = "wss://dashscope.aliyuncs.com/api-ws/v1/realtime",
    [string]$NvsOffset = "0x9000",
    [string]$NvsSize = "0x6000"
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$ConfigDir = Join-Path $ProjectRoot "config"
$CsvPath = Join-Path $ConfigDir "nvs_secrets.local.csv"
$BinPath = Join-Path $ConfigDir "nvs_secrets.local.bin"

if (-not $env:IDF_PATH) {
    throw "IDF_PATH is not set. Please run this in ESP-IDF PowerShell."
}

$NvsGen = Join-Path $env:IDF_PATH "components\nvs_flash\nvs_partition_generator\nvs_partition_gen.py"
if (-not (Test-Path $NvsGen)) {
    throw "nvs_partition_gen.py not found: $NvsGen"
}

function Escape-CsvValue([string]$Value) {
    if ($null -eq $Value) {
        return ""
    }
    if (($Value.Contains(",")) -or ($Value.Contains('"')) -or ($Value.Contains("`n")) -or ($Value.Contains("`r"))) {
        return '"' + $Value.Replace('"', '""') + '"'
    }
    return $Value
}

function Set-SecretCsvValue([string[]]$Lines, [string]$Key, [string]$Value) {
    $Prefix = "$Key,data,string,"
    $EscapedValue = Escape-CsvValue $Value
    $Updated = New-Object System.Collections.Generic.List[string]
    $Found = $false

    foreach ($Line in $Lines) {
        if ($Line.StartsWith($Prefix)) {
            $Updated.Add("$Prefix$EscapedValue")
            $Found = $true
        } else {
            $Updated.Add($Line)
        }
    }

    if (-not $Found) {
        $Updated.Add("$Prefix$EscapedValue")
    }

    return $Updated.ToArray()
}

$NeedCreateCsv = ($WifiSsid -or $WifiPass -or $QwenKey)
if ($NeedCreateCsv) {
    if (-not $WifiSsid -or -not $WifiPass -or -not $QwenKey) {
        throw "When passing values, WifiSsid, WifiPass and QwenKey are all required."
    }

    $Lines = @(
        "key,type,encoding,value",
        "robot_secret,namespace,,",
        "wifi_ssid,data,string,$(Escape-CsvValue $WifiSsid)",
        "wifi_pass,data,string,$(Escape-CsvValue $WifiPass)",
        "qwen_key,data,string,$(Escape-CsvValue $QwenKey)",
        "qwen_model,data,string,$(Escape-CsvValue $QwenModel)",
        "qwen_endpoint,data,string,$(Escape-CsvValue $QwenEndpoint)"
    )
    Set-Content -LiteralPath $CsvPath -Value $Lines -Encoding ascii
    Write-Host "Wrote local secrets CSV: $CsvPath"
} elseif (-not (Test-Path $CsvPath)) {
    throw "Local secrets CSV does not exist. Pass WifiSsid/WifiPass/QwenKey or create $CsvPath from config\nvs_secrets.example.csv."
} elseif ($PSBoundParameters.ContainsKey("QwenModel") -or $PSBoundParameters.ContainsKey("QwenEndpoint")) {
    $Lines = Get-Content -LiteralPath $CsvPath
    if ($PSBoundParameters.ContainsKey("QwenModel")) {
        $Lines = Set-SecretCsvValue $Lines "qwen_model" $QwenModel
    }
    if ($PSBoundParameters.ContainsKey("QwenEndpoint")) {
        $Lines = Set-SecretCsvValue $Lines "qwen_endpoint" $QwenEndpoint
    }
    Set-Content -LiteralPath $CsvPath -Value $Lines -Encoding ascii
    Write-Host "Updated local secrets CSV: $CsvPath"
}

python $NvsGen generate $CsvPath $BinPath $NvsSize
if ($LASTEXITCODE -ne 0) {
    throw "NVS binary generation failed."
}

python -m esptool --chip esp32p4 -p $Port write_flash $NvsOffset $BinPath
if ($LASTEXITCODE -ne 0) {
    throw "NVS flash failed."
}

Write-Host "Secrets flashed to NVS at $NvsOffset."
Write-Host "Local files are git-ignored: $CsvPath and $BinPath"
