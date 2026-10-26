param(
    [string]$KitVersion = "10.0.26100.0"
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "native_process_compat.ps1")

$kitRoot = "C:\Program Files (x86)\Windows Kits\10"
$visualStudioRoots = @(
    "C:\Program Files\Microsoft Visual Studio\2022\Community",
    "C:\Program Files (x86)\Microsoft Visual Studio\2022\Preview"
)

$toolset = $null
foreach ($visualStudioRoot in $visualStudioRoots) {
    $toolsRoot = Join-Path $visualStudioRoot "VC\Tools\MSVC"
    if (Test-Path -LiteralPath $toolsRoot) {
        $toolset = Get-ChildItem -LiteralPath $toolsRoot -Directory |
            Sort-Object { [version]$_.Name } -Descending |
            Where-Object {
                Test-Path -LiteralPath (
                    Join-Path $_.FullName "bin\Hostx64\arm64\cl.exe")
            } |
            Select-Object -First 1
        if ($null -ne $toolset) {
            break
        }
    }
}
if ($null -eq $toolset) {
    throw "No Hostx64->ARM64 MSVC compiler is installed."
}

$compilerDirectory = Join-Path $toolset.FullName "bin\Hostx64\arm64"
$compiler = Join-Path $compilerDirectory "cl.exe"
$linker = Join-Path $compilerDirectory "link.exe"
$dumpbin = Join-Path $compilerDirectory "dumpbin.exe"
$msvcInclude = Join-Path $toolset.FullName "include"
$kitSharedInclude = Join-Path $kitRoot "Include\$KitVersion\shared"
$kitUcrtInclude = Join-Path $kitRoot "Include\$KitVersion\ucrt"
$kitUmInclude = Join-Path $kitRoot "Include\$KitVersion\um"
$kitUmLibrary = Join-Path $kitRoot "Lib\$KitVersion\um\arm64"
$outputDirectory = Join-Path $PSScriptRoot "out\arm64-test-signed"

foreach ($requiredPath in @(
    $compiler,
    $linker,
    $dumpbin,
    $msvcInclude,
    $kitSharedInclude,
    $kitUcrtInclude,
    $kitUmInclude,
    $kitUmLibrary,
    $outputDirectory
)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required build path is missing: $requiredPath"
    }
}

$source = Join-Path $PSScriptRoot "winpe_cert_install.c"
$object = Join-Path $outputDirectory "winpe_cert_install.obj"
$executable = Join-Path $outputDirectory "A733WinPeCertInstall.exe"
$pdb = Join-Path $outputDirectory "A733WinPeCertInstall.pdb"

Invoke-A733NativeCompat $compiler /nologo /c /TC /std:c11 /W4 /WX /O2 /Oi /Gy /Gw /GS- /Zl `
    /DUNICODE /D_UNICODE /D_WIN64 /D_ARM64_ /DARM64 `
    "/I$msvcInclude" "/I$kitSharedInclude" "/I$kitUcrtInclude" `
    "/I$kitUmInclude" "/Fo$object" $source
if ($LASTEXITCODE -ne 0) {
    throw "WinPE certificate installer compilation failed."
}

Invoke-A733NativeCompat $linker /nologo /machine:ARM64 /subsystem:console /nodefaultlib `
    /entry:A733CertInstallEntry /incremental:no /opt:ref /opt:icf /debug `
    "/out:$executable" "/pdb:$pdb" "/libpath:$kitUmLibrary" `
    $object kernel32.lib advapi32.lib crypt32.lib
if ($LASTEXITCODE -ne 0) {
    throw "WinPE certificate installer link failed."
}

$headers = (Invoke-A733NativeCapture $dumpbin /headers $executable | Out-String)
if (($headers -notmatch "AA64 machine \(ARM64\)") -or
    ($headers -notmatch "3 subsystem \(Windows CUI\)")) {
    throw "The certificate installer is not an ARM64 console executable."
}

$imports = (Invoke-A733NativeCapture $dumpbin /imports $executable | Out-String)
foreach ($expectedImport in @("KERNEL32.dll", "ADVAPI32.dll", "CRYPT32.dll")) {
    if ($imports -notmatch [regex]::Escape($expectedImport)) {
        throw "Expected import is missing: $expectedImport"
    }
}
if ($imports -match "ucrtbase\.dll|vcruntime|msvcp") {
    throw "A dynamic CRT dependency entered the WinPE helper."
}

[pscustomobject]@{
    Executable = $executable
    Sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash
    Architecture = "ARM64"
    Imports = "KERNEL32.dll, ADVAPI32.dll, CRYPT32.dll"
    HostStoreChanged = $false
    TargetStoreScope = "running WinPE LocalMachine (RAM only)"
}
