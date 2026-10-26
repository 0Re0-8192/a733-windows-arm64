param(
    [ValidateSet("status", "force-ddr-fel", "normal")]
    [string]$Mode = "status",

    [string]$Tool = "$env:A733_WORKDIR\board\a733_ums_sd_tool.ps1",
    [string]$ForceBoot0 = "$env:A733_WORKDIR\board\artifacts\boot0-fastdram-force.bin",
    [string]$NormalBoot0 = "$env:A733_WORKDIR\board\artifacts\boot0-fastdram-normal.bin",
    [string]$OutDir = "$env:A733_WORKDIR\work\backup\a733_ums_sd\boot0_mode_status",

    [switch]$Apply,
    [switch]$AcknowledgeOneWayNormalBoot
)

$ErrorActionPreference = "Stop"

$Boot0Offset = [UInt64]0x00020000
$Boot0Size = [UInt64]0x0003C000
$ForceBoot0Sha256 = "FD0671AC6A7B1B6EA81F41808857E3C00CAF74E35618A3D03E99E7AA7485938D"  # H957 fast-DRAM (tpr13=0x6067, para2=0x20001001); original 74ACA01D…
$NormalBoot0Sha256 = "54BF01A89784395974E0EB8871236B1EE14F045021844FA3536991B619BEBE37"  # H957 fast-DRAM; original DEC1B5B5…

if (-not (Test-Path -LiteralPath $Tool)) {
    throw "Required tool not found: $Tool"
}

function Get-Boot0ModeName {
    param([string]$Sha256)
    switch ($Sha256) {
        $ForceBoot0Sha256 { "force-ddr-fel" }
        "74ACA01D856038E41EF1C5A9E27B2C82202142F704D89E9A266D6BEE8EB50813" { "force-ddr-fel(original-dram)" }
        $NormalBoot0Sha256 { "normal" }
        "DEC1B5B5B3B23342833F99CCDD4892F0F5C101B1C21035543CE9734935B2E030" { "normal(original-dram)" }
        default { "unknown" }
    }
}

function Read-CurrentBoot0 {
    if (-not (Test-Path -LiteralPath $OutDir)) {
        New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    }
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $out = Join-Path $OutDir "boot0.current.$stamp.bin"
    $read = & $Tool -Action read-region -Offset $Boot0Offset -Size $Boot0Size -OutFile $out
    $sha = (Get-FileHash -Algorithm SHA256 -LiteralPath $out).Hash
    [pscustomobject]@{
        Mode = (Get-Boot0ModeName -Sha256 $sha)
        Sha256 = $sha
        Readback = $out
        ReadRegion = $read
    }
}

$current = Read-CurrentBoot0

if ($Mode -eq "status") {
    [pscustomobject]@{
        RequestedMode = $Mode
        CurrentMode = $current.Mode
        CurrentSha256 = $current.Sha256
        CurrentReadback = $current.Readback
        ForceBoot0 = $ForceBoot0
        ForceBoot0Sha256 = if (Test-Path -LiteralPath $ForceBoot0) { (Get-FileHash -Algorithm SHA256 -LiteralPath $ForceBoot0).Hash } else { "MISSING" }
        NormalBoot0 = $NormalBoot0
        NormalBoot0Sha256 = if (Test-Path -LiteralPath $NormalBoot0) { (Get-FileHash -Algorithm SHA256 -LiteralPath $NormalBoot0).Hash } else { "MISSING" }
        Note = "normal mode permits normal boot, but disables the relay->DDR-FEL->auto-UMS loop until force-ddr-fel BOOT0 is written again."
    }
    return
}

$target = if ($Mode -eq "force-ddr-fel") { $ForceBoot0 } else { $NormalBoot0 }
$expected = if ($Mode -eq "force-ddr-fel") { $ForceBoot0Sha256 } else { $NormalBoot0Sha256 }

if (-not (Test-Path -LiteralPath $target)) {
    throw "Target BOOT0 image not found: $target"
}

$targetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $target).Hash
if ($targetHash -ne $expected) {
    throw "Target BOOT0 hash mismatch for mode $Mode. expected=$expected actual=$targetHash path=$target"
}

if ($Mode -eq "normal" -and $Apply -and -not $AcknowledgeOneWayNormalBoot) {
    throw "Refusing to apply normal BOOT0 without -AcknowledgeOneWayNormalBoot. Normal BOOT0 allows Windows boot, but the relay->DDR-FEL->auto-UMS loop will not be available after the next reset unless force BOOT0 is written again first."
}

$write = & $Tool -Action write-boot -Boot0Image $target -Apply:$Apply

[pscustomobject]@{
    RequestedMode = $Mode
    Apply = [bool]$Apply
    PreviousMode = $current.Mode
    PreviousSha256 = $current.Sha256
    TargetBoot0 = $target
    TargetSha256 = $targetHash
    WriteResult = $write
    Warning = if ($Mode -eq "normal") {
        "After applying normal BOOT0 and resetting, relay->DDR-FEL->auto-UMS will no longer be automatic. Apply force-ddr-fel again before leaving UMS if you want to keep the no-swap workflow."
    } else {
        "force-ddr-fel mode keeps relay->DDR-FEL->auto-UMS workflow available, but prevents normal SD boot."
    }
}
