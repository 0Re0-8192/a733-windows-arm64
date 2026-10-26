Set-StrictMode -Version Latest

function ConvertTo-A733NativeArgument {
    param([AllowEmptyString()][string]$Argument)
    if ($Argument.Length -gt 0 -and $Argument -notmatch '[\s\"]') {
        return $Argument
    }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            $slashes++
            continue
        }
        if ($character -eq '"') {
            if ($slashes) { [void]$builder.Append(('\' * ($slashes * 2))) }
            [void]$builder.Append('\"')
            $slashes = 0
            continue
        }
        if ($slashes) {
            [void]$builder.Append(('\' * $slashes))
            $slashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($slashes) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function New-A733NativeArgumentString {
    param([object[]]$ArgumentList)
    $expanded = @(
        foreach ($item in $ArgumentList) {
            if ($item -is [Array]) {
                foreach ($nested in $item) { $nested }
            } else {
                $item
            }
        }
    )
    return (($expanded | ForEach-Object {
        ConvertTo-A733NativeArgument -Argument ([string]$_)
    }) -join ' ')
}


function Invoke-A733NativeCompat {
    param([string]$FilePath)
    $ArgumentList = @($args)
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = New-A733NativeArgumentString -ArgumentList $ArgumentList
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }
    Set-Variable -Name LASTEXITCODE -Value $exitCode -Scope 1
    if ($stdout) { Write-Host -NoNewline $stdout }
    if ($stderr) { Write-Host -NoNewline $stderr }
}

function Invoke-A733NativeCapture {
    param([string]$FilePath)
    $ArgumentList = @($args)
    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $FilePath
    $startInfo.Arguments = New-A733NativeArgumentString -ArgumentList $ArgumentList
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($startInfo)
    try {
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        $exitCode = $process.ExitCode
    } finally {
        $process.Dispose()
    }
    Set-Variable -Name LASTEXITCODE -Value $exitCode -Scope 1
    if ($stdout) { $stdout }
    if ($stderr) { $stderr }
}
