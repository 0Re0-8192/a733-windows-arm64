param([string]$OutputDirectory,[string]$KitVersion='10.0.26100.0')
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=(Resolve-Path "$PSScriptRoot/..").Path
$analysis=(Resolve-Path "$root/../../analysis").Path
if(!$OutputDirectory) { $OutputDirectory=Join-Path $analysis ('ntfs-write-transport-tests-'+(Get-Date -Format yyyyMMdd_HHmmss_fff)) }
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(!$out.StartsWith($analysis+'\',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $out)) { throw 'Use new output under analysis.' }
$null=New-Item -ItemType Directory -Path $out
$vc=(Get-ChildItem 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC' -Directory|
    Sort-Object {[version]$_.Name} -Descending|Select-Object -First 1).FullName
$kit='C:/Program Files (x86)/Windows Kits/10'
$sdk="$kit/Include/$KitVersion/km/sdport.h"
$inputs=@("$root/actual_ntfs_write_transport.h","$root/experimental/actual_image_write_bounds.h",
    "$root/a733_smhc.h","$root/a733_smhc.c","$PSScriptRoot/actual_ntfs_write_transport_test.c",$PSCommandPath,$sdk)
$pins=@($inputs|ForEach-Object {Get-FileHash -LiteralPath $_})
$driverSource=Get-Content -LiteralPath "$root/a733_smhc.c" -Raw
$admissionHelper=[regex]::Match($driverSource,'(?sm)static NTSTATUS\s+SmhcQueueRejectedImageWrite\(.*?^}\r?\n#endif')
if(!$admissionHelper.Success) { throw 'Admission rejection helper is missing.' }
if($admissionHelper.Value -match 'SmhcImageWriteCommand|SmhcImageWritePayload|SMHC_FIFO') {
    throw 'Admission rejection helper must not issue a transfer.'
}
if($admissionHelper.Value -notmatch 'SmhcQueueImageWriteFailure\(Extension, Request, Generation, Status, TRUE\)') {
    throw 'Admission rejection helper must publish the deferred failure ticket.'
}
$issueOffset=$driverSource.IndexOf("NTSTATUS`r`nA733SmhcIssueRequest")
if($issueOffset -lt 0) { $issueOffset=$driverSource.IndexOf("NTSTATUS`nA733SmhcIssueRequest") }
$rejectOffset=$driverSource.IndexOf('SmhcQueueRejectedImageWrite(Extension, Request, Status)', $issueOffset)
$directReturnOffset=$driverSource.IndexOf('return Status;', $issueOffset)
if($rejectOffset -lt 0 -or $directReturnOffset -lt 0 -or $rejectOffset -gt $directReturnOffset) {
    throw 'IssueRequest must defer a rejected write before its synchronous return.'
}
$types=[regex]::Match((Get-Content -LiteralPath $sdk -Raw),'(?s)typedef UCHAR SDPORT_COMMAND_INDEX;.*?(?=typedef enum _SDPORT_RESET_TYPE)')
if(!$types.Success) { throw 'SDK request type extraction failed.' }
[IO.File]::WriteAllText("$out/transport_sdport_types.inc",$types.Value)
$header=Get-Content "$root/a733_smhc.h" -Raw
$fields=[regex]::Match($header,'(?s)typedef struct _A733_SMHC_EXTENSION \{\s*#if A733_ACTUAL_IMAGE_WRITE_PROFILE\r?\n(.*?)#endif')
if(!$fields.Success) { throw 'Production private extension field extraction failed.' }
[IO.File]::WriteAllText("$out/transport_write_fields.inc",$fields.Groups[1].Value)
$definitions=[Collections.Generic.List[string]]::new()
$lines=$header -split '\r?\n'
for($i=0;$i -lt $lines.Count;$i++) {
    if($lines[$i] -match '^#define (?:SMHC_|CMD_|RINT_|STATUS_|GCTRL_|A733_SMHC_MAX_BLOCK_COUNT\b)') {
        $definition=$lines[$i]
        while($lines[$i].TrimEnd().EndsWith('\')) { $i++; $definition+="`r`n"+$lines[$i] }
        $definitions.Add($definition)
    }
}
if($definitions.Count -lt 40) { throw 'Register definition extraction failed.' }
[IO.File]::WriteAllText("$out/transport_registers.inc",($definitions -join "`r`n"))
if(!('A733.BuildResourceScope' -as [type])) { Add-Type -Path "$root/../../tools/BuildResourceScope.cs" }
$scope=[A733.BuildResourceScope]::new(25,8,'BelowNormal')
$oldInclude,$oldLib=$env:INCLUDE,$env:LIB
try {
    $limits=$scope.GetStatus()
    $limits|ConvertTo-Json -Depth 5|Set-Content "$out/resource-limits.json"
    if($limits.CpuFlags -ne 5 -or $limits.CpuRate -ne 2500) { throw 'Requested aggregate CPU cap was not applied.' }
    $env:INCLUDE="$vc/include;$kit/Include/$KitVersion/shared;$kit/Include/$KitVersion/um;$kit/Include/$KitVersion/ucrt"
    $env:LIB="$vc/lib/x64;$kit/Lib/$KitVersion/um/x64;$kit/Lib/$KitVersion/ucrt/x64"
    $compileStdout="$out/compile.stdout.txt"
    $compileStderr="$out/compile.stderr.txt"
    $compileArgs=@('/nologo','/W4','/WX','/O2','/MT','/TC','/std:c11',
        "/I$out","/Fo$out/transport.obj","/Fe$out/transport.exe",
        "$PSScriptRoot/actual_ntfs_write_transport_test.c")
    $compile=Start-Process -FilePath "$vc/bin/Hostx64/x64/cl.exe" -ArgumentList $compileArgs -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput $compileStdout -RedirectStandardError $compileStderr
    $compile.WaitForExit()
    @((Get-Content $compileStdout -ErrorAction SilentlyContinue),(Get-Content $compileStderr -ErrorAction SilentlyContinue)) |
        Set-Content "$out/compile.txt"
    if($compile.ExitCode) { throw "Host compile failed: $out/compile.txt" }
    $run=Start-Process -FilePath "$out/transport.exe" -PassThru -WindowStyle Hidden `
        -RedirectStandardOutput "$out/results.txt" -RedirectStandardError "$out/errors.txt"
    if(!$run.WaitForExit(30000)) { $run.Kill(); $run.WaitForExit(); throw 'Transport mock tests exceeded30seconds.' }
    $run.WaitForExit()
    Get-Content "$out/results.txt"
    'ADMISSION_REJECTION_CONTRACT deferred_ticket=yes direct_transfer=no issue_wiring=yes' | Add-Content "$out/results.txt"
    foreach($pin in $pins) {
        "SHA256 $($pin.Hash) $($pin.Path)"|Add-Content "$out/results.txt"
        if((Get-FileHash -LiteralPath $pin.Path).Hash -cne $pin.Hash) { throw "Input changed during tests: $($pin.Path)" }
    }
    "Validation output: $out"
    if($run.ExitCode) { throw "Transport mock tests failed ($($run.ExitCode)): $out/results.txt" }
} finally {
    $env:INCLUDE,$env:LIB=$oldInclude,$oldLib
    for($attempt=0;$attempt -lt 100;$attempt++) {
        try { $scope.Dispose(); break } catch { if($attempt -eq 99) { throw }; Start-Sleep -Milliseconds 100 }
    }
}
