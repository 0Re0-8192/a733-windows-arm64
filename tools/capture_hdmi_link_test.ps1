param(
  [string] $OutputDir = "$env:A733_WORKDIR\work\log",
  [string] $Tag = "hdmi_cldo2_link_test",
  [string] $UartPort = "COM8",
  [string] $RelayPort = "COM11",
  [string] $VideoDevice = "Cam Link 4K",
  [ValidateRange(1, 86400)]
  [int] $PreTerminalTimeoutSec = 60,
  [ValidateRange(1, 86400)]
  [int] $HdmiSilenceSec = 6,
  [ValidateRange(0, 86400)]
  [int] $PostCompleteCaptureSec = 8,
  [ValidateRange(10, 600)]
  [int] $CameraStallTimeoutSec = 60,
  [switch] $ReacquireCameraAfterTerminal
)

$ErrorActionPreference = "Stop"

New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$prefix = Join-Path $OutputDir ("{0}_{1}" -f $Tag, $stamp)
$binPath = "$prefix.bin"
$textPath = "$prefix.txt"
$eventPath = "$prefix.events.txt"
$statePath = "$prefix.state.json"
$videoPath = "$prefix.mkv"
$reacquireVideoPath = "$prefix.postterminal.mkv"
$uartTimingPath = "$prefix.uart-timing.csv"

$start = Get-Date
$powerOn = $null
$lastData = $null
$lastDataElapsedSec = $null
$hdmiStart = $null
$relayOn = $false
$totalBytes = 0
$searchTail = ""
$searchTailMaxChars = 65536
$terminalHandled = $false
$preCompleteBytes = $null
$postCompleteRawOffset = $null
$postCompleteStart = $null
$postCompleteEnd = $null
$postCompleteFirstData = $null
$postCompleteLastData = $null
$postCompleteBytes = 0
$postCompleteDrainBytes = 0
$stopReason = "not-started"
$terminalMatch = $null
$relayOffFailure = $null
$relayOffCommandSent = $false
$hostFailure = $null
$cleanupFailures = [System.Collections.Generic.List[string]]::new()
$serial = $null
$raw = $null
$camera = $null
$activeVideoPath = $videoPath
$cameraLastLength = 0L
$cameraLastProgress = $null
$cameraOutputObserved = $false
$nextCameraHealthCheck = $null
$preTerminalStopwatch = $null
$postCompleteStopwatch = $null
$postCompleteAwaitingCamera = $false
$postCompleteCameraGrowthCount = 0

function Write-SharedTextFile {
  param(
    [string] $Path,
    [string] $Text,
    [System.Text.Encoding] $Encoding,
    [ValidateSet("Append", "Create")]
    [string] $Mode = "Append",
    [switch] $NoNewline,
    [int] $Attempts = 20
  )

  for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
    $stream = $null
    $writer = $null
    try {
      $fileMode = if ($Mode -eq "Create") { [System.IO.FileMode]::Create } else { [System.IO.FileMode]::Append }
      $stream = [System.IO.File]::Open(
        $Path,
        $fileMode,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::ReadWrite
      )
      $writer = [System.IO.StreamWriter]::new($stream, $Encoding)
      if ($NoNewline) {
        $writer.Write($Text)
      } else {
        $writer.WriteLine($Text)
      }
      $writer.Flush()
      $stream.Flush()
      return
    } catch [System.IO.IOException] {
      if ($attempt -ge $Attempts) { throw }
      Start-Sleep -Milliseconds 50
    } finally {
      if ($writer) { $writer.Dispose() }
      elseif ($stream) { $stream.Dispose() }
    }
  }
}

function Add-Event([string] $Message) {
  Write-SharedTextFile -Path $eventPath -Text ((Get-Date).ToString("o") + " " + $Message) -Encoding ([System.Text.Encoding]::UTF8)
}

function Start-Camera([string] $Path, [string] $Label) {
  $ffmpeg = (Get-Command ffmpeg.exe -ErrorAction Stop).Source
  $cameraInfo = [System.Diagnostics.ProcessStartInfo]::new()
  $cameraInfo.FileName = $ffmpeg
  $cameraInfo.Arguments = (
    '-y -hide_banner -loglevel error -use_wallclock_as_timestamps 1 -f dshow -rtbufsize 256M ' +
    '-i "video=' + $VideoDevice + '" -an -c:v libx264 -preset ultrafast ' +
    '-crf 18 -threads 1 -pix_fmt yuv420p "' + $Path + '"'
  )
  $cameraInfo.UseShellExecute = $false
  $cameraInfo.CreateNoWindow = $true
  $cameraInfo.RedirectStandardInput = $true
  $process = [System.Diagnostics.Process]::new()
  $process.StartInfo = $cameraInfo
  if (-not $process.Start()) { throw "ffmpeg failed to start" }
  try { $process.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch {}
  Add-Event ("camera_{0}_start pid={1} device={2} output={3}" -f $Label, $process.Id, $VideoDevice, $Path)
  return $process
}

function Stop-Camera([System.Diagnostics.Process] $Process, [string] $Label) {
  if (-not $Process) { return }
  try {
    $alreadyExited = $Process.HasExited
    if (-not $Process.HasExited) {
      try {
        $Process.StandardInput.WriteLine("q")
        $Process.StandardInput.Flush()
        if (-not $Process.WaitForExit(10000)) {
          $Process.Kill()
          if (-not $Process.WaitForExit(5000)) { throw "ffmpeg did not exit after Kill" }
        }
      } catch {
        try { $Process.Kill() } catch {}
        try { $Process.WaitForExit(5000) | Out-Null } catch {}
        throw
      }
    }
    $exitCode = if ($Process.HasExited) { $Process.ExitCode } else { "unknown" }
    Add-Event ("camera_{0}_stop exit={1}" -f $Label, $exitCode)
    if ($exitCode -ne 0) {
      throw "ffmpeg exited with code $exitCode (already_exited=$alreadyExited)"
    }
  } finally {
    $Process.Dispose()
  }
}

function Send-Relay([byte[]] $Bytes, [string] $Label) {
  $relay = [System.IO.Ports.SerialPort]::new(
    $RelayPort,
    9600,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
  )
  $relay.WriteTimeout = 1000
  $relay.DtrEnable = $false
  $relay.RtsEnable = $false
  try {
    $relay.Open()
    $relay.Write($Bytes, 0, $Bytes.Length)
    $relay.BaseStream.Flush()
    Add-Event ("relay_{0}_sent {1} {2}" -f $Label, $RelayPort, (($Bytes | ForEach-Object { "{0:X2}" -f $_ }) -join "-"))
  } finally {
    if ($relay.IsOpen) { $relay.Close() }
    $relay.Dispose()
  }
}

function Send-RelayWithRetry([byte[]] $Bytes, [string] $Label, [int] $Attempts = 3) {
  $lastFailure = $null
  for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
    try {
      Send-Relay $Bytes ("{0}-attempt-{1}" -f $Label, $attempt)
      return
    } catch {
      $lastFailure = $_.Exception
      try { Add-Event ("relay_{0}_attempt_failed attempt={1} error={2}" -f $Label, $attempt, $lastFailure.Message) } catch {}
      if ($attempt -lt $Attempts) { Start-Sleep -Milliseconds 250 }
    }
  }
  throw $lastFailure
}

function Save-State([string] $Phase) {
  [ordered]@{
    phase = $Phase
    start = $start.ToString("o")
    now = (Get-Date).ToString("o")
    power_on = if ($powerOn) { $powerOn.ToString("o") } else { $null }
    last_data = if ($lastData) { $lastData.ToString("o") } else { $null }
    hdmi_start = if ($hdmiStart) { $hdmiStart.ToString("o") } else { $null }
    stop_reason = $stopReason
    terminal_match = $terminalMatch
    relay_off_failure = $relayOffFailure
    relay_off_command_sent = $relayOffCommandSent
    total_bytes = $totalBytes
    search_tail_max_chars = $searchTailMaxChars
    pre_complete_bytes = $preCompleteBytes
    post_complete_raw_offset = $postCompleteRawOffset
    post_complete_start = if ($postCompleteStart) { $postCompleteStart.ToString("o") } else { $null }
    post_complete_end = if ($postCompleteEnd) { $postCompleteEnd.ToString("o") } else { $null }
    post_complete_first_data = if ($postCompleteFirstData) { $postCompleteFirstData.ToString("o") } else { $null }
    post_complete_last_data = if ($postCompleteLastData) { $postCompleteLastData.ToString("o") } else { $null }
    post_complete_bytes = $postCompleteBytes
    post_complete_final_drain_bytes = $postCompleteDrainBytes
    post_complete_camera_growth_count = $postCompleteCameraGrowthCount
    pre_terminal_timeout_sec = $PreTerminalTimeoutSec
    pre_terminal_elapsed_sec = if ($preTerminalStopwatch) { $preTerminalStopwatch.Elapsed.TotalSeconds } else { $null }
    post_complete_elapsed_sec = if ($postCompleteStopwatch) { $postCompleteStopwatch.Elapsed.TotalSeconds } else { $null }
    hdmi_silence_sec = $HdmiSilenceSec
    uart = $UartPort
    relay = $RelayPort
    video_device = $VideoDevice
    bin = $binPath
    text = $textPath
    events = $eventPath
    video = $videoPath
    reacquire_video = if ($ReacquireCameraAfterTerminal) { $reacquireVideoPath } else { $null }
    active_video = $activeVideoPath
    uart_timing = $uartTimingPath
    post_complete_capture_sec = $PostCompleteCaptureSec
    camera_stall_timeout_sec = $CameraStallTimeoutSec
    reacquire_camera_after_terminal = [bool]$ReacquireCameraAfterTerminal
    camera_last_length = $cameraLastLength
    camera_last_progress = if ($cameraLastProgress) { $cameraLastProgress.ToString("o") } else { $null }
    camera_output_observed = $cameraOutputObserved
    power_off_verification = "command-transmitted-no-relay-ack"
    notes = "Owned-device no-KD HDMI bring-up observation. Relay power only; no PC/register/memory write. Target MMIO/PMIC writes are firmware-audited and volatile. Relay has no state acknowledgement; final OFF is an idempotent command with bounded retries, not physical feedback."
  } | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $statePath -Encoding UTF8
}

try {
  $camera = Start-Camera $activeVideoPath "prepower"
  Start-Sleep -Seconds 2
  if ($camera.HasExited) { throw "ffmpeg exited before target power-on: $($camera.ExitCode)" }
  $cameraLastProgress = Get-Date
  $cameraItem = Get-Item -LiteralPath $activeVideoPath -ErrorAction SilentlyContinue
  if ($cameraItem -and $cameraItem.Length -gt 0) {
    $cameraOutputObserved = $true
    $cameraLastLength = $cameraItem.Length
    Add-Event ("camera_output_observed bytes={0}" -f $cameraLastLength)
  } else {
    Add-Event ("camera_output_pending timeout_sec={0}" -f $CameraStallTimeoutSec)
  }
  $nextCameraHealthCheck = $cameraLastProgress.AddSeconds(2)

  $serial = [System.IO.Ports.SerialPort]::new(
    $UartPort,
    115200,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
  )
  $serial.ReadTimeout = 100
  $serial.ReadBufferSize = 1048576
  $serial.DtrEnable = $false
  $serial.RtsEnable = $false
  $serial.Open()
  Add-Event "capture_start $UartPort 115200 8N1"

  $raw = [System.IO.File]::Open(
    $binPath,
    [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write,
    [System.IO.FileShare]::Read
  )
  Save-State "uart-open"

  $relayOn = $true
  Send-RelayWithRetry ([byte[]](0xA0, 0x01, 0x01, 0xA2)) "off"
  $relayOn = $false
  $relayOffCommandSent = $true
  Start-Sleep -Seconds 3
  $relayOffCommandSent = $false
  $relayOn = $true
  Send-Relay ([byte[]](0xA0, 0x01, 0x00, 0xA1)) "on"
  $powerOn = Get-Date
  $preTerminalStopwatch = [System.Diagnostics.Stopwatch]::StartNew()
  $stopReason = "pre-terminal-timeout"
  Save-State "powered-on"

  $buffer = New-Object byte[] 4096
  $nextState = (Get-Date).AddSeconds(2)
  $terminalPattern = '(?m)^\[A733-HDMI-TEST\] (complete[^\r\n]*|physical-gop result=fail[^\r\n]*|DE-(?:stop|CCU-restore) result=uncertain[^\r\n]*|fail-closed end[^\r\n]*|invalid-build[^\r\n]*|abort[^\r\n]*restoration=(?:verified|uncertain)[^\r\n]*)\r?\n'
  $terminalRegex = [System.Text.RegularExpressions.Regex]::new($terminalPattern)

  while ($true) {
    if ($camera.HasExited) {
      throw "ffmpeg exited during capture: $($camera.ExitCode)"
    }
    $nowForCamera = Get-Date
    if ($nextCameraHealthCheck -and
        ($nowForCamera -ge $nextCameraHealthCheck)) {
        $cameraItem = Get-Item -LiteralPath $activeVideoPath -ErrorAction SilentlyContinue
        if ($cameraItem) {
          if ($cameraItem.Length -gt $cameraLastLength) {
            if (-not $cameraOutputObserved) {
              $cameraOutputObserved = $true
              Add-Event ("camera_output_observed bytes={0} path={1}" -f $cameraItem.Length, $activeVideoPath)
            }
            $cameraLastLength = $cameraItem.Length
            $cameraLastProgress = $nowForCamera
            if ($postCompleteAwaitingCamera) {
              $postCompleteCameraGrowthCount++
              Add-Event ("post_complete_camera_progress path={0} bytes={1} growth_count={2}" -f $activeVideoPath, $cameraLastLength, $postCompleteCameraGrowthCount)
              if ($postCompleteCameraGrowthCount -ge 2) {
                $postCompleteAwaitingCamera = $false
                $postCompleteStart = $nowForCamera
                $postCompleteStopwatch.Start()
                Add-Event ("post_complete_camera_ready path={0} bytes={1}" -f $activeVideoPath, $cameraLastLength)
                Add-Event ("post_complete_observation_start seconds={0} uart_buffer={1} raw_offset={2}" -f $PostCompleteCaptureSec, $serial.ReadBufferSize, $postCompleteRawOffset)
              }
            }
          } elseif ($cameraItem.Length -lt $cameraLastLength) {
            throw ("ffmpeg output unexpectedly shrank from {0} to {1} bytes: {2}" -f $cameraLastLength, $cameraItem.Length, $activeVideoPath)
          } elseif (-not $cameraOutputObserved -and $cameraItem.Length -gt 0) {
            $cameraOutputObserved = $true
            Add-Event ("camera_output_observed bytes={0} path={1}" -f $cameraItem.Length, $activeVideoPath)
          } elseif ($cameraLastProgress -and
                  (($nowForCamera - $cameraLastProgress).TotalSeconds -ge
                   $CameraStallTimeoutSec)) {
          throw ("ffmpeg output stalled for {0:N1} seconds at {1} bytes" -f
                 (($nowForCamera - $cameraLastProgress).TotalSeconds),
                 $cameraLastLength)
        }
      } elseif ($cameraLastProgress -and
                (($nowForCamera - $cameraLastProgress).TotalSeconds -ge
                 $CameraStallTimeoutSec)) {
        throw ("ffmpeg output did not appear within {0:N1} seconds" -f
               (($nowForCamera - $cameraLastProgress).TotalSeconds))
      }
      $nextCameraHealthCheck = $nowForCamera.AddSeconds(2)
    }

    if ($terminalHandled) {
      if (-not $postCompleteAwaitingCamera -and
          $postCompleteStopwatch.Elapsed.TotalSeconds -ge $PostCompleteCaptureSec) {
        $postCompleteStopwatch.Stop()
        $postCompleteEnd = Get-Date
        $drainRemaining = [Math]::Min($serial.BytesToRead, $serial.ReadBufferSize)
        while ($drainRemaining -gt 0) {
          $drainCount = $serial.Read($buffer, 0, [Math]::Min($buffer.Length, $drainRemaining))
          if ($drainCount -le 0) { break }
          $drainRemaining -= $drainCount
          $raw.Write($buffer, 0, $drainCount)
          $totalBytes += $drainCount
          $postCompleteBytes += $drainCount
          $postCompleteDrainBytes += $drainCount
          $lastData = Get-Date
          $postCompleteLastData = $lastData
          $drainChunk = [System.Text.Encoding]::ASCII.GetString($buffer, 0, $drainCount)
          Write-SharedTextFile -Path $textPath -Text $drainChunk -Encoding ([System.Text.Encoding]::ASCII) -NoNewline
          Write-SharedTextFile -Path $uartTimingPath -Text ('{0},{1:F6},{2},{3},final-drain' -f $lastData.ToString('o'), $postCompleteStopwatch.Elapsed.TotalSeconds, ($totalBytes - $drainCount), $drainCount) -Encoding ([System.Text.Encoding]::ASCII)
        }
        $raw.Flush()
        $stopReason = "post-complete-duration"
        Add-Event ("post_complete_observation_end seconds={0:N3} bytes={1} final_drain_bytes={2}" -f $postCompleteStopwatch.Elapsed.TotalSeconds, $postCompleteBytes, $postCompleteDrainBytes)
        break
      }
    } elseif ($preTerminalStopwatch.Elapsed.TotalSeconds -ge $PreTerminalTimeoutSec) {
      $preTerminalStopwatch.Stop()
      break
    }

    $remainingMs = if ($terminalHandled) {
      if ($postCompleteAwaitingCamera) { 100.0 } else { ($PostCompleteCaptureSec * 1000.0) - $postCompleteStopwatch.Elapsed.TotalMilliseconds }
    } else {
      ($PreTerminalTimeoutSec * 1000.0) - $preTerminalStopwatch.Elapsed.TotalMilliseconds
    }
    $serial.ReadTimeout = [Math]::Max(1, [Math]::Min(100, [int][Math]::Ceiling($remainingMs)))

    try {
      # SerialPort.Read honors ReadTimeout. BaseStream.Read can block past that
      # deadline on Windows, preventing camera health checks and final relay-off.
      $count = $serial.Read($buffer, 0, $buffer.Length)
      if ($count -gt 0) {
        $now = Get-Date
        $raw.Write($buffer, 0, $count)
        $raw.Flush()
        $totalBytes += $count
        $lastData = $now
        if (-not $terminalHandled) { $lastDataElapsedSec = $preTerminalStopwatch.Elapsed.TotalSeconds }
        $chunk = [System.Text.Encoding]::ASCII.GetString($buffer, 0, $count)
        Write-SharedTextFile -Path $textPath -Text $chunk -Encoding ([System.Text.Encoding]::ASCII) -NoNewline
        $searchTail += $chunk
        if ($searchTail.Length -gt $searchTailMaxChars) {
          $searchTail = $searchTail.Substring($searchTail.Length - $searchTailMaxChars)
        }

        if ($terminalHandled) {
          if (-not $postCompleteFirstData) { $postCompleteFirstData = $now }
          $postCompleteLastData = $now
          $postCompleteBytes += $count
          Write-SharedTextFile -Path $uartTimingPath -Text ('{0},{1:F6},{2},{3},data' -f $now.ToString('o'), $postCompleteStopwatch.Elapsed.TotalSeconds, ($totalBytes - $count), $count) -Encoding ([System.Text.Encoding]::ASCII)
        }

        if (-not $hdmiStart -and $searchTail -match '\[A733-HDMI-TEST\]') {
          $hdmiStart = $now
          Add-Event "hdmi_test_uart_started"
        }

        $terminal = if (-not $terminalHandled) { $terminalRegex.Match($searchTail) } else { $null }
        if ($terminal -and $terminal.Success) {
          $terminalMatch = $terminal.Groups[1].Value
          $stopReason = "terminal-uart"
          Add-Event ("terminal_uart " + ($terminalMatch -replace '[\r\n]+', ' '))
          if ($terminalMatch -match '^complete') {
            $terminalHandled = $true
            $preTerminalStopwatch.Stop()
            $preCompleteBytes = $totalBytes - $searchTail.Length + $terminal.Index + $terminal.Length
            $postCompleteRawOffset = $preCompleteBytes
            $postCompleteBytes = $totalBytes - $preCompleteBytes
            if ($postCompleteBytes -gt 0) {
              $postCompleteFirstData = $now
              $postCompleteLastData = $now
            }
            $postCompleteStopwatch = [System.Diagnostics.Stopwatch]::new()
            Write-SharedTextFile -Path $uartTimingPath -Text 'timestamp,post_complete_elapsed_sec,raw_offset,count,kind' -Encoding ([System.Text.Encoding]::ASCII) -Mode Create
            if ($postCompleteBytes -gt 0) {
              Write-SharedTextFile -Path $uartTimingPath -Text ('{0},{1:F6},{2},{3},marker-chunk-tail' -f $now.ToString('o'), 0.0, $postCompleteRawOffset, $postCompleteBytes) -Encoding ([System.Text.Encoding]::ASCII)
            }
            if ($ReacquireCameraAfterTerminal) {
              try {
                Stop-Camera $camera "preterminal"
              } catch {
                Add-Event ("camera_preterminal_stop_nonfatal error={0}" -f ($_.Exception.Message -replace '[\r\n]+', ' '))
                $cleanupFailures.Add("camera-preterminal: " + $_.Exception.Message)
              }
              $preterminalCameraItem = Get-Item -LiteralPath $videoPath -ErrorAction SilentlyContinue
              if (-not $preterminalCameraItem -or $preterminalCameraItem.Length -le 0) {
                $cleanupFailures.Add("camera-preterminal-output: missing or empty video")
                Add-Event ("camera_preterminal_output_invalid path={0}" -f $videoPath)
              } else {
                Add-Event ("camera_preterminal_output_verified bytes={0} path={1}" -f $preterminalCameraItem.Length, $videoPath)
              }
              $activeVideoPath = $reacquireVideoPath
              $camera = Start-Camera $activeVideoPath "postterminal"
              Start-Sleep -Seconds 2
              if ($camera.HasExited) { throw "ffmpeg exited after terminal reacquire: $($camera.ExitCode)" }
              $cameraLastProgress = Get-Date
              $cameraLastLength = 0L
              $postCompleteAwaitingCamera = $true
              $postCompleteCameraGrowthCount = 0
              $cameraItem = Get-Item -LiteralPath $activeVideoPath -ErrorAction SilentlyContinue
              if ($cameraItem -and $cameraItem.Length -gt 0) {
                $cameraOutputObserved = $true
                $cameraLastLength = $cameraItem.Length
                $postCompleteCameraGrowthCount = 1
                Add-Event ("camera_reacquired_after_terminal bytes={0} path={1}" -f $cameraLastLength, $activeVideoPath)
                Add-Event ("post_complete_camera_progress path={0} bytes={1} growth_count={2}" -f $activeVideoPath, $cameraLastLength, $postCompleteCameraGrowthCount)
              } else {
                $cameraOutputObserved = $false
                $cameraLastLength = 0L
                Add-Event ("camera_reacquired_after_terminal pending timeout_sec={0} path={1}" -f $CameraStallTimeoutSec, $activeVideoPath)
              }
              $nextCameraHealthCheck = $cameraLastProgress.AddSeconds(2)
            } else {
              $postCompleteStart = Get-Date
              $postCompleteStopwatch.Start()
            }
            if ($ReacquireCameraAfterTerminal) {
              Add-Event ("camera_reacquired_after_complete output={0}" -f $activeVideoPath)
            } else {
              Add-Event ("camera_continuous_after_complete output={0}" -f $activeVideoPath)
              Add-Event ("post_complete_observation_start seconds={0} uart_buffer={1} raw_offset={2}" -f $PostCompleteCaptureSec, $serial.ReadBufferSize, $postCompleteRawOffset)
            }
            if ($PostCompleteCaptureSec -le 0) {
              $postCompleteStopwatch.Stop()
              $postCompleteEnd = Get-Date
              $stopReason = "post-complete-duration"
              break
            }
            continue
          }
          $preTerminalStopwatch.Stop()
          break
        }
      }
    } catch [System.TimeoutException] {
    }

    if (-not $terminalHandled -and $hdmiStart -and $lastDataElapsedSec -ne $null -and (($preTerminalStopwatch.Elapsed.TotalSeconds - $lastDataElapsedSec) -ge $HdmiSilenceSec)) {
      $stopReason = "hdmi-uart-silence"
      Add-Event ("hdmi_uart_silence seconds={0:N3}" -f ($preTerminalStopwatch.Elapsed.TotalSeconds - $lastDataElapsedSec))
      $preTerminalStopwatch.Stop()
      break
    }

    if ((Get-Date) -ge $nextState) {
      Save-State "capturing"
      $nextState = (Get-Date).AddSeconds(2)
    }
  }
} catch {
  $stopReason = "host-error: " + $_.Exception.Message
  $hostFailure = $_.Exception
  try { Add-Event $stopReason } catch {}
} finally {
  if ($preTerminalStopwatch -and $preTerminalStopwatch.IsRunning) { $preTerminalStopwatch.Stop() }
  if ($postCompleteStopwatch -and $postCompleteStopwatch.IsRunning) { $postCompleteStopwatch.Stop() }
  if ($relayOn) {
    try {
      Send-RelayWithRetry ([byte[]](0xA0, 0x01, 0x01, 0xA2)) "off-final"
      $relayOn = $false
      $relayOffCommandSent = $true
    } catch {
      $relayOffFailure = $_.Exception.Message
      try { Add-Event ("relay_off_failure " + $relayOffFailure) } catch {}
    }
  }

  try {
    if ($serial -and $serial.IsOpen) { $serial.Close() }
    if ($serial) { $serial.Dispose() }
  } catch {
    $cleanupFailures.Add("serial: " + $_.Exception.Message)
  }
  try {
    if ($raw) { $raw.Dispose() }
  } catch {
    $cleanupFailures.Add("raw-file: " + $_.Exception.Message)
  }

  try {
    Stop-Camera $camera "final"
  } catch {
    $cleanupFailures.Add("camera: " + $_.Exception.Message)
  }
  try {
    $cameraValidationTargets = if ($ReacquireCameraAfterTerminal) {
      @(
        [pscustomobject]@{ Label = "preterminal"; Path = $videoPath },
        [pscustomobject]@{ Label = "postterminal"; Path = $reacquireVideoPath }
      )
    } else {
      @([pscustomobject]@{ Label = "continuous"; Path = $videoPath })
    }
    foreach ($cameraValidationTarget in $cameraValidationTargets) {
      $finalCameraItem = Get-Item -LiteralPath $cameraValidationTarget.Path -ErrorAction SilentlyContinue
      if (-not $finalCameraItem -or $finalCameraItem.Length -le 0) {
        $cleanupFailures.Add(("camera-output-{0}: missing or empty final video" -f $cameraValidationTarget.Label))
        continue
      }
      if ($cameraValidationTarget.Path -eq $activeVideoPath) {
        $cameraOutputObserved = $true
        $cameraLastLength = $finalCameraItem.Length
      }
      Add-Event ("camera_output_final label={0} bytes={1} path={2}" -f $cameraValidationTarget.Label, $finalCameraItem.Length, $cameraValidationTarget.Path)
    }
  } catch {
    $cleanupFailures.Add("camera-output: " + $_.Exception.Message)
  }

  try {
    Save-State $(if ($relayOffFailure) { "unsafe-relay-off-failure" } elseif ($cleanupFailures.Count) { "cleanup-failure" } else { "done" })
  } catch {
    $cleanupFailures.Add("state: " + $_.Exception.Message)
  }
  try { Add-Event ("capture_stop reason={0} total_bytes={1}" -f $stopReason, $totalBytes) } catch {}
  if ($relayOffFailure) {
    throw "Final relay power-off failed after retries: $relayOffFailure; cleanup=$($cleanupFailures -join ' | ')"
  }
  if ($hostFailure) {
    if ($cleanupFailures.Count) {
      throw [System.Exception]::new(($hostFailure.Message + "; cleanup=" + ($cleanupFailures -join " | ")), $hostFailure)
    }
    throw $hostFailure
  }
  if ($cleanupFailures.Count) {
    throw "Capture cleanup failed: $($cleanupFailures -join ' | ')"
  }
}

Get-Content -LiteralPath $statePath -Raw
