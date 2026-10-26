param(
  [string]$WorkDir = "$env:A733_WORKDIR\board\svc_trim_20261007\boot1",
  [int]$CaptureSeconds = 600
)
$ErrorActionPreference='Stop'
$root="$env:A733_WORKDIR"
$us=Join-Path $root 'scratch\usb_shortening'
$capture=Join-Path $root 'scratch\Capture-UartWithRelay.ps1'
$ffmpeg=(Get-Command ffmpeg.exe -ErrorAction Stop).Source
if(!(Test-Path -LiteralPath $capture)){ throw "UART capture script missing $capture" }
if(!(Test-Path -LiteralPath $ffmpeg)){ throw "ffmpeg missing $ffmpeg" }
Set-Location $us
$normalOut = powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $us 'switch_boot0_mode.ps1') -Mode normal -Apply -AcknowledgeOneWayNormalBoot
$statusOut = powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $us 'switch_boot0_mode.ps1') -Mode status
$bootPrep=[ordered]@{NormalOut=@($normalOut); StatusOut=@($statusOut); Time=(Get-Date).ToString('o')}
$bootPrep | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $WorkDir 'h1042_boot0_normal.json') -Encoding UTF8
$tag='h1042_boot'
$beforeStates=@(Get-ChildItem -LiteralPath $WorkDir -Filter ($tag + '_*.state.json') -ErrorAction SilentlyContinue | ForEach-Object FullName)
$outLog=Join-Path $WorkDir 'h1042_uart_capture_stdout.log'
$errLog=Join-Path $WorkDir 'h1042_uart_capture_stderr.log'
$args=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$capture,'-SecondsAfterPowerOn',[string]$CaptureSeconds,'-OutputDir',$WorkDir,'-Tag',$tag,'-StopOnBugcheck')
$proc=Start-Process -FilePath 'powershell.exe' -ArgumentList $args -PassThru -WindowStyle Hidden -RedirectStandardOutput $outLog -RedirectStandardError $errLog
$null=$proc.Handle
$statePath=$null
$relayOn=$null
$deadline=(Get-Date).AddSeconds(90)
while((Get-Date) -lt $deadline){
  $states=@(Get-ChildItem -LiteralPath $WorkDir -Filter ($tag + '_*.state.json') -ErrorAction SilentlyContinue | Where-Object { $beforeStates -notcontains $_.FullName } | Sort-Object LastWriteTime -Descending)
  if($states.Count -gt 0){
    $statePath=$states[0].FullName
    try{
      $s=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
      if($s.RelayOnTime){ $relayOn=[DateTimeOffset]::Parse([string]$s.RelayOnTime); break }
    } catch {}
  }
  Start-Sleep -Milliseconds 250
}
if($null -eq $relayOn){ throw "UART capture did not report RelayOnTime" }
$screenRows=@()
$screenAttempts=@()
$screenSequence=0
$screenDeadline=$relayOn.AddSeconds($CaptureSeconds + 5)
while([DateTimeOffset]::Now -lt $screenDeadline){
  if(Test-Path -LiteralPath (Join-Path $WorkDir 'stop_screens')){ break }
  $screenSequence += 1
  $screenPath=Join-Path $WorkDir ('h1042_screen_{0:D3}.png' -f $screenSequence)
  if(Test-Path -LiteralPath $screenPath){ throw "Refusing to overwrite $screenPath" }
  $before=[DateTimeOffset]::Now
  $screenOut=Join-Path $WorkDir ('screen_{0:D3}.stdout.log' -f $screenSequence)
  $screenErr=Join-Path $WorkDir ('screen_{0:D3}.stderr.log' -f $screenSequence)
  $screenArgs=@('-hide_banner','-loglevel','error','-f','dshow','-i','"video=Cam Link 4K"','-frames:v','1','-update','1','-n',$screenPath)
  $screenProc=Start-Process -FilePath $ffmpeg -ArgumentList $screenArgs -WindowStyle Hidden -PassThru -RedirectStandardOutput $screenOut -RedirectStandardError $screenErr
  $null=$screenProc.Handle
  $screenFinished=$screenProc.WaitForExit(7000)
  if(!$screenFinished){$screenProc.Kill();$screenProc.WaitForExit()}
  $after=[DateTimeOffset]::Now
  $screenOk=$screenFinished -and $screenProc.ExitCode -eq 0 -and (Test-Path -LiteralPath $screenPath)
  $screenAttempts += [ordered]@{Path=$screenPath;BeforeTime=$before.ToString('o');AfterTime=$after.ToString('o');TimedOut=(!$screenFinished);ExitCode=$screenProc.ExitCode;Success=$screenOk}
  $screenAttempts | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $WorkDir 'h1042_screen_attempts.json') -Encoding UTF8
  if($screenOk){
    $screenRows += [ordered]@{Path=$screenPath; BeforeTime=$before.ToString('o'); AfterTime=$after.ToString('o'); SecondsBefore=($before-$relayOn).TotalSeconds; SecondsAfter=($after-$relayOn).TotalSeconds; Sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $screenPath).Hash}
    $screenRows | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $WorkDir 'h1042_screens.json') -Encoding UTF8
  }
  if(($screenDeadline-[DateTimeOffset]::Now).TotalSeconds -lt 20){ break }
  Start-Sleep -Seconds 20
}
$proc.WaitForExit()
$state=Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json
$result=[ordered]@{StatePath=$statePath; ProcessExit=$proc.ExitCode; UartState=$state; ScreenCount=$screenRows.Count; Screens=$screenRows; ScreenAttempts=$screenAttempts; Boot0=$bootPrep; Completed=(Get-Date).ToString('o')}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $WorkDir 'h1042_capture_result.json') -Encoding UTF8
$result | ConvertTo-Json -Depth 8


