@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "A733_PKG=%SystemRoot%\System32\A733Smhc"
set "A733_LOG=%SystemDrive%\A733OfflineProvision.log"
set "A733_TARGET="

>"%A733_LOG%" echo A733_OFFLINE_PROVISION_BEGIN
call :run "%A733_PKG%\A733WinPeCertInstall.exe"
if errorlevel 1 goto :failed
call :run pnputil.exe /add-driver "%A733_PKG%\a733_smhc.inf" /install
if errorlevel 1 goto :failed
call :run pnputil.exe /scan-devices
if errorlevel 1 goto :failed

>"%SystemDrive%\A733OfflineProvision.diskpart" echo select disk 0
>>"%SystemDrive%\A733OfflineProvision.diskpart" echo list partition
>>"%SystemDrive%\A733OfflineProvision.diskpart" echo select partition 3
>>"%SystemDrive%\A733OfflineProvision.diskpart" echo assign letter=W
>>"%SystemDrive%\A733OfflineProvision.diskpart" echo exit
call :run diskpart.exe /s "%SystemDrive%\A733OfflineProvision.diskpart"
if errorlevel 1 goto :failed

ping.exe -n 6 127.0.0.1 >nul
for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do (
    if exist "%%D:\Windows\System32\Config\SYSTEM" set "A733_TARGET=%%D:"
)
if not defined A733_TARGET (
    echo A733_OFFLINE_PROVISION_FAIL reason=target_volume_not_mounted
    goto :failed
)

echo A733_OFFLINE_TARGET=%A733_TARGET%
>>"%A733_LOG%" echo A733_OFFLINE_TARGET=%A733_TARGET%
call :run dism.exe /Image:%A733_TARGET%\ /Add-Driver /Driver:"%A733_PKG%\a733_smhc.inf" /ForceUnsigned
if errorlevel 1 goto :failed

copy /y "%A733_LOG%" "%A733_TARGET%\A733OfflineProvision.log" >nul
call :set_boot_target "%A733_TARGET%"
if errorlevel 1 goto :failed
echo A733_OFFLINE_PROVISION_PASS target=%A733_TARGET%
ping.exe -n 8 127.0.0.1 >nul
wpeutil.exe reboot
exit /b 0

:failed
set "A733_RC=%ERRORLEVEL%"
if defined A733_TARGET copy /y "%A733_LOG%" "%A733_TARGET%\A733OfflineProvision.log" >nul
echo A733_OFFLINE_PROVISION_FAIL rc=%A733_RC%
ping.exe -n 121 127.0.0.1 >nul
exit /b %A733_RC%

:set_boot_target
set "A733_ESP="
for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do (
    if exist "%%D:\EFI\Microsoft\Boot\BCD" set "A733_ESP=%%D:"
)
if not defined A733_ESP exit /b 2
call :run bcdedit.exe /store %A733_ESP%\EFI\Microsoft\Boot\BCD /set {default} device partition=%~1
if errorlevel 1 exit /b %ERRORLEVEL%
call :run bcdedit.exe /store %A733_ESP%\EFI\Microsoft\Boot\BCD /set {default} osdevice partition=%~1
if errorlevel 1 exit /b %ERRORLEVEL%
call :run bcdedit.exe /store %A733_ESP%\EFI\Microsoft\Boot\BCD /deletevalue {default} winpe
exit /b %ERRORLEVEL%

:run
echo A733_OFFLINE_RUN=%*
>>"%A733_LOG%" echo A733_OFFLINE_RUN=%*
%* >>"%A733_LOG%" 2>&1
set "A733_RC=%ERRORLEVEL%"
type "%A733_LOG%"
echo A733_OFFLINE_RC=%A733_RC%
>>"%A733_LOG%" echo A733_OFFLINE_RC=%A733_RC%
exit /b %A733_RC%
