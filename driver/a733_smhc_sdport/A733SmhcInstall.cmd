@echo off
setlocal EnableExtensions EnableDelayedExpansion
set "A733_RESULT_HOLD_PINGS=301"
if "%A733_UART_LOG_ACTIVE%"=="1" set "A733_RESULT_HOLD_PINGS=31"

set "A733_PKG=%SystemRoot%\System32\A733Smhc"
set "A733_LOG=%SystemDrive%\A733SmhcInstall.log"
set "A733_TMP=%SystemDrive%\A733SmhcStage.tmp"
set "A733_PROBE_OUT=%SystemDrive%\A733SmhcDiskProbe.out"
set "A733_WRITE_PROBE_OUT=%SystemDrive%\A733SmhcWriteProbe.out"
set "A733_VDS_SMOKE_OUT=%SystemDrive%\A733VdsSmoke.out"
set "A733_DISKPART_EXE=%SystemRoot%\System32\diskpart.exe"
set "A733_DISKPART_EXIT_SCRIPT=%SystemDrive%\A733DiskpartExitOnly.txt"
set "A733_DISKPART_LIST_SCRIPT=%SystemDrive%\A733DiskpartListDisk.txt"
set "A733_DISKPART_ASSIGN_SCRIPT=%SystemDrive%\A733DiskpartAssignMedia.txt"
set "A733_DISKPART_ASSIGN_CMD=%SystemDrive%\A733DiskpartAssignMedia.cmd"
set "A733_DISKPART_ASSIGN_OUT=%SystemDrive%\A733DiskpartAssignMedia.out"
set "A733_DISKPART_ASSIGN_DONE=%SystemDrive%\A733DiskpartAssignMedia.done"
set "A733_MEDIA_ASSIGN_SCRIPT=%SystemDrive%\A733MediaAssign.txt"
set "A733_MEDIA_ASSIGN_CMD=%SystemDrive%\A733MediaAssignRun.cmd"
set "A733_MEDIA_ASSIGN_OUT=%SystemDrive%\A733MediaAssign.out"
set "A733_MEDIA_ASSIGN_DONE=%SystemDrive%\A733MediaAssign.done"
set "A733_MEDIA_MOUNT_OUT=%SystemDrive%\A733MediaMount.out"
set "A733_SETUP_WATCHDOG_CMD=%SystemDrive%\A733SetupWatchdog.cmd"
set "A733_SETUP_ARGS="
set "A733_SKIP_SETUP_EXISTENCE_CHECK=0"
set "A733_SKIP_SETUP_PUSHD=0"
set "A733_STAGE_SETUP_FROM_X_SOURCES=0"
set "A733_SETUP_STAGE_DIR=%SystemDrive%\A733SetupStage"
set "A733_OVERLAY_X_SOURCES_FROM_STAGE=0"
set "A733_OVERLAY_X_SOURCES_MINIMAL=0"
set "A733_TRACE_LOG=%SystemDrive%\A733SmhcTrace.log"
set "A733_TRACE_SUMMARY_OUT=%SystemDrive%\A733SmhcTraceSummary.out"
set "A733_TRACE_READY=%SystemDrive%\A733SmhcTrace.ready"
set "A733_TRACE_DONE=%SystemDrive%\A733SmhcTrace.ready.done"
set "A733_PERSIST_LOG="
set "A733_PERSIST_DRIVE="
set "A733_DISK_PUBLICATION_ONLY_FLAG=%SystemRoot%\System32\A733SmhcDiskPublicationOnly.flag"
set "A733_DISK_PUBLICATION_ONLY=0"
if exist "%A733_DISK_PUBLICATION_ONLY_FLAG%" set "A733_DISK_PUBLICATION_ONLY=1"
set "A733_SKIP_VOLUME_WRITE_PROBE=1"
set "A733_SKIP_DISKPART_SMOKE=1"
set "A733_SKIP_MEDIA_DISKPART_ASSIGN=1"
set "A733_SKIP_MEDIA_DRIVE_SCAN=1"
set "A733_SKIP_MEDIA_ASSIGN=1"
set "A733_USE_PROBE_T_SETUP=1"
set "A733_SKIP_DISK_PROBE_FOR_MEDIA_TINY_WRITE=0"
set "A733_SKIP_MEDIA_TINY_WRITE_PROBE=1"
set "A733_SKIP_POST_PNP_ENUM=1"
set "A733_STOP_AFTER_PNP_INSTALL_HOLD_MS=0"
set "A733_STOP_AFTER_SCAN_HOLD_MS=0"
set "A733_DISK_PROBE_MODE=RAW_GPT_ONLY"
set "A733_SKIP_PERSIST_HELPER=1"
set "A733_DISABLE_ALL_PERSIST=1"
set "A733_SETUP_ASYNC_DIAGNOSTIC=1"
set "A733_SETUP_DIAGNOSTIC_WAIT_SECONDS=90"
set "A733_SETUP_EXE=%SystemDrive%\sources\setup.exe"
set "A733_SETUP_DIR=%SystemDrive%\sources"
set "A733_MEDIA_DRIVE="
set "A733_STOP_AFTER_MEDIA_TINY_WRITE=0"
set "A733_STOP_AFTER_RAW_PERSIST_PROBE=0"
set "A733_PRE_MEDIA_PERSIST_RC=not_run"
set "A733_TRACE_STARTED=0"
set "A733_RESULT=1"
set "A733_CERT_RC=not_run"
set "A733_SKIP_TRACE_FINAL_SUMMARY=1"
set "A733_SKIP_PRE_DRIVER_VDS_PROBE=1"
set "A733_PRE_DRIVER_VDS_RC=not_run"
set "A733_PNP_RC=not_run"
set "A733_SCAN_RC=not_run"
set "A733_ENUM_RC=not_run"
set "A733_DISK_RC=not_run"
set "A733_PROBE_RC=not_run"
set "A733_WRITE_PROBE_RC=not_run"
set "A733_VDS_QUERY_BEFORE_RC=not_run"
set "A733_VDS_START_RC=not_run"
set "A733_VDS_QUERY_AFTER_RC=not_run"
set "A733_DISKPART_PATH_STATUS=not_run"
set "A733_DISKPART_HELP_RC=not_run"
set "A733_DISKPART_EXIT_RC=not_run"
set "A733_DISKPART_LIST_RC=not_run"
set "A733_DISKPART_RC=not_run"
set "A733_VDS_SMOKE_RC=not_run"
set "A733_DISKPART_SMOKE_RC=not_run"
set "A733_TRACE_FINAL_DUMP_RC=not_run"

>"%A733_LOG%" echo A733_SMHC_STAGE_BEGIN
if exist "%A733_PROBE_OUT%" del /f /q "%A733_PROBE_OUT%" >nul 2>&1
if exist "%A733_WRITE_PROBE_OUT%" del /f /q "%A733_WRITE_PROBE_OUT%" >nul 2>&1
if exist "%A733_VDS_SMOKE_OUT%" del /f /q "%A733_VDS_SMOKE_OUT%" >nul 2>&1
if exist "%A733_DISKPART_EXIT_SCRIPT%" del /f /q "%A733_DISKPART_EXIT_SCRIPT%" >nul 2>&1
if exist "%A733_DISKPART_LIST_SCRIPT%" del /f /q "%A733_DISKPART_LIST_SCRIPT%" >nul 2>&1
if exist "%A733_DISKPART_ASSIGN_SCRIPT%" del /f /q "%A733_DISKPART_ASSIGN_SCRIPT%" >nul 2>&1
if exist "%A733_DISKPART_ASSIGN_CMD%" del /f /q "%A733_DISKPART_ASSIGN_CMD%" >nul 2>&1
if exist "%A733_DISKPART_ASSIGN_OUT%" del /f /q "%A733_DISKPART_ASSIGN_OUT%" >nul 2>&1
if exist "%A733_DISKPART_ASSIGN_DONE%" del /f /q "%A733_DISKPART_ASSIGN_DONE%" >nul 2>&1
if exist "%A733_MEDIA_ASSIGN_SCRIPT%" del /f /q "%A733_MEDIA_ASSIGN_SCRIPT%" >nul 2>&1
if exist "%A733_MEDIA_ASSIGN_CMD%" del /f /q "%A733_MEDIA_ASSIGN_CMD%" >nul 2>&1
if exist "%A733_MEDIA_ASSIGN_OUT%" del /f /q "%A733_MEDIA_ASSIGN_OUT%" >nul 2>&1
if exist "%A733_MEDIA_ASSIGN_DONE%" del /f /q "%A733_MEDIA_ASSIGN_DONE%" >nul 2>&1
if exist "%A733_MEDIA_MOUNT_OUT%" del /f /q "%A733_MEDIA_MOUNT_OUT%" >nul 2>&1
>>"%A733_LOG%" echo reason=owned_device_WinPE_storage_bringup
>>"%A733_LOG%" echo policy=no_KD no_register_write no_security_bypass
>>"%A733_LOG%" echo trust_change=WinPE_RAM_only revert=power_cycle
>>"%A733_LOG%" echo disk_publication_only=%A733_DISK_PUBLICATION_ONLY%
>>"%A733_LOG%" echo skip_volume_write_probe=%A733_SKIP_VOLUME_WRITE_PROBE%
>>"%A733_LOG%" echo skip_diskpart_smoke=%A733_SKIP_DISKPART_SMOKE%
>>"%A733_LOG%" echo skip_media_diskpart_assign=%A733_SKIP_MEDIA_DISKPART_ASSIGN%
>>"%A733_LOG%" echo skip_media_drive_scan=%A733_SKIP_MEDIA_DRIVE_SCAN%
>>"%A733_LOG%" echo skip_media_assign=%A733_SKIP_MEDIA_ASSIGN%
>>"%A733_LOG%" echo use_probe_t_setup=%A733_USE_PROBE_T_SETUP%
>>"%A733_LOG%" echo skip_disk_probe_for_media_tiny_write=%A733_SKIP_DISK_PROBE_FOR_MEDIA_TINY_WRITE%
>>"%A733_LOG%" echo skip_media_tiny_write_probe=%A733_SKIP_MEDIA_TINY_WRITE_PROBE%
>>"%A733_LOG%" echo skip_post_pnp_enum=%A733_SKIP_POST_PNP_ENUM%
>>"%A733_LOG%" echo stop_after_pnp_install_hold_ms=%A733_STOP_AFTER_PNP_INSTALL_HOLD_MS%
>>"%A733_LOG%" echo stop_after_scan_hold_ms=%A733_STOP_AFTER_SCAN_HOLD_MS%
>>"%A733_LOG%" echo disk_probe_mode=%A733_DISK_PROBE_MODE%
>>"%A733_LOG%" echo skip_persist_helper=%A733_SKIP_PERSIST_HELPER%
>>"%A733_LOG%" echo disable_all_persist=%A733_DISABLE_ALL_PERSIST%
>>"%A733_LOG%" echo setup_async_diagnostic=%A733_SETUP_ASYNC_DIAGNOSTIC%
>>"%A733_LOG%" echo setup_dir=%A733_SETUP_DIR%
>>"%A733_LOG%" echo stop_after_media_tiny_write=%A733_STOP_AFTER_MEDIA_TINY_WRITE%
>>"%A733_LOG%" echo stop_after_raw_persist_probe=%A733_STOP_AFTER_RAW_PERSIST_PROBE%
>>"%A733_LOG%" echo skip_trace_final_summary=%A733_SKIP_TRACE_FINAL_SUMMARY%
>>"%A733_LOG%" echo skip_pre_driver_vds_probe=%A733_SKIP_PRE_DRIVER_VDS_PROBE%
>>"%A733_LOG%" echo persistent_log_name=A733DIAG.LOG

echo.
echo ================================================================
echo A733 SMHC WinPE diagnostic install
echo ================================================================
type "%A733_LOG%"

echo.
echo --- Active boot policy ---
bcdedit.exe /enum all >"%A733_TMP%" 2>&1
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"

"%A733_PKG%\A733WinPeCertInstall.exe" >"%A733_TMP%" 2>&1
set "A733_CERT_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_CERT_RC=%A733_CERT_RC%
>>"%A733_LOG%" echo A733_CERT_RC=%A733_CERT_RC%
if not "%A733_CERT_RC%"=="0" (
    set "A733_RESULT=%A733_CERT_RC%"
    goto :failed
)

del /q "%A733_TRACE_LOG%" "%A733_TRACE_SUMMARY_OUT%" "%A733_TRACE_READY%" "%A733_TRACE_DONE%" >nul 2>&1
if exist "%A733_TRACE_LOG%" (
    set "A733_RESULT=3"
    goto :failed
)
if exist "%A733_TRACE_SUMMARY_OUT%" (
    set "A733_RESULT=3"
    goto :failed
)
if exist "%A733_TRACE_READY%" (
    set "A733_RESULT=3"
    goto :failed
)
if exist "%A733_TRACE_DONE%" (
    set "A733_RESULT=3"
    goto :failed
)
start "A733SmhcTrace" /b "%A733_PKG%\A733WinPeSmhcTrace.exe" capture "%A733_TRACE_LOG%" "%A733_TRACE_READY%"
set "A733_TRACE_STARTED=1"
for /L %%I in (1,1,50) do (
    if exist "%A733_TRACE_READY%" goto :trace_ready
    ping.exe -n 2 127.0.0.1 >nul
)
echo A733_TRACE_START status=FAIL reason=ready_timeout
>>"%A733_LOG%" echo A733_TRACE_START status=FAIL reason=ready_timeout
set "A733_RESULT=1460"
goto :failed

:trace_ready
echo A733_TRACE_START status=PASS provider={B13D8EB4-3F46-4BE7-9A92-58F91D7C1E40}
>>"%A733_LOG%" echo A733_TRACE_START status=PASS provider={B13D8EB4-3F46-4BE7-9A92-58F91D7C1E40}

echo.
echo --- Pre-driver VDS LoadService-only probe ---
if "%A733_SKIP_PRE_DRIVER_VDS_PROBE%"=="1" (
    echo A733_PRE_DRIVER_VDS_PROBE_SKIP reason=avoid_known_com_vds_keinsertqueueapc_frontier
    >>"%A733_LOG%" echo A733_PRE_DRIVER_VDS_PROBE_SKIP reason=avoid_known_com_vds_keinsertqueueapc_frontier
    set "A733_PRE_DRIVER_VDS_RC=skipped"
    goto :after_pre_driver_vds_probe
)
echo A733_PRE_DRIVER_VDS_PROBE_LIVE_BEGIN reason=loadservice_before_a733_driver_publication
>>"%A733_LOG%" echo A733_PRE_DRIVER_VDS_PROBE_LIVE_BEGIN reason=loadservice_before_a733_driver_publication
set "A733_DISK_PROBE_MODE_SAVED=%A733_DISK_PROBE_MODE%"
set "A733_DISK_PROBE_MODE=VDS_ONLY_PRE_DRIVER"
"%A733_PKG%\A733WinPeDiskProbe.exe"
set "A733_PRE_DRIVER_VDS_RC=%ERRORLEVEL%"
set "A733_DISK_PROBE_MODE=%A733_DISK_PROBE_MODE_SAVED%"
set "A733_DISK_PROBE_MODE_SAVED="
echo A733_PRE_DRIVER_VDS_PROBE_LIVE_END rc=%A733_PRE_DRIVER_VDS_RC%
>>"%A733_LOG%" echo A733_PRE_DRIVER_VDS_PROBE_LIVE_END rc=%A733_PRE_DRIVER_VDS_RC%
echo A733_PRE_DRIVER_VDS_RC=%A733_PRE_DRIVER_VDS_RC%
>>"%A733_LOG%" echo A733_PRE_DRIVER_VDS_RC=%A733_PRE_DRIVER_VDS_RC%
if not "%A733_PRE_DRIVER_VDS_RC%"=="0" (
    set "A733_RESULT=%A733_PRE_DRIVER_VDS_RC%"
    goto :failed
)

:after_pre_driver_vds_probe

echo A733_PNP_INSTALL_BEGIN inf="%A733_PKG%\a733_smhc.inf"
>>"%A733_LOG%" echo A733_PNP_INSTALL_BEGIN inf="%A733_PKG%\a733_smhc.inf"
pnputil.exe /add-driver "%A733_PKG%\a733_smhc.inf" /install >"%A733_TMP%" 2>&1
set "A733_PNP_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_PNP_RC=%A733_PNP_RC%
>>"%A733_LOG%" echo A733_PNP_RC=%A733_PNP_RC%
if not "%A733_PNP_RC%"=="0" (
    set "A733_RESULT=%A733_PNP_RC%"
    goto :failed
)
if not "%A733_STOP_AFTER_PNP_INSTALL_HOLD_MS%"=="0" (
    echo A733_PNP_INSTALL_HOLD milliseconds=%A733_STOP_AFTER_PNP_INSTALL_HOLD_MS% reason=separate_driver_publication_from_scan_and_disk_probe
    >>"%A733_LOG%" echo A733_PNP_INSTALL_HOLD milliseconds=%A733_STOP_AFTER_PNP_INSTALL_HOLD_MS% reason=separate_driver_publication_from_scan_and_disk_probe
    ping.exe -n 121 127.0.0.1 >nul
    echo A733_PNP_INSTALL_HOLD_END status=RETURNED
    >>"%A733_LOG%" echo A733_PNP_INSTALL_HOLD_END status=RETURNED
    goto :disk_publication_only_done
)

pnputil.exe /scan-devices >"%A733_TMP%" 2>&1
set "A733_SCAN_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_SCAN_RC=%A733_SCAN_RC%
>>"%A733_LOG%" echo A733_SCAN_RC=%A733_SCAN_RC%
if not "%A733_SCAN_RC%"=="0" (
    set "A733_RESULT=%A733_SCAN_RC%"
    goto :failed
)
if not "%A733_STOP_AFTER_SCAN_HOLD_MS%"=="0" (
    echo A733_SCAN_HOLD milliseconds=%A733_STOP_AFTER_SCAN_HOLD_MS% reason=separate_scan_devices_from_enum_and_disk_probe
    >>"%A733_LOG%" echo A733_SCAN_HOLD milliseconds=%A733_STOP_AFTER_SCAN_HOLD_MS% reason=separate_scan_devices_from_enum_and_disk_probe
    ping.exe -n 121 127.0.0.1 >nul
    echo A733_SCAN_HOLD_END status=RETURNED
    >>"%A733_LOG%" echo A733_SCAN_HOLD_END status=RETURNED
    goto :disk_publication_only_done
)

echo.
if "%A733_SKIP_POST_PNP_ENUM%"=="1" (
    echo A733_POST_PNP_ENUM_SKIP reason=focus_disk_probe_after_scan
    >>"%A733_LOG%" echo A733_POST_PNP_ENUM_SKIP reason=focus_disk_probe_after_scan
) else (
    echo --- AWI0001 controller ---
    pnputil.exe /enum-devices /deviceid "ACPI\AWI0001" /drivers /services /resources >"%A733_TMP%" 2>&1
    set "A733_ENUM_RC=!ERRORLEVEL!"
    type "%A733_TMP%"
    type "%A733_TMP%" >>"%A733_LOG%"
    echo A733_ENUM_RC=%A733_ENUM_RC%
    >>"%A733_LOG%" echo A733_ENUM_RC=%A733_ENUM_RC%
    if not "%A733_ENUM_RC%"=="0" (
        set "A733_RESULT=%A733_ENUM_RC%"
        goto :failed
    )

    echo.
    echo --- Connected disk devices ---
    pnputil.exe /enum-devices /connected /class DiskDrive /drivers >"%A733_TMP%" 2>&1
    set "A733_DISK_RC=!ERRORLEVEL!"
    type "%A733_TMP%"
    type "%A733_TMP%" >>"%A733_LOG%"
    echo A733_DISK_RC=%A733_DISK_RC%
    >>"%A733_LOG%" echo A733_DISK_RC=%A733_DISK_RC%
    if not "%A733_DISK_RC%"=="0" (
        set "A733_RESULT=%A733_DISK_RC%"
        goto :failed
    )
)

echo.
echo --- Read-only service, geometry, layout, and sector probe ---
echo A733_DISK_PROBE_LIVE_BEGIN reason=dynamic_ole32_stage_visibility
>>"%A733_LOG%" echo A733_DISK_PROBE_LIVE_BEGIN reason=dynamic_ole32_stage_visibility
if "%A733_SKIP_DISK_PROBE_FOR_MEDIA_TINY_WRITE%"=="1" (
    set "A733_PROBE_RC=skipped"
    echo A733_DISK_PROBE_SKIP reason=media_tiny_write_focus
    >>"%A733_LOG%" echo A733_DISK_PROBE_SKIP reason=media_tiny_write_focus
) else (
    echo A733_DISK_PROBE_CAPTURE mode=live_console reason=partition_vbr_read_frontier
    >>"%A733_LOG%" echo A733_DISK_PROBE_CAPTURE mode=live_console reason=partition_vbr_read_frontier
    "%A733_PKG%\A733WinPeDiskProbe.exe"
    set "A733_PROBE_RC=!ERRORLEVEL!"
)
echo A733_DISK_PROBE_LIVE_END rc=%A733_PROBE_RC%
>>"%A733_LOG%" echo A733_DISK_PROBE_LIVE_END rc=%A733_PROBE_RC%
if exist "%A733_PROBE_OUT%" (
    type "%A733_PROBE_OUT%"
    type "%A733_PROBE_OUT%" >>"%A733_LOG%"
)
echo A733_PROBE_RC=%A733_PROBE_RC%
>>"%A733_LOG%" echo A733_PROBE_RC=%A733_PROBE_RC%
if not "%A733_PROBE_RC%"=="0" (
    set "A733_RESULT=%A733_PROBE_RC%"
    if "%A733_STOP_AFTER_MEDIA_TINY_WRITE%"=="1" (
        echo A733_DISK_PROBE_NONFATAL reason=media_tiny_write_probe probe_rc=%A733_PROBE_RC%
        >>"%A733_LOG%" echo A733_DISK_PROBE_NONFATAL reason=media_tiny_write_probe probe_rc=%A733_PROBE_RC%
        set "A733_RESULT=0"
    ) else (
        goto :failed
    )
)
if "%A733_DISK_PUBLICATION_ONLY%"=="1" goto :disk_publication_only_done

echo.
echo --- Guarded A733SETUP temporary-file write, flush, and readback probe ---
if "%A733_SKIP_VOLUME_WRITE_PROBE%"=="1" (
    set "A733_WRITE_PROBE_RC=skipped"
    echo A733_WRITE_PROBE_SKIP mode=entire_probe reason=diskpart_startup_ab
    >>"%A733_LOG%" echo A733_WRITE_PROBE_SKIP mode=entire_probe reason=diskpart_startup_ab
) else (
    echo A733_WRITE_PROBE_CAPTURE mode=live_console reason=bugcheck_before_redirected_output_replay
    "%A733_PKG%\A733WinPeVolumeWriteProbe.exe"
    set "A733_WRITE_PROBE_RC=!ERRORLEVEL!"
    if exist "%A733_WRITE_PROBE_OUT%" (
        type "%A733_WRITE_PROBE_OUT%"
        type "%A733_WRITE_PROBE_OUT%" >>"%A733_LOG%"
    )
    echo A733_WRITE_PROBE_RC=%A733_WRITE_PROBE_RC%
    >>"%A733_LOG%" echo A733_WRITE_PROBE_RC=%A733_WRITE_PROBE_RC%
    if not "%A733_WRITE_PROBE_RC%"=="0" (
        set "A733_RESULT=%A733_WRITE_PROBE_RC%"
        goto :failed
    )
)

if "%A733_SKIP_DISKPART_SMOKE%"=="1" (
    set "A733_DISKPART_SMOKE_RC=skipped"
    set "A733_VDS_SMOKE_RC=skipped"
    echo A733_DISKPART_SMOKE_SKIP reason=exit_only_hang
    >>"%A733_LOG%" echo A733_DISKPART_SMOKE_SKIP reason=exit_only_hang
) else (
    call :run_diskpart_smoke
    set "A733_DISKPART_SMOKE_RC=!ERRORLEVEL!"
    set "A733_VDS_SMOKE_RC=%A733_DISKPART_SMOKE_RC%"
    echo A733_DISKPART_SMOKE_RC=%A733_DISKPART_SMOKE_RC%
    >>"%A733_LOG%" echo A733_DISKPART_SMOKE_RC=%A733_DISKPART_SMOKE_RC%
    if not "%A733_DISKPART_SMOKE_RC%"=="0" (
        set "A733_RESULT=%A733_DISKPART_SMOKE_RC%"
        goto :failed
    )
)
echo A733_SMHC_STAGE_PASS
>>"%A733_LOG%" echo A733_SMHC_STAGE_PASS
set "A733_RESULT=0"
goto :show_result

:disk_publication_only_done
set "A733_RESULT=0"
call :stop_trace
set "A733_TRACE_FINAL_RC=%ERRORLEVEL%"
if not "%A733_TRACE_FINAL_RC%"=="0" set "A733_RESULT=%A733_TRACE_FINAL_RC%"
echo.
echo --- Disk-publication-only diagnostic result ---
echo A733_DISK_PUBLICATION_ONLY status=STOP_AFTER_DISK_PROBE result=%A733_RESULT% trace_rc=%A733_TRACE_FINAL_RC%
>>"%A733_LOG%" echo A733_DISK_PUBLICATION_ONLY status=STOP_AFTER_DISK_PROBE result=%A733_RESULT% trace_rc=%A733_TRACE_FINAL_RC%
if exist "%A733_PROBE_OUT%" (
    echo.
    echo --- Final visible A733WinPeDiskProbe output ---
    type "%A733_PROBE_OUT%"
    >>"%A733_LOG%" echo A733_DISK_PUBLICATION_ONLY_REPLAY_DISK_PROBE_BEGIN
    type "%A733_PROBE_OUT%" >>"%A733_LOG%"
) else (
    echo A733_PROBE_OUT status=missing
    >>"%A733_LOG%" echo A733_PROBE_OUT status=missing
)
if exist "%A733_TRACE_SUMMARY_OUT%" (
    echo.
    echo --- Replayed A733 SMHC ETW summary ---
    type "%A733_TRACE_SUMMARY_OUT%"
    >>"%A733_LOG%" echo A733_DISK_PUBLICATION_ONLY_REPLAY_TRACE_SUMMARY_BEGIN
    type "%A733_TRACE_SUMMARY_OUT%" >>"%A733_LOG%"
)
echo A733_FINAL_VISIBLE_DISK_PUBLICATION_ONLY_END
>>"%A733_LOG%" echo A733_FINAL_VISIBLE_DISK_PUBLICATION_ONLY_END
del /q "%A733_TMP%" >nul 2>&1
echo.
echo Disk-publication-only mode; Windows Setup is intentionally not started.
if "%A733_SKIP_TRACE_FINAL_SUMMARY%"=="1" (
    echo A733_FINAL_VISIBLE_TRACE_RECAP_SKIP reason=skip_trace_final_summary
    >>"%A733_LOG%" echo A733_FINAL_VISIBLE_TRACE_RECAP_SKIP reason=skip_trace_final_summary
) else if exist "%A733_TRACE_SUMMARY_OUT%" (
    echo.
    echo --- Final visible A733 SMHC low-arg recap ---
    "%A733_PKG%\A733WinPeSmhcTrace.exe" lowargsummary "%A733_TRACE_LOG%"
    echo.
    echo --- Final visible A733 SMHC final 220 events ---
    "%A733_PKG%\A733WinPeSmhcTrace.exe" dump "%A733_TRACE_LOG%" 220
)
ping.exe -n %A733_RESULT_HOLD_PINGS% 127.0.0.1 >nul
endlocal & exit /b %A733_RESULT%

:failed
call :stop_trace
echo.
echo --- A733 failure summary (reprinted after trace) ---
echo A733_RESULT=%A733_RESULT%
echo A733_CERT_RC=%A733_CERT_RC%
echo A733_PNP_RC=%A733_PNP_RC%
echo A733_SCAN_RC=%A733_SCAN_RC%
echo A733_ENUM_RC=%A733_ENUM_RC%
echo A733_DISK_RC=%A733_DISK_RC%
echo A733_PROBE_RC=%A733_PROBE_RC%
echo A733_WRITE_PROBE_RC=%A733_WRITE_PROBE_RC%
echo A733_VDS_QUERY_BEFORE_RC=%A733_VDS_QUERY_BEFORE_RC%
echo A733_VDS_START_RC=%A733_VDS_START_RC%
echo A733_VDS_QUERY_AFTER_RC=%A733_VDS_QUERY_AFTER_RC%
echo A733_DISKPART_PATH_STATUS=%A733_DISKPART_PATH_STATUS%
echo A733_DISKPART_HELP_RC=%A733_DISKPART_HELP_RC%
echo A733_DISKPART_EXIT_RC=%A733_DISKPART_EXIT_RC%
echo A733_DISKPART_LIST_RC=%A733_DISKPART_LIST_RC%
echo A733_DISKPART_RC=%A733_DISKPART_RC%
echo A733_VDS_SMOKE_RC=%A733_VDS_SMOKE_RC%
echo A733_DISKPART_SMOKE_RC=%A733_DISKPART_SMOKE_RC%
>>"%A733_LOG%" echo A733_FAILURE_SUMMARY_BEGIN
>>"%A733_LOG%" echo A733_RESULT=%A733_RESULT%
>>"%A733_LOG%" echo A733_CERT_RC=%A733_CERT_RC%
>>"%A733_LOG%" echo A733_PNP_RC=%A733_PNP_RC%
>>"%A733_LOG%" echo A733_SCAN_RC=%A733_SCAN_RC%
>>"%A733_LOG%" echo A733_ENUM_RC=%A733_ENUM_RC%
>>"%A733_LOG%" echo A733_DISK_RC=%A733_DISK_RC%
>>"%A733_LOG%" echo A733_PROBE_RC=%A733_PROBE_RC%
>>"%A733_LOG%" echo A733_WRITE_PROBE_RC=%A733_WRITE_PROBE_RC%
>>"%A733_LOG%" echo A733_VDS_QUERY_BEFORE_RC=%A733_VDS_QUERY_BEFORE_RC%
>>"%A733_LOG%" echo A733_VDS_START_RC=%A733_VDS_START_RC%
>>"%A733_LOG%" echo A733_VDS_QUERY_AFTER_RC=%A733_VDS_QUERY_AFTER_RC%
>>"%A733_LOG%" echo A733_DISKPART_PATH_STATUS=%A733_DISKPART_PATH_STATUS%
>>"%A733_LOG%" echo A733_DISKPART_HELP_RC=%A733_DISKPART_HELP_RC%
>>"%A733_LOG%" echo A733_DISKPART_EXIT_RC=%A733_DISKPART_EXIT_RC%
>>"%A733_LOG%" echo A733_DISKPART_LIST_RC=%A733_DISKPART_LIST_RC%
>>"%A733_LOG%" echo A733_DISKPART_RC=%A733_DISKPART_RC%
>>"%A733_LOG%" echo A733_VDS_SMOKE_RC=%A733_VDS_SMOKE_RC%
>>"%A733_LOG%" echo A733_DISKPART_SMOKE_RC=%A733_DISKPART_SMOKE_RC%
if exist "%A733_PROBE_OUT%" (
    echo.
    echo --- Replayed A733WinPeDiskProbe output ---
    type "%A733_PROBE_OUT%"
    >>"%A733_LOG%" echo A733_FAILURE_REPLAY_DISK_PROBE_BEGIN
    type "%A733_PROBE_OUT%" >>"%A733_LOG%"
)
if exist "%A733_WRITE_PROBE_OUT%" (
    echo.
    echo --- Replayed A733WinPeVolumeWriteProbe output ---
    type "%A733_WRITE_PROBE_OUT%"
    >>"%A733_LOG%" echo A733_FAILURE_REPLAY_WRITE_PROBE_BEGIN
    type "%A733_WRITE_PROBE_OUT%" >>"%A733_LOG%"
)
if exist "%A733_VDS_SMOKE_OUT%" (
    echo.
    echo --- Replayed A733 VDS/diskpart smoke output ---
    type "%A733_VDS_SMOKE_OUT%"
    >>"%A733_LOG%" echo A733_FAILURE_REPLAY_VDS_SMOKE_BEGIN
    type "%A733_VDS_SMOKE_OUT%" >>"%A733_LOG%"
)
if exist "%A733_TRACE_SUMMARY_OUT%" (
    echo.
    echo --- Replayed A733 SMHC ETW summary ---
    type "%A733_TRACE_SUMMARY_OUT%"
    >>"%A733_LOG%" echo A733_FAILURE_REPLAY_TRACE_SUMMARY_BEGIN
    type "%A733_TRACE_SUMMARY_OUT%" >>"%A733_LOG%"
)
>>"%A733_LOG%" echo A733_FAILURE_SUMMARY_END
echo A733_SMHC_STAGE_FAIL
>>"%A733_LOG%" echo A733_SMHC_STAGE_FAIL
if exist "%A733_TRACE_LOG%" (
    echo.
    echo --- Replayed A733 SMHC ETW final 60 events ---
    "%A733_PKG%\A733WinPeSmhcTrace.exe" dump "%A733_TRACE_LOG%" 60 >"%A733_TMP%" 2>&1
    set "A733_TRACE_FINAL_DUMP_RC=!ERRORLEVEL!"
    type "%A733_TMP%"
    >>"%A733_LOG%" echo A733_FAILURE_REPLAY_TRACE_FINAL60_BEGIN
    type "%A733_TMP%" >>"%A733_LOG%"
    call echo A733_TRACE_FINAL_DUMP_RC=%%A733_TRACE_FINAL_DUMP_RC%%
    call >>"%A733_LOG%" echo A733_TRACE_FINAL_DUMP_RC=%%A733_TRACE_FINAL_DUMP_RC%%
)
echo.
echo --- Final visible A733 failure summary ^(held after trace dump^) ---
echo A733_RESULT=%A733_RESULT%
echo A733_CERT_RC=%A733_CERT_RC%
echo A733_PNP_RC=%A733_PNP_RC%
echo A733_SCAN_RC=%A733_SCAN_RC%
echo A733_ENUM_RC=%A733_ENUM_RC%
echo A733_DISK_RC=%A733_DISK_RC%
echo A733_PROBE_RC=%A733_PROBE_RC%
echo A733_WRITE_PROBE_RC=%A733_WRITE_PROBE_RC%
echo A733_VDS_QUERY_BEFORE_RC=%A733_VDS_QUERY_BEFORE_RC%
echo A733_VDS_START_RC=%A733_VDS_START_RC%
echo A733_VDS_QUERY_AFTER_RC=%A733_VDS_QUERY_AFTER_RC%
echo A733_DISKPART_PATH_STATUS=%A733_DISKPART_PATH_STATUS%
echo A733_DISKPART_HELP_RC=%A733_DISKPART_HELP_RC%
echo A733_DISKPART_EXIT_RC=%A733_DISKPART_EXIT_RC%
echo A733_DISKPART_LIST_RC=%A733_DISKPART_LIST_RC%
echo A733_DISKPART_RC=%A733_DISKPART_RC%
echo A733_VDS_SMOKE_RC=%A733_VDS_SMOKE_RC%
echo A733_DISKPART_SMOKE_RC=%A733_DISKPART_SMOKE_RC%
echo.
echo --- Final visible storage service state ---
for %%S in (a733smhc sdbus sdstor disk partmgr) do (
    echo A733_SERVICE_QUERY name=%%S
    >>"%A733_LOG%" echo A733_SERVICE_QUERY name=%%S
    sc.exe query %%S >"%A733_TMP%" 2>&1
    type "%A733_TMP%" | findstr.exe /i /c:"SERVICE_NAME" /c:"STATE" /c:"WIN32_EXIT_CODE" /c:"SERVICE_EXIT_CODE" /c:"CHECKPOINT"
    type "%A733_TMP%" >>"%A733_LOG%"
)
if exist "%A733_PROBE_OUT%" (
    echo.
    echo --- Final visible A733WinPeDiskProbe output ---
    type "%A733_PROBE_OUT%"
) else (
    echo A733_PROBE_OUT status=missing
)
if exist "%A733_VDS_SMOKE_OUT%" (
    echo.
    echo --- Final visible A733 VDS/diskpart smoke output ---
    type "%A733_VDS_SMOKE_OUT%"
)
if exist "%A733_TRACE_LOG%" (
    echo.
    if "%A733_SKIP_TRACE_FINAL_SUMMARY%"=="1" (
        echo A733_TRACE_KEYSUMMARY_SKIP reason=skip_trace_final_summary
        >>"%A733_LOG%" echo A733_TRACE_KEYSUMMARY_SKIP reason=skip_trace_final_summary
    ) else (
        echo --- Final visible A733 SMHC key summary ---
        "%A733_PKG%\A733WinPeSmhcTrace.exe" keysummary "%A733_TRACE_LOG%" >"%A733_TMP%" 2>&1
        set "A733_TRACE_KEYSUMMARY_RC=!ERRORLEVEL!"
        type "%A733_TMP%"
        type "%A733_TMP%" >>"%A733_LOG%"
        echo A733_TRACE_KEYSUMMARY_RC=%A733_TRACE_KEYSUMMARY_RC%
        >>"%A733_LOG%" echo A733_TRACE_KEYSUMMARY_RC=%A733_TRACE_KEYSUMMARY_RC%
    )
)
echo A733_FINAL_VISIBLE_FAILURE_END

:show_result
del /q "%A733_TMP%" >nul 2>&1
echo.
echo Probe helper already held its result for 20 seconds.

if not "%A733_RESULT%"=="0" goto :hold_failure
if "%A733_DISABLE_ALL_PERSIST%"=="1" (
    set "A733_PRE_MEDIA_PERSIST_RC=0"
) else (
    call :syncpersist "pre_media_assign"
    set "A733_PRE_MEDIA_PERSIST_RC=%ERRORLEVEL%"
)
if "%A733_STOP_AFTER_RAW_PERSIST_PROBE%"=="1" goto :raw_persist_probe_done
if "%A733_USE_PROBE_T_SETUP%"=="1" (
    set "A733_MEDIA_DRIVE=T:"
    set "A733_OVERLAY_X_SOURCES_MINIMAL=0"
    set "A733_SETUP_EXE=T:\setup.exe"
    set "A733_SETUP_DIR=T:\"
    set "A733_SETUP_SOURCE=T:\sources\install.wim"
    set "A733_SETUP_SOURCE_KIND=wim_preferred_or_missing"
    if not exist "T:\sources\install.wim" if exist "T:\sources\install.swm" (
        set "A733_SETUP_SOURCE=T:\sources\install.swm"
        set "A733_SETUP_SOURCE_KIND=split_swm"
    )
    set "A733_SETUP_ARGS=/installfrom:!A733_SETUP_SOURCE!"
    echo A733_MEDIA_ASSIGN_SKIP reason=probe_t_mount_direct_setup setup=T:\setup.exe source=!A733_SETUP_SOURCE! source_kind=!A733_SETUP_SOURCE_KIND! args=!A733_SETUP_ARGS! overlay=none
    >>"%A733_LOG%" echo A733_MEDIA_ASSIGN_SKIP reason=probe_t_mount_direct_setup setup=T:\setup.exe source=!A733_SETUP_SOURCE! source_kind=!A733_SETUP_SOURCE_KIND! args=!A733_SETUP_ARGS! overlay=none
) else if "%A733_SKIP_MEDIA_ASSIGN%"=="1" (
    echo A733_MEDIA_ASSIGN_SKIP reason=setup_x_sources_focus setup=%A733_SETUP_EXE%
    >>"%A733_LOG%" echo A733_MEDIA_ASSIGN_SKIP reason=setup_x_sources_focus setup=%A733_SETUP_EXE%
) else (
    call :assign_media_drive
)
if "%A733_SKIP_MEDIA_TINY_WRITE_PROBE%"=="1" (
    set "A733_MEDIA_TINY_WRITE_RC=skipped"
    echo A733_MEDIA_TINY_WRITE_SKIP mode=entire_probe reason=setup_launch_focus
    >>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_SKIP mode=entire_probe reason=setup_launch_focus
) else (
    call :media_tiny_write_probe
    set "A733_MEDIA_TINY_WRITE_RC=!ERRORLEVEL!"
)
if "%A733_STOP_AFTER_MEDIA_TINY_WRITE%"=="1" goto :media_tiny_write_done
if "%A733_STAGE_SETUP_FROM_X_SOURCES%"=="1" (
    call :stage_setup_from_x_sources
    set "A733_STAGE_SETUP_RC=!ERRORLEVEL!"
    if not "!A733_STAGE_SETUP_RC!"=="0" goto :setup_stage_failed
)
if "%A733_OVERLAY_X_SOURCES_FROM_STAGE%"=="1" (
    call :overlay_x_sources_from_stage
    set "A733_OVERLAY_X_SOURCES_RC=!ERRORLEVEL!"
    if not "!A733_OVERLAY_X_SOURCES_RC!"=="0" goto :setup_stage_failed
)
if "%A733_OVERLAY_X_SOURCES_MINIMAL%"=="1" (
    call :overlay_x_sources_minimal
    set "A733_OVERLAY_X_SOURCES_RC=!ERRORLEVEL!"
    if not "!A733_OVERLAY_X_SOURCES_RC!"=="0" goto :setup_stage_failed
)
if "%A733_SKIP_SETUP_EXISTENCE_CHECK%"=="1" (
    echo A733_SETUP_EXISTENCE_CHECK_SKIP reason=config path=%A733_SETUP_EXE%
    >>"%A733_LOG%" echo A733_SETUP_EXISTENCE_CHECK_SKIP reason=config path=%A733_SETUP_EXE%
) else (
    if not exist "%A733_SETUP_EXE%" goto :setup_missing
)

if "%A733_SETUP_ASYNC_DIAGNOSTIC%"=="1" (
    call :init_persistent_log
    if not "%A733_DISABLE_ALL_PERSIST%"=="1" call :syncpersist "pre_setup"
    echo Starting Windows Setup with watchdog from %A733_SETUP_EXE%
    >>"%A733_LOG%" echo A733_SETUP_START mode=async_watchdog path=%A733_SETUP_EXE% media_drive=%A733_MEDIA_DRIVE%
    call :run_setup_async_diagnostic
) else (
    call :init_persistent_log
    if not "%A733_DISABLE_ALL_PERSIST%"=="1" call :syncpersist "pre_setup"
    echo Starting Windows Setup synchronously from %A733_SETUP_EXE%
    >>"%A733_LOG%" echo A733_SETUP_START mode=synchronous path=%A733_SETUP_EXE% media_drive=%A733_MEDIA_DRIVE%
    "%A733_SETUP_EXE%"
)
set "A733_SETUP_RC=%ERRORLEVEL%"
echo A733_SETUP_RETURN rc=%A733_SETUP_RC%
>>"%A733_LOG%" echo A733_SETUP_RETURN rc=%A733_SETUP_RC%
call :stop_trace
set "A733_TRACE_FINAL_RC=%ERRORLEVEL%"
set "A733_FINAL_RC=%A733_SETUP_RC%"
if "%A733_FINAL_RC%"=="0" set "A733_FINAL_RC=%A733_TRACE_FINAL_RC%"
if not "%A733_TRACE_FINAL_RC%"=="0" (
    echo A733_SETUP_DIAGNOSTIC status=FAIL reason=trace_stop rc=%A733_TRACE_FINAL_RC%
    >>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC status=FAIL reason=trace_stop rc=%A733_TRACE_FINAL_RC%
)
if not "%A733_DISABLE_ALL_PERSIST%"=="1" call :syncpersist "after_setup_return"
ping.exe -n %A733_RESULT_HOLD_PINGS% 127.0.0.1 >nul
endlocal & exit /b %A733_FINAL_RC%

:raw_persist_probe_done
call :stop_trace
set "A733_TRACE_FINAL_RC=%ERRORLEVEL%"
echo.
echo --- Raw persist probe diagnostic result ---
echo A733_RAW_PERSIST_PROBE_ONLY status=STOP persist_rc=%A733_PRE_MEDIA_PERSIST_RC% trace_rc=%A733_TRACE_FINAL_RC%
>>"%A733_LOG%" echo A733_RAW_PERSIST_PROBE_ONLY status=STOP persist_rc=%A733_PRE_MEDIA_PERSIST_RC% trace_rc=%A733_TRACE_FINAL_RC%
if exist "%A733_TRACE_SUMMARY_OUT%" (
    echo.
    echo --- Raw persist probe SMHC ETW summary ---
    type "%A733_TRACE_SUMMARY_OUT%"
    >>"%A733_LOG%" echo A733_RAW_PERSIST_PROBE_TRACE_SUMMARY_BEGIN
    type "%A733_TRACE_SUMMARY_OUT%" >>"%A733_LOG%"
)
echo A733_RAW_PERSIST_PROBE_DONE
>>"%A733_LOG%" echo A733_RAW_PERSIST_PROBE_DONE
ping.exe -n %A733_RESULT_HOLD_PINGS% 127.0.0.1 >nul
endlocal & exit /b %A733_PRE_MEDIA_PERSIST_RC%

:media_tiny_write_done
call :stop_trace
set "A733_TRACE_FINAL_RC=%ERRORLEVEL%"
echo.
echo --- Media tiny write diagnostic result ---
echo A733_MEDIA_TINY_WRITE_ONLY status=STOP write_rc=%A733_MEDIA_TINY_WRITE_RC% trace_rc=%A733_TRACE_FINAL_RC%
>>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_ONLY status=STOP write_rc=%A733_MEDIA_TINY_WRITE_RC% trace_rc=%A733_TRACE_FINAL_RC%
if exist "%A733_TRACE_SUMMARY_OUT%" (
    echo.
    echo --- Media tiny write SMHC ETW summary ---
    type "%A733_TRACE_SUMMARY_OUT%"
    >>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_TRACE_SUMMARY_BEGIN
    type "%A733_TRACE_SUMMARY_OUT%" >>"%A733_LOG%"
)
echo A733_MEDIA_TINY_WRITE_DONE
>>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_DONE
ping.exe -n %A733_RESULT_HOLD_PINGS% 127.0.0.1 >nul
endlocal & exit /b %A733_MEDIA_TINY_WRITE_RC%

:run_setup_async_diagnostic
echo A733_SETUP_START mode=async_diagnostic wait_seconds=%A733_SETUP_DIAGNOSTIC_WAIT_SECONDS% args=%A733_SETUP_ARGS%
>>"%A733_LOG%" echo A733_SETUP_START mode=async_diagnostic wait_seconds=%A733_SETUP_DIAGNOSTIC_WAIT_SECONDS% path=%A733_SETUP_EXE% media_drive=%A733_MEDIA_DRIVE% args=%A733_SETUP_ARGS%
    if "%A733_SKIP_SETUP_PUSHD%"=="1" (
        set "A733_SETUP_PUSHD_RC=skip"
        echo A733_SETUP_CWD_SKIP reason=config dir=%A733_SETUP_DIR%
        >>"%A733_LOG%" echo A733_SETUP_CWD_SKIP reason=config dir=%A733_SETUP_DIR%
    ) else (
        pushd "%A733_SETUP_DIR%" >nul 2>&1
        set "A733_SETUP_PUSHD_RC=!ERRORLEVEL!"
        echo A733_SETUP_CWD dir=%A733_SETUP_DIR% rc=!A733_SETUP_PUSHD_RC!
        >>"%A733_LOG%" echo A733_SETUP_CWD dir=%A733_SETUP_DIR% rc=!A733_SETUP_PUSHD_RC!
    )
    call :start_setup_watchdog
    start "" "%A733_SETUP_EXE%" %A733_SETUP_ARGS%
    set "A733_SETUP_START_RC=!ERRORLEVEL!"
    if "!A733_SETUP_PUSHD_RC!"=="0" popd
echo A733_SETUP_START_RC=%A733_SETUP_START_RC%
>>"%A733_LOG%" echo A733_SETUP_START_RC=%A733_SETUP_START_RC%
ping.exe -n %A733_SETUP_DIAGNOSTIC_WAIT_SECONDS% 127.0.0.1 >nul
echo.
echo --- Terminating Setup diagnostic processes before log replay ---
>>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC_PRELOG_KILL_BEGIN
taskkill.exe /f /t /im SetupHost.exe >>"%A733_LOG%" 2>&1
taskkill.exe /f /t /im setuphost.exe >>"%A733_LOG%" 2>&1
taskkill.exe /f /t /im SetupPrep.exe >>"%A733_LOG%" 2>&1
taskkill.exe /f /t /im setup.exe >>"%A733_LOG%" 2>&1
>>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC_PRELOG_KILL_END
ping.exe -n 4 127.0.0.1 >nul
echo.
echo --- Setup diagnostic process list ---
>>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC_PROCESS_LIST_BEGIN
tasklist.exe
tasklist.exe >>"%A733_LOG%" 2>&1
>>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC_PROCESS_LIST_END
echo.
echo --- Setup diagnostic logs ---
>>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC_LOGS_BEGIN
for %%L in (
    "%SystemRoot%\Panther\setuperr.log"
    "%SystemRoot%\Panther\setupact.log"
    "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\setuperr.log"
    "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\setupact.log"
    "%SystemDrive%\$WINDOWS.~BT\Sources\Rollback\setuperr.log"
    "%SystemDrive%\$WINDOWS.~BT\Sources\Rollback\setupact.log"
) do (
    if exist %%~fL (
        echo A733_SETUP_LOG_BEGIN path=%%~fL
        >>"%A733_LOG%" echo A733_SETUP_LOG_BEGIN path=%%~fL
        type %%~fL
        type %%~fL >>"%A733_LOG%"
        echo A733_SETUP_LOG_END path=%%~fL
        >>"%A733_LOG%" echo A733_SETUP_LOG_END path=%%~fL
    ) else (
        echo A733_SETUP_LOG_MISSING path=%%~fL
        >>"%A733_LOG%" echo A733_SETUP_LOG_MISSING path=%%~fL
    )
)
>>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC_LOGS_END
if not "%A733_DISABLE_ALL_PERSIST%"=="1" call :syncpersist "after_setup_logs"
echo.
echo --- Terminating Setup diagnostic processes ---
taskkill.exe /f /t /im SetupHost.exe >>"%A733_LOG%" 2>&1
taskkill.exe /f /t /im setuphost.exe >>"%A733_LOG%" 2>&1
taskkill.exe /f /t /im SetupPrep.exe >>"%A733_LOG%" 2>&1
taskkill.exe /f /t /im setup.exe >>"%A733_LOG%" 2>&1
echo A733_SETUP_DIAGNOSTIC_DONE
>>"%A733_LOG%" echo A733_SETUP_DIAGNOSTIC_DONE
if not "%A733_DISABLE_ALL_PERSIST%"=="1" call :syncpersist "after_setup_taskkill"
exit /b 4242

:start_setup_watchdog
del /q "%A733_SETUP_WATCHDOG_CMD%" >nul 2>&1
>>"%A733_SETUP_WATCHDOG_CMD%" echo @echo off
>>"%A733_SETUP_WATCHDOG_CMD%" echo setlocal EnableExtensions
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo A733_SETUP_WATCHDOG_BEGIN wait_seconds=%A733_SETUP_DIAGNOSTIC_WAIT_SECONDS%
>>"%A733_SETUP_WATCHDOG_CMD%" echo ping.exe -n %A733_SETUP_DIAGNOSTIC_WAIT_SECONDS% 127.0.0.1 ^>nul
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo.
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo --- A733 setup watchdog pre-kill service state ---
>>"%A733_SETUP_WATCHDOG_CMD%" echo if exist "%SystemRoot%\System32\sc.exe" sc.exe query vds
>>"%A733_SETUP_WATCHDOG_CMD%" echo if exist "%SystemRoot%\System32\sc.exe" sc.exe query a733smhc
>>"%A733_SETUP_WATCHDOG_CMD%" echo if exist "%SystemRoot%\System32\tasklist.exe" tasklist.exe
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo.
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo --- A733 setup watchdog SMHC ETW snapshot ---
>>"%A733_SETUP_WATCHDOG_CMD%" echo if exist "%A733_PKG%\A733WinPeSmhcTrace.exe" "%A733_PKG%\A733WinPeSmhcTrace.exe" stop
>>"%A733_SETUP_WATCHDOG_CMD%" echo for /L %%%%I in ^(1,1,20^) do if not exist "%A733_TRACE_DONE%" ping.exe -n 2 127.0.0.1 ^>nul
>>"%A733_SETUP_WATCHDOG_CMD%" echo if exist "%A733_TRACE_LOG%" "%A733_PKG%\A733WinPeSmhcTrace.exe" dump "%A733_TRACE_LOG%" 180
if "%A733_SKIP_TRACE_FINAL_SUMMARY%"=="1" (
    >>"%A733_SETUP_WATCHDOG_CMD%" echo echo A733_SETUP_WATCHDOG_TRACE_SUMMARY_SKIP reason=skip_trace_final_summary
) else (
    >>"%A733_SETUP_WATCHDOG_CMD%" echo if exist "%A733_TRACE_LOG%" "%A733_PKG%\A733WinPeSmhcTrace.exe" keysummary "%A733_TRACE_LOG%"
    >>"%A733_SETUP_WATCHDOG_CMD%" echo if exist "%A733_TRACE_LOG%" "%A733_PKG%\A733WinPeSmhcTrace.exe" lowargsummary "%A733_TRACE_LOG%"
)
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo.
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo --- A733 setup watchdog kill/log replay ---
>>"%A733_SETUP_WATCHDOG_CMD%" echo taskkill.exe /f /t /im SetupHost.exe
>>"%A733_SETUP_WATCHDOG_CMD%" echo taskkill.exe /f /t /im setuphost.exe
>>"%A733_SETUP_WATCHDOG_CMD%" echo taskkill.exe /f /t /im SetupPrep.exe
>>"%A733_SETUP_WATCHDOG_CMD%" echo taskkill.exe /f /t /im setup.exe
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo.
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo --- Setup diagnostic logs ---
>>"%A733_SETUP_WATCHDOG_CMD%" echo for %%%%L in ^(
>>"%A733_SETUP_WATCHDOG_CMD%" echo     "%SystemRoot%\Panther\setuperr.log"
>>"%A733_SETUP_WATCHDOG_CMD%" echo     "%SystemRoot%\Panther\setupact.log"
>>"%A733_SETUP_WATCHDOG_CMD%" echo     "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\setuperr.log"
>>"%A733_SETUP_WATCHDOG_CMD%" echo     "%SystemDrive%\$WINDOWS.~BT\Sources\Panther\setupact.log"
>>"%A733_SETUP_WATCHDOG_CMD%" echo     "%SystemDrive%\$WINDOWS.~BT\Sources\Rollback\setuperr.log"
>>"%A733_SETUP_WATCHDOG_CMD%" echo     "%SystemDrive%\$WINDOWS.~BT\Sources\Rollback\setupact.log"
>>"%A733_SETUP_WATCHDOG_CMD%" echo ^) do ^(
>>"%A733_SETUP_WATCHDOG_CMD%" echo     if exist %%%%~fL ^(
>>"%A733_SETUP_WATCHDOG_CMD%" echo         echo A733_SETUP_LOG_BEGIN path=%%%%~fL
>>"%A733_SETUP_WATCHDOG_CMD%" echo         type %%%%~fL
>>"%A733_SETUP_WATCHDOG_CMD%" echo         echo A733_SETUP_LOG_END path=%%%%~fL
>>"%A733_SETUP_WATCHDOG_CMD%" echo     ^) else ^(
>>"%A733_SETUP_WATCHDOG_CMD%" echo         echo A733_SETUP_LOG_MISSING path=%%%%~fL
>>"%A733_SETUP_WATCHDOG_CMD%" echo     ^)
>>"%A733_SETUP_WATCHDOG_CMD%" echo ^)
>>"%A733_SETUP_WATCHDOG_CMD%" echo echo A733_SETUP_WATCHDOG_DONE
>>"%A733_SETUP_WATCHDOG_CMD%" echo ping.exe -n 301 127.0.0.1 ^>nul
start "A733SetupWatchdog" cmd.exe /k "%A733_SETUP_WATCHDOG_CMD%"
exit /b 0

:assign_media_drive
set "A733_MEDIA_DRIVE="
set "A733_MEDIA_ASSIGN_METHOD=drive_scan"
set "A733_SETUP_EXE=%SystemDrive%\sources\setup.exe"
if "%A733_SKIP_MEDIA_DRIVE_SCAN%"=="1" (
    echo A733_MEDIA_DRIVE_SCAN_SKIP reason=direct_if_exist_may_hang
    >>"%A733_LOG%" echo A733_MEDIA_DRIVE_SCAN_SKIP reason=direct_if_exist_may_hang
) else (
    for %%D in (C D E F G H I J K L M N O P Q R S T U V W Y Z) do (
        if /i not "%%D:"=="%SystemDrive%" (
            if exist "%%D:\setup.exe" (
                if exist "%%D:\sources\install.wim" (
                    set "A733_MEDIA_DRIVE=%%D:"
                    set "A733_MEDIA_ASSIGN_METHOD=drive_scan"
                    set "A733_SETUP_EXE=%%D:\setup.exe"
                    goto :media_drive_found
                )
            )
        )
    )
)
call :assign_media_drive_diskpart
if not "%A733_MEDIA_DRIVE%"=="" goto :media_drive_found
call :assign_media_drive_mountvol
if not "%A733_MEDIA_DRIVE%"=="" goto :media_drive_found
call :assign_media_drive_helper
if not "%A733_MEDIA_DRIVE%"=="" goto :media_drive_found
echo A733_MEDIA_ASSIGN status=FALLBACK reason=media_root_not_found fallback=%A733_SETUP_EXE%
>>"%A733_LOG%" echo A733_MEDIA_ASSIGN status=FALLBACK reason=media_root_not_found fallback=%A733_SETUP_EXE%
exit /b 0

:media_drive_found
echo A733_MEDIA_ASSIGN status=PASS method=%A733_MEDIA_ASSIGN_METHOD% drive=%A733_MEDIA_DRIVE% setup=%A733_SETUP_EXE%
>>"%A733_LOG%" echo A733_MEDIA_ASSIGN status=PASS method=%A733_MEDIA_ASSIGN_METHOD% drive=%A733_MEDIA_DRIVE% setup=%A733_SETUP_EXE%
exit /b 0

:assign_media_drive_helper
if not exist "%A733_PKG%\A733WinPePersistLog.exe" (
    echo A733_MEDIA_HELPER_SKIP reason=missing_helper
    >>"%A733_LOG%" echo A733_MEDIA_HELPER_SKIP reason=missing_helper
    exit /b 0
)
echo A733_MEDIA_HELPER_BEGIN mode=media_mount timeout_seconds=25
>>"%A733_LOG%" echo A733_MEDIA_HELPER_BEGIN mode=media_mount timeout_seconds=25
>"%A733_MEDIA_ASSIGN_CMD%" echo @echo off
>>"%A733_MEDIA_ASSIGN_CMD%" echo set A733_PERSIST_HELPER_MODE=media_mount
>>"%A733_MEDIA_ASSIGN_CMD%" echo "%A733_PKG%\A733WinPePersistLog.exe" ^> "%A733_MEDIA_ASSIGN_OUT%" 2^>^&1
>>"%A733_MEDIA_ASSIGN_CMD%" echo set A733_MEDIA_HELPER_WRAPPER_RC=%%ERRORLEVEL%%
>>"%A733_MEDIA_ASSIGN_CMD%" echo echo A733_MEDIA_HELPER_WRAPPER_RC=%%A733_MEDIA_HELPER_WRAPPER_RC%% ^>^> "%A733_MEDIA_ASSIGN_OUT%"
>>"%A733_MEDIA_ASSIGN_CMD%" echo echo done rc=%%A733_MEDIA_HELPER_WRAPPER_RC%% ^> "%A733_MEDIA_ASSIGN_DONE%"
start "A733MediaHelper" /b cmd.exe /c "%A733_MEDIA_ASSIGN_CMD%"
set "A733_MEDIA_HELPER_START_RC=%ERRORLEVEL%"
echo A733_MEDIA_HELPER_START_RC=%A733_MEDIA_HELPER_START_RC%
>>"%A733_LOG%" echo A733_MEDIA_HELPER_START_RC=%A733_MEDIA_HELPER_START_RC%
set "A733_MEDIA_HELPER_WAIT=timeout"
for /l %%W in (1,1,25) do (
    if exist "%A733_MEDIA_ASSIGN_DONE%" (
        set "A733_MEDIA_HELPER_WAIT=%%W"
        goto :assign_media_drive_helper_after_wait
    )
    ping.exe -n 2 127.0.0.1 >nul
)
:assign_media_drive_helper_after_wait
echo A733_MEDIA_HELPER_WAIT result=%A733_MEDIA_HELPER_WAIT%
>>"%A733_LOG%" echo A733_MEDIA_HELPER_WAIT result=%A733_MEDIA_HELPER_WAIT%
set "A733_MEDIA_HELPER_RC=1460"
if exist "%A733_MEDIA_ASSIGN_DONE%" (
    type "%A733_MEDIA_ASSIGN_DONE%"
    type "%A733_MEDIA_ASSIGN_DONE%" >>"%A733_LOG%"
    for /f "tokens=3 delims== " %%R in (%A733_MEDIA_ASSIGN_DONE%) do set "A733_MEDIA_HELPER_RC=%%R"
) else (
    echo A733_MEDIA_HELPER_DONE_MISSING
    >>"%A733_LOG%" echo A733_MEDIA_HELPER_DONE_MISSING
    taskkill.exe /f /im A733WinPePersistLog.exe >>"%A733_LOG%" 2>&1
)
if exist "%A733_MEDIA_ASSIGN_OUT%" (
    type "%A733_MEDIA_ASSIGN_OUT%"
    type "%A733_MEDIA_ASSIGN_OUT%" >>"%A733_LOG%"
) else (
    echo A733_MEDIA_HELPER_OUT_MISSING
    >>"%A733_LOG%" echo A733_MEDIA_HELPER_OUT_MISSING
)
echo A733_MEDIA_HELPER_RC=%A733_MEDIA_HELPER_RC%
>>"%A733_LOG%" echo A733_MEDIA_HELPER_RC=%A733_MEDIA_HELPER_RC%
if not "%A733_MEDIA_HELPER_RC%"=="0" exit /b 0
for %%D in (T U V W Y Z) do (
    if exist "%%D:\setup.exe" (
        if exist "%%D:\sources\install.wim" (
            set "A733_MEDIA_DRIVE=%%D:"
            set "A733_MEDIA_ASSIGN_METHOD=helper"
            set "A733_SETUP_EXE=%%D:\setup.exe"
            echo A733_MEDIA_HELPER status=PASS drive=%%D:
            >>"%A733_LOG%" echo A733_MEDIA_HELPER status=PASS drive=%%D:
            exit /b 0
        )
    )
)
echo A733_MEDIA_HELPER status=MISS reason=no_setup_root_after_helper
>>"%A733_LOG%" echo A733_MEDIA_HELPER status=MISS reason=no_setup_root_after_helper
exit /b 0

:assign_media_drive_diskpart
if "%A733_SKIP_MEDIA_DISKPART_ASSIGN%"=="1" (
    echo A733_MEDIA_DISKPART_ASSIGN_SKIP reason=list_disk_known_hang
    >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_SKIP reason=list_disk_known_hang
    exit /b 0
)
if not exist "%A733_DISKPART_EXE%" (
    echo A733_MEDIA_DISKPART_ASSIGN_SKIP reason=diskpart_missing
    >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_SKIP reason=diskpart_missing
    exit /b 0
)
if exist "T:\setup.exe" (
    if exist "T:\sources\install.wim" (
        set "A733_MEDIA_DRIVE=T:"
        set "A733_MEDIA_ASSIGN_METHOD=diskpart_preexisting"
        set "A733_SETUP_EXE=T:\setup.exe"
        echo A733_MEDIA_DISKPART_ASSIGN status=PASS reason=preexisting drive=T:
        >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN status=PASS reason=preexisting drive=T:
        exit /b 0
    )
)
echo A733_MEDIA_DISKPART_ASSIGN_BEGIN disk=0 partition=2 drive=T: mode=minimal_list_only
>>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_BEGIN disk=0 partition=2 drive=T: mode=minimal_list_only
>"%A733_DISKPART_ASSIGN_SCRIPT%" echo list disk
>>"%A733_DISKPART_ASSIGN_SCRIPT%" echo select disk 0
>>"%A733_DISKPART_ASSIGN_SCRIPT%" echo detail disk
>>"%A733_DISKPART_ASSIGN_SCRIPT%" echo list partition
>>"%A733_DISKPART_ASSIGN_SCRIPT%" echo list volume
>>"%A733_DISKPART_ASSIGN_SCRIPT%" echo exit
type "%A733_DISKPART_ASSIGN_SCRIPT%"
type "%A733_DISKPART_ASSIGN_SCRIPT%" >>"%A733_LOG%"
>"%A733_DISKPART_ASSIGN_CMD%" echo @echo off
>>"%A733_DISKPART_ASSIGN_CMD%" echo "%A733_DISKPART_EXE%" /s "%A733_DISKPART_ASSIGN_SCRIPT%" ^> "%A733_DISKPART_ASSIGN_OUT%" 2^>^&1
>>"%A733_DISKPART_ASSIGN_CMD%" echo set A733_DISKPART_ASSIGN_WRAPPER_RC=%%ERRORLEVEL%%
>>"%A733_DISKPART_ASSIGN_CMD%" echo echo A733_DISKPART_ASSIGN_WRAPPER_RC=%%A733_DISKPART_ASSIGN_WRAPPER_RC%% ^>^> "%A733_DISKPART_ASSIGN_OUT%"
>>"%A733_DISKPART_ASSIGN_CMD%" echo echo done rc=%%A733_DISKPART_ASSIGN_WRAPPER_RC%% ^> "%A733_DISKPART_ASSIGN_DONE%"
type "%A733_DISKPART_ASSIGN_CMD%"
type "%A733_DISKPART_ASSIGN_CMD%" >>"%A733_LOG%"
start "A733DiskpartAssign" /b cmd.exe /c "%A733_DISKPART_ASSIGN_CMD%"
set "A733_MEDIA_DISKPART_ASSIGN_START_RC=%ERRORLEVEL%"
echo A733_MEDIA_DISKPART_ASSIGN_START_RC=%A733_MEDIA_DISKPART_ASSIGN_START_RC%
>>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_START_RC=%A733_MEDIA_DISKPART_ASSIGN_START_RC%
set "A733_MEDIA_DISKPART_ASSIGN_WAIT=timeout"
for /l %%W in (1,1,90) do (
    if exist "%A733_DISKPART_ASSIGN_DONE%" (
        set "A733_MEDIA_DISKPART_ASSIGN_WAIT=%%W"
        goto :assign_media_drive_diskpart_after_wait
    )
    ping.exe -n 2 127.0.0.1 >nul
)
:assign_media_drive_diskpart_after_wait
echo A733_MEDIA_DISKPART_ASSIGN_WAIT result=%A733_MEDIA_DISKPART_ASSIGN_WAIT%
>>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_WAIT result=%A733_MEDIA_DISKPART_ASSIGN_WAIT%
if exist "%A733_DISKPART_ASSIGN_DONE%" (
    type "%A733_DISKPART_ASSIGN_DONE%"
    type "%A733_DISKPART_ASSIGN_DONE%" >>"%A733_LOG%"
) else (
    echo A733_MEDIA_DISKPART_ASSIGN_DONE_MISSING
    >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_DONE_MISSING
    taskkill.exe /f /im diskpart.exe >>"%A733_LOG%" 2>&1
    ping.exe -n 3 127.0.0.1 >nul
)
if exist "%A733_DISKPART_ASSIGN_OUT%" (
    echo A733_MEDIA_DISKPART_ASSIGN_OUT_BEGIN
    >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_OUT_BEGIN
    type "%A733_DISKPART_ASSIGN_OUT%"
    type "%A733_DISKPART_ASSIGN_OUT%" >>"%A733_LOG%"
    echo A733_MEDIA_DISKPART_ASSIGN_OUT_END
    >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_OUT_END
) else (
    echo A733_MEDIA_DISKPART_ASSIGN_OUT_MISSING
    >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN_OUT_MISSING
)
if exist "T:\setup.exe" (
    if exist "T:\sources\install.wim" (
        set "A733_MEDIA_DRIVE=T:"
        set "A733_MEDIA_ASSIGN_METHOD=diskpart_assign"
        set "A733_SETUP_EXE=T:\setup.exe"
        echo A733_MEDIA_DISKPART_ASSIGN status=PASS drive=T:
        >>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN status=PASS drive=T:
        exit /b 0
    )
)
echo A733_MEDIA_DISKPART_ASSIGN status=MISS drive=T:
>>"%A733_LOG%" echo A733_MEDIA_DISKPART_ASSIGN status=MISS drive=T:
exit /b 0

:assign_media_drive_mountvol
set "A733_MEDIA_MOUNTVOL_DRIVE=T:"
set "A733_MEDIA_MOUNTVOL_POINT=%A733_MEDIA_MOUNTVOL_DRIVE%\"
if exist "%A733_MEDIA_MOUNTVOL_POINT%" (
    echo A733_MEDIA_MOUNTVOL_SKIP reason=temp_drive_exists drive=%A733_MEDIA_MOUNTVOL_DRIVE%
    >>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL_SKIP reason=temp_drive_exists drive=%A733_MEDIA_MOUNTVOL_DRIVE%
    exit /b 0
)
echo A733_MEDIA_MOUNTVOL_BEGIN drive=%A733_MEDIA_MOUNTVOL_DRIVE%
>>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL_BEGIN drive=%A733_MEDIA_MOUNTVOL_DRIVE%
if exist "%SystemRoot%\System32\sc.exe" (
    sc.exe start vds >"%A733_TMP%" 2>&1
    set "A733_MEDIA_VDS_START_RC=%ERRORLEVEL%"
) else (
    >"%A733_TMP%" echo A733_MEDIA_VDS_START_SKIP reason=sc_missing
    set "A733_MEDIA_VDS_START_RC=9009"
)
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_MEDIA_VDS_START_RC=%A733_MEDIA_VDS_START_RC%
>>"%A733_LOG%" echo A733_MEDIA_VDS_START_RC=%A733_MEDIA_VDS_START_RC%
ping.exe -n 4 127.0.0.1 >nul
mountvol.exe /E >"%A733_TMP%" 2>&1
set "A733_MEDIA_MOUNTVOL_ENABLE_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_MEDIA_MOUNTVOL_ENABLE_RC=%A733_MEDIA_MOUNTVOL_ENABLE_RC%
>>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL_ENABLE_RC=%A733_MEDIA_MOUNTVOL_ENABLE_RC%
mountvol.exe "%A733_MEDIA_MOUNTVOL_POINT%" /d >nul 2>&1
mountvol.exe >"%A733_MEDIA_MOUNT_OUT%" 2>&1
set "A733_MOUNTVOL_LIST_RC=%ERRORLEVEL%"
type "%A733_MEDIA_MOUNT_OUT%"
type "%A733_MEDIA_MOUNT_OUT%" >>"%A733_LOG%"
echo A733_MEDIA_MOUNTVOL_LIST_RC=%A733_MOUNTVOL_LIST_RC%
>>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL_LIST_RC=%A733_MOUNTVOL_LIST_RC%
if not "%A733_MOUNTVOL_LIST_RC%"=="0" exit /b 0
for /f "tokens=1" %%V in (%A733_MEDIA_MOUNT_OUT%) do (
    call :try_media_mountvol_token "%%V"
    if not "!A733_MEDIA_DRIVE!"=="" exit /b 0
)
echo A733_MEDIA_MOUNTVOL_END status=MISS
>>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL_END status=MISS
exit /b 0

:try_media_mountvol_token
set "A733_MEDIA_VOLUME_TOKEN=%~1"
if "%A733_MEDIA_VOLUME_TOKEN:~0,11%"=="\\?\Volume{" call :try_media_mountvol "%A733_MEDIA_VOLUME_TOKEN%"
exit /b 0

:try_media_mountvol
set "A733_MEDIA_VOLUME=%~1"
echo A733_MEDIA_MOUNTVOL_TRY volume=%A733_MEDIA_VOLUME% drive=%A733_MEDIA_MOUNTVOL_DRIVE%
>>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL_TRY volume=%A733_MEDIA_VOLUME% drive=%A733_MEDIA_MOUNTVOL_DRIVE%
mountvol.exe "%A733_MEDIA_MOUNTVOL_POINT%" "%A733_MEDIA_VOLUME%" >"%A733_TMP%" 2>&1
set "A733_MOUNTVOL_ASSIGN_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_MEDIA_MOUNTVOL_ASSIGN_RC=%A733_MOUNTVOL_ASSIGN_RC%
>>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL_ASSIGN_RC=%A733_MOUNTVOL_ASSIGN_RC%
if "%A733_MOUNTVOL_ASSIGN_RC%"=="0" (
    if exist "%A733_MEDIA_MOUNTVOL_DRIVE%\setup.exe" (
        if exist "%A733_MEDIA_MOUNTVOL_DRIVE%\sources\install.wim" (
            set "A733_MEDIA_DRIVE=%A733_MEDIA_MOUNTVOL_DRIVE%"
            set "A733_MEDIA_ASSIGN_METHOD=mountvol"
            set "A733_SETUP_EXE=%A733_MEDIA_MOUNTVOL_DRIVE%\setup.exe"
            echo A733_MEDIA_MOUNTVOL status=PASS volume=%A733_MEDIA_VOLUME% drive=%A733_MEDIA_DRIVE%
            >>"%A733_LOG%" echo A733_MEDIA_MOUNTVOL status=PASS volume=%A733_MEDIA_VOLUME% drive=%A733_MEDIA_DRIVE%
            exit /b 0
        )
    )
)
mountvol.exe "%A733_MEDIA_MOUNTVOL_POINT%" /d >nul 2>&1
exit /b 0

:media_tiny_write_probe
if "%A733_MEDIA_DRIVE%"=="" (
    echo A733_MEDIA_TINY_WRITE_SKIP reason=no_media_drive
    >>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_SKIP reason=no_media_drive
    exit /b 2
)
set "A733_MEDIA_TINY_FILE=%A733_MEDIA_DRIVE%\A733H206.TMP"
echo A733_MEDIA_TINY_WRITE_BEGIN path=%A733_MEDIA_TINY_FILE%
>>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_BEGIN path=%A733_MEDIA_TINY_FILE%
>"%A733_MEDIA_TINY_FILE%" echo A733_MEDIA_TINY_WRITE payload=h201
set "A733_MEDIA_TINY_CREATE_RC=%ERRORLEVEL%"
echo A733_MEDIA_TINY_WRITE_CREATE_RC=%A733_MEDIA_TINY_CREATE_RC%
>>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_CREATE_RC=%A733_MEDIA_TINY_CREATE_RC%
if not "%A733_MEDIA_TINY_CREATE_RC%"=="0" exit /b %A733_MEDIA_TINY_CREATE_RC%
type "%A733_MEDIA_TINY_FILE%" >"%A733_TMP%" 2>&1
set "A733_MEDIA_TINY_READ_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_MEDIA_TINY_WRITE_READ_RC=%A733_MEDIA_TINY_READ_RC%
>>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_READ_RC=%A733_MEDIA_TINY_READ_RC%
set "A733_MEDIA_TINY_DELETE_RC=0"
echo A733_MEDIA_TINY_WRITE_KEEP path=%A733_MEDIA_TINY_FILE% >"%A733_TMP%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_MEDIA_TINY_WRITE_DELETE_RC=%A733_MEDIA_TINY_DELETE_RC%
>>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE_DELETE_RC=%A733_MEDIA_TINY_DELETE_RC%
if not "%A733_MEDIA_TINY_READ_RC%"=="0" exit /b %A733_MEDIA_TINY_READ_RC%
if not "%A733_MEDIA_TINY_DELETE_RC%"=="0" exit /b %A733_MEDIA_TINY_DELETE_RC%
echo A733_MEDIA_TINY_WRITE status=PASS
>>"%A733_LOG%" echo A733_MEDIA_TINY_WRITE status=PASS
exit /b 0

:stage_setup_from_x_sources
echo A733_SETUP_STAGE_BEGIN dir=%A733_SETUP_STAGE_DIR%
>>"%A733_LOG%" echo A733_SETUP_STAGE_BEGIN dir=%A733_SETUP_STAGE_DIR%
if not exist "%A733_SETUP_STAGE_DIR%" md "%A733_SETUP_STAGE_DIR%"
if errorlevel 1 exit /b 20
copy /y "%SystemDrive%\sources\*.*" "%A733_SETUP_STAGE_DIR%\" >"%A733_TMP%" 2>&1
set "A733_SETUP_STAGE_COPY_X_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_SETUP_STAGE_COPY_X rc=%A733_SETUP_STAGE_COPY_X_RC%
if not "%A733_SETUP_STAGE_COPY_X_RC%"=="0" exit /b %A733_SETUP_STAGE_COPY_X_RC%
attrib.exe -R -S -H "%A733_SETUP_STAGE_DIR%\*.*" >nul 2>&1
set "A733_WINSXS_COMPONENT="
for /d %%G in ("%SystemRoot%\WinSxS\arm64_microsoft-windows-i..sedsetup-media-base_*") do set "A733_WINSXS_COMPONENT=%%~fG"
if "!A733_WINSXS_COMPONENT!"=="" (
    echo A733_SETUP_STAGE_COMPONENT_MISSING pattern=sedsetup-media-base
    >>"%A733_LOG%" echo A733_SETUP_STAGE_COMPONENT_MISSING pattern=sedsetup-media-base
    exit /b 22
)
attrib.exe -R -S -H "%A733_SETUP_STAGE_DIR%\*.*" >nul 2>&1
copy /y "!A733_WINSXS_COMPONENT!\*.*" "%A733_SETUP_STAGE_DIR%\" >"%A733_TMP%" 2>&1
set "A733_SETUP_STAGE_COMPONENT_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_SETUP_STAGE_COMPONENT pattern=sedsetup-media-base rc=%A733_SETUP_STAGE_COMPONENT_RC% path=!A733_WINSXS_COMPONENT!
if not "%A733_SETUP_STAGE_COMPONENT_RC%"=="0" exit /b %A733_SETUP_STAGE_COMPONENT_RC%
attrib.exe -R -S -H "%A733_SETUP_STAGE_DIR%\*.*" >nul 2>&1
set "A733_WINSXS_COMPONENT="
for /d %%G in ("%SystemRoot%\WinSxS\arm64_microsoft-windows-s..platform-media-base_*") do set "A733_WINSXS_COMPONENT=%%~fG"
if "!A733_WINSXS_COMPONENT!"=="" (
    echo A733_SETUP_STAGE_COMPONENT_MISSING pattern=platform-media-base
    >>"%A733_LOG%" echo A733_SETUP_STAGE_COMPONENT_MISSING pattern=platform-media-base
    exit /b 23
)
copy /y "!A733_WINSXS_COMPONENT!\*.*" "%A733_SETUP_STAGE_DIR%\" >"%A733_TMP%" 2>&1
set "A733_SETUP_STAGE_COMPONENT_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_SETUP_STAGE_COMPONENT pattern=platform-media-base rc=%A733_SETUP_STAGE_COMPONENT_RC% path=!A733_WINSXS_COMPONENT!
if not "%A733_SETUP_STAGE_COMPONENT_RC%"=="0" exit /b %A733_SETUP_STAGE_COMPONENT_RC%
attrib.exe -R -S -H "%A733_SETUP_STAGE_DIR%\*.*" >nul 2>&1
set "A733_WINSXS_COMPONENT="
for /d %%G in ("%SystemRoot%\WinSxS\arm64_microsoft-windows-s..tform-media-onecore_*") do set "A733_WINSXS_COMPONENT=%%~fG"
if "!A733_WINSXS_COMPONENT!"=="" (
    echo A733_SETUP_STAGE_COMPONENT_MISSING pattern=platform-media-onecore
    >>"%A733_LOG%" echo A733_SETUP_STAGE_COMPONENT_MISSING pattern=platform-media-onecore
    exit /b 24
)
copy /y "!A733_WINSXS_COMPONENT!\*.*" "%A733_SETUP_STAGE_DIR%\" >"%A733_TMP%" 2>&1
set "A733_SETUP_STAGE_COMPONENT_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_SETUP_STAGE_COMPONENT pattern=platform-media-onecore rc=%A733_SETUP_STAGE_COMPONENT_RC% path=!A733_WINSXS_COMPONENT!
if not "%A733_SETUP_STAGE_COMPONENT_RC%"=="0" exit /b %A733_SETUP_STAGE_COMPONENT_RC%
attrib.exe -R -S -H "%A733_SETUP_STAGE_DIR%\*.*" >nul 2>&1
if not exist "%A733_SETUP_STAGE_DIR%\setup.exe" exit /b 21
echo A733_SETUP_STAGE_END status=PASS exe=%A733_SETUP_STAGE_DIR%\setup.exe
>>"%A733_LOG%" echo A733_SETUP_STAGE_END status=PASS exe=%A733_SETUP_STAGE_DIR%\setup.exe
exit /b 0

:overlay_x_sources_minimal
echo A733_X_SOURCES_MIN_OVERLAY_BEGIN to=%SystemDrive%\sources
>>"%A733_LOG%" echo A733_X_SOURCES_MIN_OVERLAY_BEGIN to=%SystemDrive%\sources
if not exist "%SystemDrive%\sources\setup.exe" exit /b 41
attrib.exe -R -S -H "%SystemDrive%\sources\*.*" >nul 2>&1

set "A733_WINSXS_COMPONENT="
for /d %%G in ("%SystemRoot%\WinSxS\arm64_microsoft-windows-i..sedsetup-media-base_*") do set "A733_WINSXS_COMPONENT=%%~fG"
if "!A733_WINSXS_COMPONENT!"=="" exit /b 42
copy /y "!A733_WINSXS_COMPONENT!\*.*" "%SystemDrive%\sources\" >"%A733_TMP%" 2>&1
set "A733_X_SOURCES_MIN_COPY_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_X_SOURCES_MIN_COMPONENT pattern=sedsetup-media-base rc=%A733_X_SOURCES_MIN_COPY_RC% path=!A733_WINSXS_COMPONENT!
if not "%A733_X_SOURCES_MIN_COPY_RC%"=="0" exit /b %A733_X_SOURCES_MIN_COPY_RC%

set "A733_WINSXS_COMPONENT="
for /d %%G in ("%SystemRoot%\WinSxS\arm64_microsoft-windows-s..platform-media-base_*") do set "A733_WINSXS_COMPONENT=%%~fG"
if "!A733_WINSXS_COMPONENT!"=="" exit /b 43
copy /y "!A733_WINSXS_COMPONENT!\*.*" "%SystemDrive%\sources\" >"%A733_TMP%" 2>&1
set "A733_X_SOURCES_MIN_COPY_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_X_SOURCES_MIN_COMPONENT pattern=platform-media-base rc=%A733_X_SOURCES_MIN_COPY_RC% path=!A733_WINSXS_COMPONENT!
if not "%A733_X_SOURCES_MIN_COPY_RC%"=="0" exit /b %A733_X_SOURCES_MIN_COPY_RC%

set "A733_WINSXS_COMPONENT="
for /d %%G in ("%SystemRoot%\WinSxS\arm64_microsoft-windows-s..tform-media-onecore_*") do set "A733_WINSXS_COMPONENT=%%~fG"
if "!A733_WINSXS_COMPONENT!"=="" exit /b 44
copy /y "!A733_WINSXS_COMPONENT!\*.*" "%SystemDrive%\sources\" >"%A733_TMP%" 2>&1
set "A733_X_SOURCES_MIN_COPY_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_X_SOURCES_MIN_COMPONENT pattern=platform-media-onecore rc=%A733_X_SOURCES_MIN_COPY_RC% path=!A733_WINSXS_COMPONENT!
if not "%A733_X_SOURCES_MIN_COPY_RC%"=="0" exit /b %A733_X_SOURCES_MIN_COPY_RC%

set "A733_WINSXS_COMPONENT="
for /d %%G in ("%SystemRoot%\WinSxS\arm64_microsoft-windows-i..setup-media-onecore_*") do set "A733_WINSXS_COMPONENT=%%~fG"
if not "!A733_WINSXS_COMPONENT!"=="" (
    if exist "!A733_WINSXS_COMPONENT!\imagelib.dll" (
        copy /y "!A733_WINSXS_COMPONENT!\imagelib.dll" "%SystemDrive%\sources\" >"%A733_TMP%" 2>&1
        set "A733_X_SOURCES_MIN_COPY_RC=!ERRORLEVEL!"
        type "%A733_TMP%"
        type "%A733_TMP%" >>"%A733_LOG%"
        >>"%A733_LOG%" echo A733_X_SOURCES_MIN_FILE file=imagelib.dll rc=!A733_X_SOURCES_MIN_COPY_RC! path=!A733_WINSXS_COMPONENT!
        if not "!A733_X_SOURCES_MIN_COPY_RC!"=="0" exit /b !A733_X_SOURCES_MIN_COPY_RC!
    )
)

attrib.exe -R -S -H "%SystemDrive%\sources\*.*" >nul 2>&1
if not exist "%SystemDrive%\sources\wdsclient.dll" exit /b 45
if not exist "%SystemDrive%\sources\wimgapi.dll" exit /b 46
if not exist "%SystemDrive%\sources\imagelib.dll" exit /b 47
echo A733_X_SOURCES_MIN_OVERLAY_END status=PASS setup=%SystemDrive%\sources\setup.exe
>>"%A733_LOG%" echo A733_X_SOURCES_MIN_OVERLAY_END status=PASS setup=%SystemDrive%\sources\setup.exe
exit /b 0

:overlay_x_sources_from_stage
echo A733_X_SOURCES_OVERLAY_BEGIN from=%A733_SETUP_STAGE_DIR% to=%SystemDrive%\sources
>>"%A733_LOG%" echo A733_X_SOURCES_OVERLAY_BEGIN from=%A733_SETUP_STAGE_DIR% to=%SystemDrive%\sources
if not exist "%SystemDrive%\sources\setup.exe" exit /b 31
if not exist "%A733_SETUP_STAGE_DIR%\setup.exe" exit /b 32
attrib.exe -R -S -H "%SystemDrive%\sources\*.*" >nul 2>&1
copy /y "%A733_SETUP_STAGE_DIR%\*.*" "%SystemDrive%\sources\" >"%A733_TMP%" 2>&1
set "A733_X_SOURCES_OVERLAY_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_X_SOURCES_OVERLAY_COPY rc=%A733_X_SOURCES_OVERLAY_RC%
if not "%A733_X_SOURCES_OVERLAY_RC%"=="0" exit /b %A733_X_SOURCES_OVERLAY_RC%
attrib.exe -R -S -H "%SystemDrive%\sources\*.*" >nul 2>&1
if not exist "%SystemDrive%\sources\wdsclient.dll" exit /b 33
if not exist "%SystemDrive%\sources\winsetup.dll" exit /b 34
echo A733_X_SOURCES_OVERLAY_END status=PASS setup=%SystemDrive%\sources\setup.exe
>>"%A733_LOG%" echo A733_X_SOURCES_OVERLAY_END status=PASS setup=%SystemDrive%\sources\setup.exe
exit /b 0

:init_persistent_log
set "A733_PERSIST_LOG="
set "A733_PERSIST_DRIVE="
if "%A733_DISABLE_ALL_PERSIST%"=="1" (
    echo A733_PERSIST_INIT_SKIP reason=disabled_for_media_root_setup_probe
    >>"%A733_LOG%" echo A733_PERSIST_INIT_SKIP reason=disabled_for_media_root_setup_probe
    exit /b 0
)
echo A733_PERSIST_INIT_BEGIN
>>"%A733_LOG%" echo A733_PERSIST_INIT_BEGIN
mountvol.exe S: /s >"%A733_TMP%" 2>&1
set "A733_MOUNTVOL_S_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_MOUNTVOL drive=S rc=%A733_MOUNTVOL_S_RC%
if exist S:\EFI\BOOT\BOOTAA64.EFI (
    set "A733_PERSIST_DRIVE=S:"
    set "A733_PERSIST_LOG=S:\A733DIAG.LOG"
    goto :persist_found
)
for %%D in (C D E F G H I J K L M N O P Q R T U V W Y Z) do (
    if /i not "%%D:"=="%SystemDrive%" (
        if exist "%%D:\EFI\BOOT\BOOTAA64.EFI" (
            set "A733_PERSIST_DRIVE=%%D:"
            set "A733_PERSIST_LOG=%%D:\A733DIAG.LOG"
            goto :persist_found
        )
    )
)
echo A733_PERSIST_INIT status=MISS
>>"%A733_LOG%" echo A733_PERSIST_INIT status=MISS
exit /b 1

:persist_found
echo A733_PERSIST_INIT status=PASS drive=%A733_PERSIST_DRIVE% path=%A733_PERSIST_LOG%
>>"%A733_LOG%" echo A733_PERSIST_INIT status=PASS drive=%A733_PERSIST_DRIVE% path=%A733_PERSIST_LOG%
exit /b 0

:syncpersist
set "A733_PERSIST_SYNC_RC=1"
>>"%A733_LOG%" echo A733_PERSIST_SYNC_BEGIN reason=%~1 path=%A733_PERSIST_LOG%
if "%A733_DISABLE_ALL_PERSIST%"=="1" (
    >>"%A733_LOG%" echo A733_PERSIST_SYNC_SKIP reason=disabled_for_media_root_setup_probe
    exit /b 0
)
if "%A733_PERSIST_LOG%"=="" goto :sync_persistent_helper
copy /y "%A733_LOG%" "%A733_PERSIST_LOG%" >"%A733_TMP%" 2>&1
set "A733_PERSIST_COPY_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_PERSIST_COPY_END reason=%~1 rc=%A733_PERSIST_COPY_RC%
set "A733_PERSIST_SYNC_RC=%A733_PERSIST_COPY_RC%"
if "%A733_PERSIST_COPY_RC%"=="0" copy /y "%A733_LOG%" "%A733_PERSIST_LOG%" >nul 2>&1

:sync_persistent_helper
if "%A733_SKIP_PERSIST_HELPER%"=="1" (
    >>"%A733_LOG%" echo A733_PERSIST_HELPER_SKIP reason=disabled_for_setup_media_drive_probe
    goto :sync_persistent_done
)
if not exist "%A733_PKG%\A733WinPePersistLog.exe" goto :sync_persistent_done
"%A733_PKG%\A733WinPePersistLog.exe" >"%A733_TMP%" 2>&1
set "A733_PERSIST_HELPER_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
>>"%A733_LOG%" echo A733_PERSIST_HELPER_END reason=%~1 rc=%A733_PERSIST_HELPER_RC%
if "%A733_PERSIST_HELPER_RC%"=="0" (
    set "A733_PERSIST_SYNC_RC=0"
    "%A733_PKG%\A733WinPePersistLog.exe" >nul 2>&1
)

:sync_persistent_done
>>"%A733_LOG%" echo A733_PERSIST_SYNC_END reason=%~1 rc=%A733_PERSIST_SYNC_RC%
exit /b %A733_PERSIST_SYNC_RC%

:setup_missing
call :stop_trace
echo Setup executable not found; holding the diagnostic result.
>>"%A733_LOG%" echo A733_SETUP_START status=FAIL reason=missing
ping.exe -n %A733_RESULT_HOLD_PINGS% 127.0.0.1 >nul
endlocal & exit /b 2

:setup_stage_failed
call :stop_trace
echo Setup staging failed; holding the diagnostic result.
>>"%A733_LOG%" echo A733_SETUP_START status=FAIL reason=stage_failed rc=%A733_STAGE_SETUP_RC%
ping.exe -n %A733_RESULT_HOLD_PINGS% 127.0.0.1 >nul
endlocal & exit /b %A733_STAGE_SETUP_RC%

:hold_failure
echo Probe failed; Windows Setup is not started.
ping.exe -n %A733_RESULT_HOLD_PINGS% 127.0.0.1 >nul
endlocal & exit /b %A733_RESULT%

:run_diskpart_smoke
echo.
echo --- Pre-Setup diskpart phased smoke probe ---
echo A733_DISKPART_SMOKE_BEGIN reason=pre_setup_diskinfo_frontier
>>"%A733_LOG%" echo A733_DISKPART_SMOKE_BEGIN reason=pre_setup_diskinfo_frontier
>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_SMOKE_BEGIN reason=pre_setup_diskinfo_frontier

if exist "%A733_DISKPART_EXE%" (
    set "A733_DISKPART_PATH_STATUS=present"
) else (
    set "A733_DISKPART_PATH_STATUS=missing"
)
echo A733_DISKPART_PATH path=%A733_DISKPART_EXE% status=%A733_DISKPART_PATH_STATUS%
>>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_PATH path=%A733_DISKPART_EXE% status=%A733_DISKPART_PATH_STATUS%
>>"%A733_LOG%" echo A733_DISKPART_PATH path=%A733_DISKPART_EXE% status=%A733_DISKPART_PATH_STATUS%
if not "%A733_DISKPART_PATH_STATUS%"=="present" (
    echo A733_DISKPART_SMOKE status=FAIL reason=diskpart_missing
    >>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_SMOKE status=FAIL reason=diskpart_missing
    >>"%A733_LOG%" echo A733_DISKPART_SMOKE status=FAIL reason=diskpart_missing
    exit /b 9009
)

echo.
echo --- diskpart help ---
>>"%A733_VDS_SMOKE_OUT%" echo --- diskpart help ---
"%A733_DISKPART_EXE%" /? >"%A733_TMP%" 2>&1
set "A733_DISKPART_HELP_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_VDS_SMOKE_OUT%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_DISKPART_HELP_RC=%A733_DISKPART_HELP_RC%
>>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_HELP_RC=%A733_DISKPART_HELP_RC%
>>"%A733_LOG%" echo A733_DISKPART_HELP_RC=%A733_DISKPART_HELP_RC%

>"%A733_DISKPART_EXIT_SCRIPT%" echo exit
echo.
echo --- diskpart exit-only ---
>>"%A733_VDS_SMOKE_OUT%" echo --- diskpart exit-only ---
"%A733_DISKPART_EXE%" /s "%A733_DISKPART_EXIT_SCRIPT%" >"%A733_TMP%" 2>&1
set "A733_DISKPART_EXIT_RC=%ERRORLEVEL%"
set "A733_DISKPART_RC=%A733_DISKPART_EXIT_RC%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_VDS_SMOKE_OUT%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_DISKPART_EXIT_RC=%A733_DISKPART_EXIT_RC%
>>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_EXIT_RC=%A733_DISKPART_EXIT_RC%
>>"%A733_LOG%" echo A733_DISKPART_EXIT_RC=%A733_DISKPART_EXIT_RC%

if not "%A733_DISKPART_EXIT_RC%"=="0" (
    echo A733_DISKPART_SMOKE status=FAIL reason=exit_only_rc rc=%A733_DISKPART_EXIT_RC%
    >>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_SMOKE status=FAIL reason=exit_only_rc rc=%A733_DISKPART_EXIT_RC%
    >>"%A733_LOG%" echo A733_DISKPART_SMOKE status=FAIL reason=exit_only_rc rc=%A733_DISKPART_EXIT_RC%
    exit /b %A733_DISKPART_EXIT_RC%
)

>"%A733_DISKPART_LIST_SCRIPT%" echo list disk
>>"%A733_DISKPART_LIST_SCRIPT%" echo exit
echo.
echo --- diskpart list disk ---
>>"%A733_VDS_SMOKE_OUT%" echo --- diskpart list disk ---
"%A733_DISKPART_EXE%" /s "%A733_DISKPART_LIST_SCRIPT%" >"%A733_TMP%" 2>&1
set "A733_DISKPART_LIST_RC=%ERRORLEVEL%"
set "A733_DISKPART_RC=%A733_DISKPART_LIST_RC%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_VDS_SMOKE_OUT%"
type "%A733_TMP%" >>"%A733_LOG%"
echo A733_DISKPART_LIST_RC=%A733_DISKPART_LIST_RC%
>>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_LIST_RC=%A733_DISKPART_LIST_RC%
>>"%A733_LOG%" echo A733_DISKPART_LIST_RC=%A733_DISKPART_LIST_RC%

if not "%A733_DISKPART_LIST_RC%"=="0" (
    echo A733_DISKPART_SMOKE status=FAIL reason=list_disk_rc rc=%A733_DISKPART_LIST_RC%
    >>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_SMOKE status=FAIL reason=list_disk_rc rc=%A733_DISKPART_LIST_RC%
    >>"%A733_LOG%" echo A733_DISKPART_SMOKE status=FAIL reason=list_disk_rc rc=%A733_DISKPART_LIST_RC%
    exit /b %A733_DISKPART_LIST_RC%
)
echo A733_DISKPART_SMOKE status=PASS
>>"%A733_VDS_SMOKE_OUT%" echo A733_DISKPART_SMOKE status=PASS
>>"%A733_LOG%" echo A733_DISKPART_SMOKE status=PASS
exit /b 0

:stop_trace
if not "%A733_TRACE_STARTED%"=="1" exit /b 0
set "A733_TRACE_FAIL_RC=0"
"%A733_PKG%\A733WinPeSmhcTrace.exe" stop >"%A733_TMP%" 2>&1
set "A733_TRACE_STOP_RC=%ERRORLEVEL%"
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
for /L %%I in (1,1,50) do (
    if exist "%A733_TRACE_DONE%" goto :trace_done
    ping.exe -n 2 127.0.0.1 >nul
)
echo A733_TRACE_FLUSH status=FAIL reason=done_timeout stop_rc=%A733_TRACE_STOP_RC%
>>"%A733_LOG%" echo A733_TRACE_FLUSH status=FAIL reason=done_timeout stop_rc=%A733_TRACE_STOP_RC%
set "A733_TRACE_STARTED=0"
if not "%A733_TRACE_STOP_RC%"=="0" exit /b %A733_TRACE_STOP_RC%
exit /b 1460

:trace_done
if not "%A733_TRACE_STOP_RC%"=="0" (
    echo A733_TRACE_VALIDATE status=FAIL reason=stop_rc rc=%A733_TRACE_STOP_RC%
    >>"%A733_LOG%" echo A733_TRACE_VALIDATE status=FAIL reason=stop_rc rc=%A733_TRACE_STOP_RC%
    if "%A733_TRACE_FAIL_RC%"=="0" set "A733_TRACE_FAIL_RC=%A733_TRACE_STOP_RC%"
)
for /L %%I in (1,1,10) do (
    "%A733_PKG%\A733WinPeSmhcTrace.exe" verify "%A733_TRACE_LOG%" "%A733_TRACE_DONE%" >"%A733_TMP%" 2>&1
    if not errorlevel 1 goto :trace_verified
    ping.exe -n 2 127.0.0.1 >nul
)
"%A733_PKG%\A733WinPeSmhcTrace.exe" verify "%A733_TRACE_LOG%" "%A733_TRACE_DONE%" >"%A733_TMP%" 2>&1
set "A733_TRACE_VERIFY_RC=%ERRORLEVEL%"
goto :trace_verify_complete

:trace_verified
set "A733_TRACE_VERIFY_RC=0"

:trace_verify_complete
type "%A733_TMP%"
type "%A733_TMP%" >>"%A733_LOG%"
if not "%A733_TRACE_VERIFY_RC%"=="0" (
    echo A733_TRACE_VALIDATE status=FAIL reason=verify_rc rc=%A733_TRACE_VERIFY_RC%
    >>"%A733_LOG%" echo A733_TRACE_VALIDATE status=FAIL reason=verify_rc rc=%A733_TRACE_VERIFY_RC%
    if "%A733_TRACE_FAIL_RC%"=="0" set "A733_TRACE_FAIL_RC=%A733_TRACE_VERIFY_RC%"
)
echo.
echo --- A733 SMHC ETW tail ---
if "%A733_DISK_PUBLICATION_ONLY%"=="1" (
    "%A733_PKG%\A733WinPeSmhcTrace.exe" dump "%A733_TRACE_LOG%" 80
) else if "%A733_STOP_AFTER_RAW_PERSIST_PROBE%"=="1" (
    "%A733_PKG%\A733WinPeSmhcTrace.exe" dump "%A733_TRACE_LOG%" 80
) else (
    "%A733_PKG%\A733WinPeSmhcTrace.exe" dump "%A733_TRACE_LOG%" 240
)
set "A733_TRACE_DUMP_RC=%ERRORLEVEL%"
if not "%A733_TRACE_DUMP_RC%"=="0" (
    echo A733_TRACE_VALIDATE status=FAIL reason=dump_rc rc=%A733_TRACE_DUMP_RC%
    >>"%A733_LOG%" echo A733_TRACE_VALIDATE status=FAIL reason=dump_rc rc=%A733_TRACE_DUMP_RC%
    if "%A733_TRACE_FAIL_RC%"=="0" set "A733_TRACE_FAIL_RC=%A733_TRACE_DUMP_RC%"
)

echo.
if "%A733_SKIP_TRACE_FINAL_SUMMARY%"=="1" (
    echo --- A733 SMHC ETW summary skipped ---
    >"%A733_TRACE_SUMMARY_OUT%" echo A733_TRACE_SUMMARY_SKIP reason=skip_trace_final_summary
    set "A733_TRACE_SUMMARY_RC=0"
) else if "%A733_DISK_PUBLICATION_ONLY%"=="1" (
    echo --- A733 SMHC ETW key summary ---
    "%A733_PKG%\A733WinPeSmhcTrace.exe" keysummary "%A733_TRACE_LOG%" >"%A733_TRACE_SUMMARY_OUT%" 2>&1
) else if "%A733_STOP_AFTER_RAW_PERSIST_PROBE%"=="1" (
    echo --- A733 SMHC ETW key summary ---
    "%A733_PKG%\A733WinPeSmhcTrace.exe" keysummary "%A733_TRACE_LOG%" >"%A733_TRACE_SUMMARY_OUT%" 2>&1
) else (
    echo --- A733 SMHC ETW summary ---
    "%A733_PKG%\A733WinPeSmhcTrace.exe" summary "%A733_TRACE_LOG%" >"%A733_TRACE_SUMMARY_OUT%" 2>&1
)
if not "%A733_SKIP_TRACE_FINAL_SUMMARY%"=="1" set "A733_TRACE_SUMMARY_RC=%ERRORLEVEL%"
type "%A733_TRACE_SUMMARY_OUT%"
type "%A733_TRACE_SUMMARY_OUT%" >>"%A733_LOG%"
echo.
if "%A733_SKIP_TRACE_FINAL_SUMMARY%"=="1" (
    echo A733_TRACE_LOWARG_RECAP_SKIP reason=skip_trace_final_summary
    >>"%A733_LOG%" echo A733_TRACE_LOWARG_RECAP_SKIP reason=skip_trace_final_summary
) else (
    echo --- A733 SMHC ETW low-arg recap ---
    "%A733_PKG%\A733WinPeSmhcTrace.exe" lowargsummary "%A733_TRACE_LOG%"
    "%A733_PKG%\A733WinPeSmhcTrace.exe" lowargsummary "%A733_TRACE_LOG%" >>"%A733_LOG%" 2>&1
)
echo A733_TRACE_SUMMARY_RC=%A733_TRACE_SUMMARY_RC%
>>"%A733_LOG%" echo A733_TRACE_SUMMARY_RC=%A733_TRACE_SUMMARY_RC%

if not "%A733_STOP_AFTER_RAW_PERSIST_PROBE%"=="1" type "%A733_TRACE_LOG%" >>"%A733_LOG%"
echo A733_TRACE_STOP status=complete stop_rc=%A733_TRACE_STOP_RC% verify_rc=%A733_TRACE_VERIFY_RC% dump_rc=%A733_TRACE_DUMP_RC%
>>"%A733_LOG%" echo A733_TRACE_STOP status=complete stop_rc=%A733_TRACE_STOP_RC% verify_rc=%A733_TRACE_VERIFY_RC% dump_rc=%A733_TRACE_DUMP_RC%
set "A733_TRACE_STARTED=0"
exit /b %A733_TRACE_FAIL_RC%
