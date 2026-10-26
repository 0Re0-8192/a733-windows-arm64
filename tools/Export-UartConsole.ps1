param(
    [Parameter(Mandatory=$true)][string]$InputPath,
    [int]$CodePage = 932
)
$ErrorActionPreference = 'Stop'
$inputFile = (Resolve-Path -LiteralPath $InputPath).Path
$bytes = [System.IO.File]::ReadAllBytes($inputFile)
$ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
$start = $ascii.IndexOf("`r`n[A733-CONSOLE] relay-start ", [System.StringComparison]::Ordinal)
$pattern = '\r\n\[A733-CONSOLE\] relay-end exit=(\d+) captured=(\d+) sent=(\d+) dropped=(\d+) sink_error=(\d+) ram_bytes=(\d+) ram_error=(\d+)(?: timeout_retries=(\d+))?\r\n'
$end = [regex]::Match($ascii, $pattern)
if ($start -lt 0 -or !$end.Success -or $end.Index -le $start) {
    throw 'No complete UART console transcript; retain the original capture for diagnosis.'
}
$textPath = [System.IO.Path]::ChangeExtension($inputFile, '.console.txt')
$jsonPath = [System.IO.Path]::ChangeExtension($inputFile, '.console.json')
if ((Test-Path -LiteralPath $textPath) -or (Test-Path -LiteralPath $jsonPath)) {
    throw 'Console export already exists; refusing to overwrite it.'
}
$values = @($end.Groups | Select-Object -Skip 1 -First 7 | ForEach-Object { [uint64]$_.Value })
$encoding = [System.Text.Encoding]::GetEncoding($CodePage)
$console = $encoding.GetString($bytes, $start, $end.Index + $end.Length - $start)
$result = [ordered]@{
    Source = $inputFile
    SourceSha256 = (Get-FileHash -LiteralPath $inputFile -Algorithm SHA256).Hash
    ConsoleText = $textPath
    CodePage = $CodePage
    ChildExit = $values[0]
    CapturedBytes = $values[1]
    SentBytes = $values[2]
    DroppedBytes = $values[3]
    SinkError = $values[4]
    RamBytesBeforeSummary = $values[5]
    RamError = $values[6]
    TimeoutRetriesBeforeSummary = if ($end.Groups[8].Success) { [uint64]$end.Groups[8].Value } else { 0 }
    WireBytesBeforeSummary = $end.Index - $start
    WireCountMatches = ($end.Index - $start) -eq $values[2]
    NoReportedLoss = ($values[3] -eq 0 -and $values[4] -eq 0 -and $values[6] -eq 0)
}
$result.TransportVerified = $result.WireCountMatches -and $result.NoReportedLoss
$utf8 = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($textPath, $console, $utf8)
[System.IO.File]::WriteAllText($jsonPath, ($result | ConvertTo-Json), $utf8)
[pscustomobject]$result
