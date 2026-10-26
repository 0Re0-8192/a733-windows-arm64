param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$root=(Resolve-Path "$PSScriptRoot/..").Path
if(!$OutputDirectory) { $OutputDirectory="$root/../../analysis/ntfs-mapping-guard-$(Get-Date -Format yyyyMMdd_HHmmss_fff)" }
if(Test-Path -LiteralPath $OutputDirectory) { throw 'New output required.' }
$null=New-Item -ItemType Directory -Path $OutputDirectory
$out=(Resolve-Path -LiteralPath $OutputDirectory).Path
$source=Get-Content "$root/a733_smhc.c" -Raw
$hash=(Get-FileHash "$root/a733_smhc.c").Hash
& "$PSScriptRoot/Test-ActualNtfsDriver.ps1" | Out-File "$out/baseline.txt"
$matches=[regex]::Matches($source,'(?s)Status = SmhcResolveHardwareArgumentForRequest\(Extension, Request,\s*(Argument|HardwareArgument|NextArgument|CurrentArgument), &HardwareArgument\);\s*if \(!NT_SUCCESS\(Status\)\) \{[^{}]*return Status;\s*\}')
if($matches.Count -ne 6 -or [regex]::Matches($source,'if \(SmhcMapVirtualSetupArgumentForRequest\(').Count) { throw 'Expected six guarded media mapping callers and no ignored boolean media mapper.' }
$inc=''
for($i=0;$i -lt $matches.Count;$i++) {
    $m=$matches[$i]; $arg=$m.Groups[1].Value
    $rest=$source.Substring($m.Index+$m.Length)
    $write=[regex]::Match($rest,'SmhcWrite\(Extension, SMHC_ARG, HardwareArgument\);')
    if(!$write.Success -or $write.Index -gt 6500 -or !$m.Value.Contains('Request->Status = Status;') -or !$m.Value.Contains('Request->RequiredEvents = 0;')) { throw 'Mapping rejection or following ARG write missing.' }
    $local=if($arg -eq 'HardwareArgument'){''}else{"ULONG $arg=Input;"}
    $inc+="static LONG Guarded$i(EXTENSION *Extension,REQUEST *Request,ULONG Input) { ULONG HardwareArgument=Input; $local LONG Status; $($m.Value) $($write.Value) SmhcWrite(Extension,SMHC_CMD,0xA7330638U); return Status; }`r`n"
}
$writeHelper=[regex]::Match($source,'(?ms)^static NTSTATUS\r?\nSmhcWriteBlockForDiagnostic\(.*?^\}').Value
$deny=[regex]::Match($writeHelper,'(?s)#if A733_ACTUAL_NTFS_IMAGE_PROFILE\s*if \(SmhcActualFatVolumeProfile\(\)\) \{\s*return STATUS_MEDIA_WRITE_PROTECTED;\s*\}\s*#endif')
if(!$deny.Success -or $writeHelper.Substring(0,$deny.Index) -match 'SmhcWrite\(|SmhcResetFifo') { throw 'Diagnostic write deny must precede MMIO.' }
$inc+="static LONG GuardDiagnosticWrite(EXTENSION *Extension,REQUEST *Request,ULONG Input) { (void)Request;`r`n$($deny.Value)`r`nSmhcWrite(Extension,SMHC_ARG,Input); SmhcWrite(Extension,SMHC_CMD,0xA7330638U); return STATUS_SUCCESS; }`r`n"
[IO.File]::WriteAllText("$out/ntfs_mapping_guards.inc",$inc)
$vc='C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207'
$kit='C:/Program Files (x86)/Windows Kits/10'
$oldInclude,$oldLib=$env:INCLUDE,$env:LIB
try {
    $env:INCLUDE="$vc/include;$kit/Include/10.0.26100.0/shared;$kit/Include/10.0.26100.0/um;$kit/Include/10.0.26100.0/ucrt"
    $env:LIB="$vc/lib/x64;$kit/Lib/10.0.26100.0/um/x64;$kit/Lib/10.0.26100.0/ucrt/x64"
    foreach($profile in @(@(0,0),@(1,0),@(1,1))) {
        $name='guard_'+($profile -join '_')
        & "$vc/bin/Hostx64/x64/cl.exe" /nologo /W4 /WX /O2 /MT /TC /std:c11 `
            "/DA733_ACTUAL_FAT_VOLUME_PROFILE=$($profile[0])" "/DA733_ACTUAL_NTFS_IMAGE_PROFILE=$($profile[1])" `
            "/I$out" "/I$root/out/x64-check" "/Fo$out/$name.obj" "/Fe$out/$name.exe" "$PSScriptRoot/ntfs_mapping_guard_test.c" 2>&1|Tee-Object "$out/results.txt" -Append
        if($LASTEXITCODE) { throw 'Guard test compile failed.' }
        & "$out/$name.exe"|Tee-Object "$out/results.txt" -Append
        if($LASTEXITCODE) { throw 'Guard test failed.' }
    }
    if((Get-FileHash "$root/a733_smhc.c").Hash -cne $hash) { throw 'Source changed during guard tests.' }
    Get-FileHash "$root/a733_smhc.c",$PSCommandPath,"$PSScriptRoot/ntfs_mapping_guard_test.c"|ForEach-Object { "SHA256 $($_.Hash) $($_.Path)" }|Tee-Object "$out/results.txt" -Append
    "Log: $out/results.txt"
} finally { $env:INCLUDE,$env:LIB=$oldInclude,$oldLib }
