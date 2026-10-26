$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$source = [IO.File]::ReadAllText((Join-Path $root 'A733SmhcInstall.cmd')).Replace("`r`n", "`n")
$run = Join-Path $root ('out\batch-label-tests-' + (Get-Date -Format 'yyyyMMdd_HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$results = @()
foreach ($kind in @('lf', 'crlf')) {
    foreach ($padding in @(0, 64, 512, 2048)) {
        # Exit before the real script body. The selected subroutine returns
        # immediately with TRACE_STARTED=0, so no hardware/helper is invoked.
        $header = "@echo off`nset A733_TRACE_STARTED=0`ncall :stop_trace`nexit /b %errorlevel%`n"
        $fixtureText = $header + 'rem ' + ('x' * $padding) + "`n" + $source
        if ($kind -eq 'crlf') { $fixtureText = $fixtureText.Replace("`n", "`r`n") }
        $fixture = Join-Path $run ("label_${kind}_${padding}.cmd")
        [IO.File]::WriteAllText($fixture, $fixtureText, [Text.UTF8Encoding]::new($false))
        $ErrorActionPreference = 'Continue'
        $output = & $env:ComSpec /d /c $fixture 2>&1
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $results += [pscustomobject]@{LineEndings=$kind; Padding=$padding; ExitCode=$code}
        if ($kind -eq 'crlf' -and $code -ne 0) { throw "CRLF label lookup failed: $output" }
    }
}
$results | ConvertTo-Json -Compress
