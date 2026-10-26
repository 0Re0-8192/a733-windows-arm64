param(
    [string]$RelayPort = "COM11",
    [string]$RelayScript = "$env:A733_WORKDIR\board\usb_relay_control.ps1",
    [string]$LoadScript = "$env:A733_WORKDIR\board\load_fit_spl_capture_uart.ps1",
    [string]$FelExe = "$env:A733_WORKDIR\board\artifacts\sunxi-fel-a733-highsram.exe",
    [int]$OffSeconds = 3,
    [int]$FelPollSeconds = 35,
    [int]$UmsPollSeconds = 35,
    [string]$Label = "relay_cycle_auto_ums"
)

$ErrorActionPreference = "Stop"

foreach ($path in @($RelayScript, $LoadScript, $FelExe)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required file not found: $path"
    }
}

function Test-Fel {
    $oldErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        $out = & $FelExe version 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldErrorActionPreference
    }
    [pscustomobject]@{
        ExitCode = $code
        Output = (($out | Out-String).Trim())
        Ok = ($code -eq 0)
    }
}

$start = Get-Date
$relayOff = & $RelayScript -Port $RelayPort -Action off
Start-Sleep -Seconds $OffSeconds
$relayOn = & $RelayScript -Port $RelayPort -Action on

$deadline = (Get-Date).AddSeconds($FelPollSeconds)
$fel = $null
do {
    $fel = Test-Fel
    if ($fel.Ok) {
        break
    }
    Start-Sleep -Milliseconds 500
} while ((Get-Date) -lt $deadline)

if (-not $fel.Ok) {
    throw "FEL did not appear after relay cycle. Last sunxi-fel output: $($fel.Output)"
}

$load = & $LoadScript -FelExe $FelExe -Label $Label -PostSplPollSeconds $UmsPollSeconds

[pscustomobject]@{
    Started = $start.ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    Finished = (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz")
    RelayPort = $RelayPort
    RelayOff = $relayOff
    RelayOn = $relayOn
    Fel = $fel
    Load = $load
}
