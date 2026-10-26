param(
  [ValidateRange(0, 255)]
  [int] $DiskNumber = 3,
  [string] $OutputRoot = "$env:A733_WORKDIR\work\log",
  [switch] $Apply
)

$ErrorActionPreference = "Stop"
$expectedFriendlyName = "Generic STORAGE DEVICE"
$expectedSerial = "000000001532"
$expectedSize = 127999672320L
$expectedSectorSize = 512
$expectedEspOffset = 33554432L
$expectedEspSize = 4294967296L
$espGptType = "{c12a7328-f81f-11d2-ba4b-00a0c93ec93b}"
$expectedDiskGuid = "9857561c-effb-48dd-af22-bf64c8ab7ba9"
$expectedEspGuid = "3fe48bd3-c0cd-4247-8498-ebbc97458dcd"
$expectedHeaderSha256 = "080FB1634AF4C3B17680254FF89202A02BA3056DA2B4D3808CEDD0BEFBA2D3D5"
$headerLength = 131072
$temporaryDriveLetter = $null
$runDir = $null
$backupReady = $false
$applyStarted = $false
$applyCommitted = $false
$transactionPath = $null
$operationFailure = $null
$runMutex = $null
$runMutexHeld = $false
$changedPaths = [System.Collections.Generic.List[string]]::new()
$stores = @()

function Assert-Equal($Observed, $Expected, [string] $Label) {
  if ($Observed -ne $Expected) {
    throw "$Label mismatch: observed='$Observed' expected='$Expected'"
  }
}

function Invoke-BcdEdit([string[]] $Arguments) {
  $output = & bcdedit.exe @Arguments 2>&1
  $exitCode = $LASTEXITCODE
  $text = $output -join "`r`n"
  if ($exitCode -ne 0) {
    throw "bcdedit failed ($exitCode): bcdedit $($Arguments -join ' ')`r`n$text"
  }
  return $text
}

function Get-BcdElement([string] $Text, [string] $Name) {
  $pattern = "(?mi)^\s*" + [regex]::Escape($Name) + "\s+(.+?)\s*$"
  $match = [regex]::Match($Text, $pattern)
  if (-not $match.Success) { return $null }
  return $match.Groups[1].Value.Trim()
}

function Get-WithoutDebugLine([string] $Text) {
  return ((($Text -split "`r?`n") |
    Where-Object { $_ -notmatch '(?i)^\s*debug\s+' } |
    ForEach-Object { $_.TrimEnd() }) -join "`n").Trim()
}

function Normalize-BcdAllForDefaultDebug([string] $Text, [string] $ExpectedDebug) {
  $currentIdentifier = $null
  $debugCount = 0
  $normalized = foreach ($line in ($Text -split "`r?`n")) {
    $identifier = [regex]::Match($line, '(?i)^\s*identifier\s+(.+?)\s*$')
    if ($identifier.Success) {
      $currentIdentifier = $identifier.Groups[1].Value.Trim()
    }
    $debug = [regex]::Match($line, '(?i)^\s*debug\s+(.+?)\s*$')
    if ($debug.Success) {
      if ($currentIdentifier -ne "{default}") {
        throw "unexpected debug element outside {default}: identifier=$currentIdentifier line=$line"
      }
      Assert-Equal $debug.Groups[1].Value.Trim() $ExpectedDebug "{default} debug in /enum all"
      $debugCount++
      "debug <normalized>"
    } else {
      $line.TrimEnd()
    }
  }
  Assert-Equal $debugCount 1 "number of debug elements in /enum all"
  return (($normalized -join "`n").Trim())
}

function Assert-ExpectedProfile([string] $BootMgr, [string] $Loader, [string] $DebugValue) {
  Assert-Equal (Get-BcdElement $BootMgr "bootdebug") "No" "bootmgr bootdebug"
  Assert-Equal (Get-BcdElement $Loader "bootdebug") "No" "loader bootdebug"
  Assert-Equal (Get-BcdElement $Loader "debug") $DebugValue "loader debug"
  Assert-Equal (Get-BcdElement $Loader "halbreakpoint") "No" "loader halbreakpoint"
  Assert-Equal (Get-BcdElement $Loader "ems") "No" "loader ems"
  Assert-Equal (Get-BcdElement $Loader "winpe") "Yes" "loader winpe"
}

function Flush-FileDurable([string] $Path) {
  $stream = [System.IO.File]::Open(
    $Path,
    [System.IO.FileMode]::Open,
    [System.IO.FileAccess]::ReadWrite,
    [System.IO.FileShare]::Read
  )
  try {
    $stream.Flush($true)
  } finally {
    $stream.Dispose()
  }
}

function Get-PhysicalHeaderSha256([int] $Number, [int] $Length) {
  $path = "\\.\PhysicalDrive$Number"
  $stream = [System.IO.File]::Open(
    $path,
    [System.IO.FileMode]::Open,
    [System.IO.FileAccess]::Read,
    [System.IO.FileShare]::ReadWrite
  )
  try {
    $buffer = New-Object byte[] $Length
    $offset = 0
    while ($offset -lt $Length) {
      $count = $stream.Read($buffer, $offset, $Length - $offset)
      if ($count -le 0) { throw "short physical-header read at $offset of $Length" }
      $offset += $count
    }
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
      return ([BitConverter]::ToString($sha.ComputeHash($buffer))).Replace("-", "")
    } finally {
      $sha.Dispose()
    }
  } finally {
    $stream.Dispose()
  }
}

function Flush-Volume([char] $DriveLetter) {
  if (-not ("A733VolumeFlush" -as [type])) {
    Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public static class A733VolumeFlush {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern IntPtr CreateFileW(
        string name,
        uint access,
        uint share,
        IntPtr security,
        uint creation,
        uint flags,
        IntPtr templateFile);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool FlushFileBuffers(IntPtr handle);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool CloseHandle(IntPtr handle);

    public static void Flush(char driveLetter) {
        const uint GENERIC_READ = 0x80000000;
        const uint GENERIC_WRITE = 0x40000000;
        const uint FILE_SHARE_READ = 0x00000001;
        const uint FILE_SHARE_WRITE = 0x00000002;
        const uint OPEN_EXISTING = 3;
        IntPtr invalid = new IntPtr(-1);
        IntPtr handle = CreateFileW(
            @"\\.\" + driveLetter + ":",
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            IntPtr.Zero,
            OPEN_EXISTING,
            0,
            IntPtr.Zero);
        if (handle == invalid) {
            throw new Win32Exception(Marshal.GetLastWin32Error(), "CreateFile(volume) failed");
        }
        try {
            if (!FlushFileBuffers(handle)) {
                throw new Win32Exception(Marshal.GetLastWin32Error(), "FlushFileBuffers(volume) failed");
            }
        } finally {
            CloseHandle(handle);
        }
    }
}
"@
  }
  [A733VolumeFlush]::Flush($DriveLetter)
}

function Write-JsonDurable([string] $Path, [object] $Value) {
  $temporaryPath = $Path + ".tmp"
  $Value | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $temporaryPath -Encoding ASCII
  Flush-FileDurable $temporaryPath
  Move-Item -LiteralPath $temporaryPath -Destination $Path -Force
  Flush-FileDurable $Path
}

function Write-Transaction([string] $Status, [object[]] $StoreResults, [string] $ErrorText = $null) {
  if (-not $transactionPath) { return }
  $transaction = [ordered]@{
    version = 1
    status = $Status
    timestamp = (Get-Date).ToString("o")
    disk_number = $DiskNumber
    disk_guid = $expectedDiskGuid
    esp_guid = $expectedEspGuid
    physical_header_sha256 = $expectedHeaderSha256
    stores = $StoreResults
    error = $ErrorText
  }
  Write-JsonDurable $transactionPath $transaction
}

function Restore-Stores([object[]] $CurrentStores, [object[]] $BackupRecords, [char] $DriveLetter) {
  $errors = [System.Collections.Generic.List[string]]::new()
  foreach ($store in $CurrentStores) {
    try {
      $record = @($BackupRecords | Where-Object { $_.name -eq $store.Name })
      if ($record.Count -ne 1) { throw "expected one backup record, found $($record.Count)" }
      $record = $record[0]
      if (-not (Test-Path -LiteralPath $record.backup -PathType Leaf)) {
        throw "backup is missing: $($record.backup)"
      }
      $backupHash = (Get-FileHash -LiteralPath $record.backup -Algorithm SHA256).Hash
      Assert-Equal $backupHash ([string]$record.before_sha256) "$($store.Name) rollback backup hash"
      Copy-Item -LiteralPath $record.backup -Destination $store.Path -Force
      Flush-FileDurable $store.Path
      $restoredHash = (Get-FileHash -LiteralPath $store.Path -Algorithm SHA256).Hash
      Assert-Equal $restoredHash $backupHash "$($store.Name) restored hash"
    } catch {
      $errors.Add("$($store.Name): $($_.Exception.Message)")
    }
  }
  try {
    Flush-Volume $DriveLetter
  } catch {
    $errors.Add("volume flush: $($_.Exception.Message)")
  }
  return @($errors)
}

function Recover-IncompleteTransaction([object[]] $CurrentStores, [char] $DriveLetter) {
  $transactionDirs = @(Get-ChildItem -LiteralPath $OutputRoot -Directory -Filter "bcd_no_kd_profile_*" -ErrorAction Stop)
  $pending = [System.Collections.Generic.List[object]]::new()
  foreach ($directory in $transactionDirs) {
    $regularPath = Join-Path $directory.FullName "transaction.json"
    $temporaryPath = $regularPath + ".tmp"
    $candidatePath = if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
      $temporaryPath
    } elseif (Test-Path -LiteralPath $regularPath -PathType Leaf) {
      $regularPath
    } else {
      $null
    }
    if (-not $candidatePath) { continue }
    try {
      $manifest = Get-Content -LiteralPath $candidatePath -Raw | ConvertFrom-Json -ErrorAction Stop
    } catch {
      if ($candidatePath -eq $temporaryPath -and (Test-Path -LiteralPath $regularPath -PathType Leaf)) {
        try {
          $manifest = Get-Content -LiteralPath $regularPath -Raw | ConvertFrom-Json -ErrorAction Stop
        } catch {
          throw "cannot parse transaction manifests $temporaryPath or $regularPath`: $($_.Exception.Message)"
        }
      } else {
        throw "cannot parse transaction manifest $candidatePath`: $($_.Exception.Message)"
      }
    }
    $hasTemporary = $candidatePath -eq $temporaryPath
    if ($hasTemporary -or $manifest.status -in @("applying", "rollback-failed")) {
      $pending.Add([pscustomobject]@{
        CanonicalPath = $regularPath
        SourcePath = $candidatePath
        Manifest = $manifest
      })
    }
  }
  if ($pending.Count -gt 1) {
    throw "multiple incomplete BCD transactions require manual audit: $($pending.SourcePath -join ', ')"
  }
  if ($pending.Count -eq 0) { return }

  $entry = $pending[0]
  Assert-Equal (([string]$entry.Manifest.disk_guid).ToLowerInvariant()) $expectedDiskGuid "recovery disk GUID"
  Assert-Equal (([string]$entry.Manifest.esp_guid).ToLowerInvariant()) $expectedEspGuid "recovery ESP GUID"
  Assert-Equal (([string]$entry.Manifest.physical_header_sha256).ToUpperInvariant()) $expectedHeaderSha256 "recovery physical header"
  if (-not $Apply) {
    throw "incomplete BCD transaction requires -Apply recovery first: $($entry.SourcePath)"
  }

  $restoreErrors = @(Restore-Stores $CurrentStores @($entry.Manifest.stores) $DriveLetter)
  $entry.Manifest | Add-Member -NotePropertyName timestamp -NotePropertyValue (Get-Date).ToString("o") -Force
  if ($restoreErrors.Count) {
    $entry.Manifest | Add-Member -NotePropertyName status -NotePropertyValue "rollback-failed" -Force
    $entry.Manifest | Add-Member -NotePropertyName error -NotePropertyValue ($restoreErrors -join " | ") -Force
    Write-JsonDurable $entry.CanonicalPath $entry.Manifest
    throw "incomplete transaction recovery failed: $($restoreErrors -join ' | ')"
  }
  $entry.Manifest | Add-Member -NotePropertyName status -NotePropertyValue "recovered-before-new-transaction" -Force
  $entry.Manifest | Add-Member -NotePropertyName error -NotePropertyValue $null -Force
  Write-JsonDurable $entry.CanonicalPath $entry.Manifest
}

function Write-RunSummary([string] $Status, [object[]] $StoreResults, [string] $ErrorText = $null) {
  if (-not $runDir) { return }
  [ordered]@{
    status = $Status
    timestamp = (Get-Date).ToString("o")
    apply = [bool]$Apply
    persistence = "persistent-on-SD-until-restored"
    disk_number = $DiskNumber
    expected_friendly_name = $expectedFriendlyName
    expected_serial = $expectedSerial
    expected_size = $expectedSize
    expected_disk_guid = $expectedDiskGuid
    expected_esp_guid = $expectedEspGuid
    expected_header_sha256 = $expectedHeaderSha256
    esp_offset = $expectedEspOffset
    esp_size = $expectedEspSize
    changed_paths = @($changedPaths)
    stores = $StoreResults
    error = $ErrorText
    note = "Only {default} debug Yes -> No is permitted. bootdebug, halbreakpoint, EMS, and all other enumerated BCD lines must remain unchanged."
  } | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $runDir "summary.json") -Encoding ASCII
}

try {
  $principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
  if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Administrator rights are required for the enrolled-disk and volume-flush checks."
  }

  $mutexName = "Global\A733NoKdBcdProfile_PhysicalDrive$DiskNumber"
  $createdNew = $false
  $runMutex = [System.Threading.Mutex]::new($false, $mutexName, [ref]$createdNew)
  try {
    $runMutexHeld = $runMutex.WaitOne(0)
  } catch [System.Threading.AbandonedMutexException] {
    $runMutexHeld = $true
  }
  if (-not $runMutexHeld) {
    throw "another A733 BCD transaction already holds $mutexName"
  }

  $disk = Get-Disk -Number $DiskNumber -ErrorAction Stop
  Assert-Equal $disk.FriendlyName $expectedFriendlyName "disk friendly name"
  Assert-Equal ([string]$disk.SerialNumber).Trim() $expectedSerial "disk serial"
  Assert-Equal ([string]$disk.BusType) "USB" "disk bus type"
  Assert-Equal ([int64]$disk.Size) $expectedSize "disk size"
  Assert-Equal ([string]$disk.PartitionStyle) "GPT" "disk partition style"
  Assert-Equal ([bool]$disk.IsBoot) $false "disk IsBoot"
  Assert-Equal ([bool]$disk.IsSystem) $false "disk IsSystem"
  Assert-Equal ([int]$disk.LogicalSectorSize) $expectedSectorSize "disk logical sector size"
  Assert-Equal (([Guid]$disk.Guid).ToString("D").ToLowerInvariant()) $expectedDiskGuid "disk GPT GUID"
  if ([string]$disk.OperationalStatus -notmatch "Online") {
    throw "disk is not online: $($disk.OperationalStatus)"
  }
  Assert-Equal (Get-PhysicalHeaderSha256 $DiskNumber $headerLength) $expectedHeaderSha256 "physical header SHA256"

  $OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
  $outputRootMatch = [regex]::Match($OutputRoot, '^(?<drive>[A-Za-z]):\\')
  if (-not $outputRootMatch.Success) {
    throw "OutputRoot must be on a local drive: $OutputRoot"
  }
  New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null
  $outputPartition = Get-Partition -DriveLetter ([char]$outputRootMatch.Groups["drive"].Value) -ErrorAction Stop
  if (@($outputPartition | Where-Object DiskNumber -eq $DiskNumber).Count) {
    throw "OutputRoot must not reside on the target SD: $OutputRoot"
  }

  $esp = Get-Partition -DiskNumber $DiskNumber -PartitionNumber 1 -ErrorAction Stop
  Assert-Equal ([int64]$esp.Offset) $expectedEspOffset "ESP offset"
  Assert-Equal ([int64]$esp.Size) $expectedEspSize "ESP size"
  Assert-Equal ([string]$esp.GptType).ToLowerInvariant() $espGptType "ESP GPT type"
  Assert-Equal (([Guid]$esp.Guid).ToString("D").ToLowerInvariant()) $expectedEspGuid "ESP unique GUID"

  $volume = $esp | Get-Volume -ErrorAction Stop
  Assert-Equal ([string]$volume.FileSystem) "FAT32" "ESP filesystem"
  if (-not $volume.DriveLetter) {
    $used = @(Get-Volume | Where-Object DriveLetter | ForEach-Object { [string]$_.DriveLetter })
    $temporaryDriveLetter = @("R", "S", "T", "U", "V", "W", "X", "Y", "Z") |
      Where-Object { $_ -notin $used } |
      Select-Object -First 1
    if (-not $temporaryDriveLetter) { throw "no temporary drive letter is available" }
    Add-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber 1 -AccessPath "$temporaryDriveLetter`:\" -ErrorAction Stop
    $driveLetter = [char]$temporaryDriveLetter
  } else {
    $driveLetter = [char]$volume.DriveLetter
  }

  $root = "$driveLetter`:"
  $stores = @(
    [pscustomobject]@{ Name = "EFI_Boot_BCD"; Path = Join-Path $root "EFI\Boot\BCD" },
    [pscustomobject]@{ Name = "EFI_Microsoft_Boot_BCD"; Path = Join-Path $root "EFI\Microsoft\Boot\BCD" }
  )
  Recover-IncompleteTransaction $stores $driveLetter
  foreach ($store in $stores) {
    if (-not (Test-Path -LiteralPath $store.Path -PathType Leaf)) {
      throw "required BCD store is missing: $($store.Path)"
    }
  }

  $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
  $runDir = Join-Path $OutputRoot "bcd_no_kd_profile_$stamp"
  New-Item -ItemType Directory -Force -Path $runDir | Out-Null

  $results = @()
  foreach ($store in $stores) {
    $backupPath = Join-Path $runDir ($store.Name + ".before.BCD")
    $inspectPath = Join-Path $runDir ($store.Name + ".before.inspect.BCD")
    $sourceHash = (Get-FileHash -LiteralPath $store.Path -Algorithm SHA256).Hash
    Copy-Item -LiteralPath $store.Path -Destination $backupPath -Force
    Flush-FileDurable $backupPath
    $beforeHash = (Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash
    Assert-Equal $beforeHash $sourceHash "$($store.Name) source/backup hash"
    Copy-Item -LiteralPath $backupPath -Destination $inspectPath -Force
    $bootMgrBefore = Invoke-BcdEdit @("/store", $inspectPath, "/enum", "{bootmgr}")
    $loaderBefore = Invoke-BcdEdit @("/store", $inspectPath, "/enum", "{default}")
    $allBefore = Invoke-BcdEdit @("/store", $inspectPath, "/enum", "all")
    Assert-ExpectedProfile $bootMgrBefore $loaderBefore "Yes"
    Normalize-BcdAllForDefaultDebug $allBefore "Yes" | Out-Null
    Assert-Equal (Get-FileHash -LiteralPath $backupPath -Algorithm SHA256).Hash $beforeHash "$($store.Name) immutable backup hash"
    Set-Content -LiteralPath (Join-Path $runDir ($store.Name + ".before.bootmgr.txt")) -Value $bootMgrBefore -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $runDir ($store.Name + ".before.default.txt")) -Value $loaderBefore -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $runDir ($store.Name + ".before.all.txt")) -Value $allBefore -Encoding ASCII
    $results += [pscustomobject]@{
      name = $store.Name
      path = $store.Path
      backup = $backupPath
      inspection_copy = $inspectPath
      before_sha256 = $beforeHash
      after_sha256 = $null
      verified_debug = "Yes"
    }
  }

  if (-not $Apply) {
    Write-RunSummary "dry-run-pass" $results
    [pscustomobject]@{ Status = "dry-run-pass"; RunDir = $runDir; Stores = $results } | ConvertTo-Json -Depth 5
    return
  }

  $transactionPath = Join-Path $runDir "transaction.json"
  Write-Transaction "prepared" $results
  $backupReady = $true
  foreach ($result in $results) {
    Assert-Equal (Get-FileHash -LiteralPath $result.path -Algorithm SHA256).Hash $result.before_sha256 "$($result.name) unchanged before apply"
  }
  Assert-Equal (Get-PhysicalHeaderSha256 $DiskNumber $headerLength) $expectedHeaderSha256 "physical header immediately before apply"
  Write-Transaction "applying" $results
  $applyStarted = $true
  foreach ($store in $stores) {
    Invoke-BcdEdit @("/store", $store.Path, "/set", "{default}", "debug", "No") | Out-Null
    $changedPaths.Add($store.Path)
  }

  for ($index = 0; $index -lt $stores.Count; $index++) {
    $store = $stores[$index]
    $bootMgrAfter = Invoke-BcdEdit @("/store", $store.Path, "/enum", "{bootmgr}")
    $loaderAfter = Invoke-BcdEdit @("/store", $store.Path, "/enum", "{default}")
    $allAfter = Invoke-BcdEdit @("/store", $store.Path, "/enum", "all")
    Assert-ExpectedProfile $bootMgrAfter $loaderAfter "No"
    $bootMgrBefore = Get-Content -LiteralPath (Join-Path $runDir ($store.Name + ".before.bootmgr.txt")) -Raw
    $loaderBefore = Get-Content -LiteralPath (Join-Path $runDir ($store.Name + ".before.default.txt")) -Raw
    $allBefore = Get-Content -LiteralPath (Join-Path $runDir ($store.Name + ".before.all.txt")) -Raw
    Assert-Equal ($bootMgrAfter.Trim()) ($bootMgrBefore.Trim()) "$($store.Name) bootmgr semantic delta"
    Assert-Equal (Get-WithoutDebugLine $loaderAfter) (Get-WithoutDebugLine $loaderBefore) "$($store.Name) loader non-debug delta"
    Assert-Equal (Normalize-BcdAllForDefaultDebug $allAfter "No") (Normalize-BcdAllForDefaultDebug $allBefore "Yes") "$($store.Name) /enum all non-debug delta"
    Set-Content -LiteralPath (Join-Path $runDir ($store.Name + ".after.bootmgr.txt")) -Value $bootMgrAfter -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $runDir ($store.Name + ".after.default.txt")) -Value $loaderAfter -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $runDir ($store.Name + ".after.all.txt")) -Value $allAfter -Encoding ASCII
    $afterCopy = Join-Path $runDir ($store.Name + ".after.BCD")
    Copy-Item -LiteralPath $store.Path -Destination $afterCopy -Force
    Flush-FileDurable $afterCopy
    $results[$index].after_sha256 = (Get-FileHash -LiteralPath $afterCopy -Algorithm SHA256).Hash
    $results[$index].verified_debug = "No"
  }

  Flush-Volume $driveLetter
  Write-Transaction "committed" $results
  $applyCommitted = $true
  $applyStarted = $false
  Write-RunSummary "applied-and-verified" $results
  [pscustomobject]@{ Status = "applied-and-verified"; RunDir = $runDir; Stores = $results } | ConvertTo-Json -Depth 5
} catch {
  $failure = $_.Exception
  $operationFailure = $failure
  if ($Apply -and $backupReady -and $applyStarted) {
    $restoreErrors = @(Restore-Stores $stores $results $driveLetter)
    if ($restoreErrors.Count) {
      $rollbackText = $restoreErrors -join " | "
      try { Write-Transaction "rollback-failed" $results ($failure.Message + "; rollback=" + $rollbackText) } catch {}
      try { Write-RunSummary "rollback-failed" $results ($failure.Message + "; rollback=" + $rollbackText) } catch {}
      throw "BCD update failed and rollback also failed: $($failure.Message); rollback=$rollbackText"
    }
    $applyStarted = $false
    try { Write-Transaction "rolled-back-after-failure" $results $failure.Message } catch {}
    try { Write-RunSummary "rolled-back-after-failure" $results $failure.Message } catch {}
  } elseif ($applyCommitted -and $runDir) {
    try { Write-RunSummary "applied-with-postcommit-host-error" $results $failure.Message } catch {}
  } elseif ($runDir) {
    try { Write-RunSummary "failed-before-change" $results $failure.Message } catch {}
  }
  throw $failure
} finally {
  $cleanupFailure = $null
  if ($temporaryDriveLetter) {
    try {
      Remove-PartitionAccessPath -DiskNumber $DiskNumber -PartitionNumber 1 -AccessPath "$temporaryDriveLetter`:\" -ErrorAction Stop
    } catch {
      $cleanupFailure = "failed to remove temporary drive letter $temporaryDriveLetter`: $($_.Exception.Message)"
    }
  }
  if ($runMutexHeld -and $runMutex) {
    try {
      $runMutex.ReleaseMutex()
      $runMutexHeld = $false
    } catch {
      $mutexFailure = "failed to release transaction mutex: $($_.Exception.Message)"
      $cleanupFailure = if ($cleanupFailure) { $cleanupFailure + "; " + $mutexFailure } else { $mutexFailure }
    }
  }
  if ($runMutex) {
    try {
      $runMutex.Dispose()
    } catch {
      $mutexDisposeFailure = "failed to dispose transaction mutex: $($_.Exception.Message)"
      $cleanupFailure = if ($cleanupFailure) { $cleanupFailure + "; " + $mutexDisposeFailure } else { $mutexDisposeFailure }
    }
  }
  if ($cleanupFailure) {
    if ($operationFailure) {
      throw "$($operationFailure.Message); cleanup=$cleanupFailure"
    }
    throw $cleanupFailure
  }
}
