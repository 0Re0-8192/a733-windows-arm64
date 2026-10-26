param([ValidateSet('basic', 'burst', 'drain')][string]$Mode = 'basic')
if ($Mode -eq 'drain') {
    [Console]::Out.Write(('0123456789ABCDEF' * 14336))
    [Console]::Out.WriteLine('DRAIN_END')
    exit 0
}
if ($Mode -eq 'burst') {
    [Console]::Out.Write(('0123456789ABCDEF' * 131072))
    [Console]::Out.WriteLine('BURST_END')
    exit 0
}
[Console]::Out.WriteLine('STDOUT_SENTINEL')
[Console]::Error.WriteLine('STDERR_SENTINEL')
[Console]::Out.Write('FINAL_WITHOUT_NEWLINE')
exit 37
