param(
    [ValidateSet('ARM64', 'x64')][string]$Architecture = 'ARM64',
    [string]$KitVersion = '10.0.26100.0'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot "native_process_compat.ps1")
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$toolset = Get-ChildItem 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC' -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$arch = $Architecture.ToLowerInvariant()
$bin = Join-Path $toolset.FullName "bin\Hostx64\$arch"
$cl = Join-Path $bin 'cl.exe'
$link = Join-Path $bin 'link.exe'
$dumpbin = Join-Path $bin 'dumpbin.exe'
$out = Join-Path $PSScriptRoot "out\uart-$arch"
New-Item -ItemType Directory -Path $out -Force | Out-Null
$common = @('/nologo', '/c', '/W4', '/WX', '/O2', "/I$($toolset.FullName)\include",
    "/I$kit\Include\$KitVersion\shared", "/I$kit\Include\$KitVersion\ucrt")
$define = if ($Architecture -eq 'ARM64') { '/D_ARM64_' } else { '/D_AMD64_' }
$kernelObject = Join-Path $out 'a733_uart_log.obj'
$driver = Join-Path $out 'a733uartlog.sys'
Invoke-A733NativeCompat $cl @common /GS- /Zl /kernel $define /wd5103 "/I$kit\Include\$KitVersion\km" `
    "/Fo$kernelObject" (Join-Path $PSScriptRoot 'a733_uart_log.c')
if ($LASTEXITCODE -ne 0) { throw 'UART driver compile failed' }
$kernelRuntime = if ($Architecture -eq 'ARM64') {
    @("/libpath:$kit\Lib\$KitVersion\um\arm64", 'arm64rt.lib', 'libcntpr.lib')
} else { @() }
Invoke-A733NativeCompat $link /nologo /driver /subsystem:native /entry:GsDriverEntry /nodefaultlib `
    "/machine:$Architecture" "/out:$driver" $kernelObject `
    "/libpath:$kit\Lib\$KitVersion\km\$arch" ntoskrnl.lib hal.lib wdmsec.lib BufferOverflowFastFailK.lib @kernelRuntime
if ($LASTEXITCODE -ne 0) { throw 'UART driver link failed' }
$userObject = Join-Path $out 'winpe_uart_log.obj'
$exe = Join-Path $out 'A733WinPeUartLog.exe'
Invoke-A733NativeCompat $cl @common /MT /DUNICODE /D_UNICODE "/I$kit\Include\$KitVersion\um" `
    "/Fo$userObject" (Join-Path $PSScriptRoot 'winpe_uart_log.c')
if ($LASTEXITCODE -ne 0) { throw 'UART relay compile failed' }
Invoke-A733NativeCompat $link /nologo /subsystem:console "/machine:$Architecture" "/out:$exe" $userObject `
    "/libpath:$($toolset.FullName)\lib\$arch" `
    "/libpath:$kit\Lib\$KitVersion\ucrt\$arch" `
    "/libpath:$kit\Lib\$KitVersion\um\$arch" kernel32.lib advapi32.lib
if ($LASTEXITCODE -ne 0) { throw 'UART relay link failed' }
$imports = Invoke-A733NativeCapture $dumpbin /imports $exe | Out-String
if ($imports -match '(?i)ucrtbase\.dll|vcruntime.*\.dll|msvcp.*\.dll') {
    throw 'A dynamic CRT dependency entered the UART relay'
}
$kernelImports = Invoke-A733NativeCapture $dumpbin /imports $driver | Out-String
if ($kernelImports -match '(?i)KERNEL32\.dll|ucrtbase\.dll') {
    throw 'User-mode imports entered the UART driver'
}
if ($Architecture -eq 'ARM64') {
    $package = Join-Path $PSScriptRoot 'out\arm64-test-signed'
    New-Item -ItemType Directory -Path $package -Force | Out-Null
    Copy-Item -LiteralPath $driver, $exe -Destination $package -Force
}
[pscustomobject]@{ Architecture=$Architecture; Driver=$driver; Executable=$exe }
