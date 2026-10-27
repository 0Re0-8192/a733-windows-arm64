param(
    [Parameter(Mandatory)]
    [string]$Tag,

    [Parameter(Mandatory)]
    [string]$BaseWim,

    [Parameter(Mandatory)]
    [string]$ExpectedBaseSha256,

    [int]$ImageIndex = 1,

    [switch]$NoDiskPublicationOnly,
    [switch]$SkipOfflineRegistryUpdate,
    [switch]$Recompress,
    [switch]$ActualNtfsImage,
    [switch]$ActualNtfsWrite,
    [switch]$ActualFatWrite,
    [switch]$ActualFatFullMediaRead,
    [switch]$ActualRawGpt,

    [ValidateRange(1, 64)]
    [int]$MaxLogicalProcessors = 1,

    [ValidateRange(1, 100)]
    [int]$CpuPercent = 2,

    [ValidateSet("Idle", "BelowNormal")]
    [string]$PriorityClass = "Idle"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
if ($ActualNtfsWrite -and !$ActualNtfsImage) { throw 'ActualNtfsWrite requires ActualNtfsImage.' }
if ($ActualFatWrite -and !$ActualNtfsImage) { throw 'ActualFatWrite requires ActualNtfsImage.' }
if ($ActualNtfsWrite -and $ActualFatWrite) { throw 'Choose only one disposable filesystem write fixture.' }
if ($ActualFatFullMediaRead -and ($ActualNtfsImage -or $ActualNtfsWrite -or $ActualFatWrite)) {
    throw 'ActualFatFullMediaRead requires the read-only actual FAT32 profile.'
}
if ($ActualRawGpt -and ($ActualNtfsImage -or $ActualNtfsWrite -or $ActualFatWrite -or $ActualFatFullMediaRead)) {
    throw 'ActualRawGpt requires the read-only raw-media profile.'
}

$self = Get-Process -Id $PID
$oldPriority = $self.PriorityClass
$oldAffinity = $self.ProcessorAffinity
$limits = $null

try {
    $self.PriorityClass = $PriorityClass
    # Keep the one-time interop compilation off the first logical CPU as well.
    for ($bit = 62; $bit -ge 0; $bit--) {
        $mask = [int64]1 -shl $bit
        if (($oldAffinity.ToInt64() -band $mask) -ne 0) {
            $self.ProcessorAffinity = [IntPtr]$mask
            break
        }
    }
    if (-not ('A733.BuildResourceScope' -as [type])) {
        Add-Type -Path (Join-Path $PSScriptRoot 'BuildResourceScope.cs')
    }
    $self.ProcessorAffinity = $oldAffinity
    $limits = [A733.BuildResourceScope]::new($CpuPercent, $MaxLogicalProcessors, $PriorityClass)

    $buildArgs = @{
        Tag = $Tag
        BaseWim = $BaseWim
        ExpectedBaseSha256 = $ExpectedBaseSha256
        ImageIndex = $ImageIndex
        SkipRecompression = !$Recompress
        ActualNtfsImage = [bool]$ActualNtfsImage
        ActualNtfsWrite = [bool]$ActualNtfsWrite
        ActualFatWrite = [bool]$ActualFatWrite
        ActualFatFullMediaRead = [bool]$ActualFatFullMediaRead
        ActualRawGpt = [bool]$ActualRawGpt
    }
    if ($NoDiskPublicationOnly) {
        $buildArgs.NoDiskPublicationOnly = $true
    }
    if ($SkipOfflineRegistryUpdate) {
        $buildArgs.SkipOfflineRegistryUpdate = $true
    }

    $result = & (Join-Path $PSScriptRoot "Build-SmhcDiagnosticWim.ps1") @buildArgs
    $result | Add-Member -NotePropertyName ResourceLimits -NotePropertyValue $limits.GetStatus() -PassThru
} finally {
    if ($limits) {
        # A live child keeps the job capped; do not override its limits on interruption.
        $limits.Dispose()
    }
    try {
        $self = Get-Process -Id $PID
        $self.ProcessorAffinity = $oldAffinity
        $self.PriorityClass = $oldPriority
    } catch {
        throw "Could not restore build host process scheduling: $($_.Exception.Message)"
    }
}
