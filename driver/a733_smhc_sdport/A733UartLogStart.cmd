@echo off
setlocal EnableExtensions
set "A733_PKG=%SystemRoot%\System32\A733Smhc"
set "A733_UART_START=%SystemDrive%\A733UartStart.log"
if /i "%~1"=="--run-diagnostics" goto :diagnostics
"%A733_PKG%\A733WinPeCertInstall.exe" >"%A733_UART_START%" 2>&1
"%A733_PKG%\A733WinPeUartLog.exe" --start-driver >>"%A733_UART_START%" 2>&1
type "%A733_UART_START%"
"%A733_PKG%\A733WinPeUartLog.exe" --capture "\\.\A733UartLog" "%SystemDrive%\A733Console.log" "%ComSpec% /d /c type %A733_UART_START% & call %SystemRoot%\System32\A733UartLogStart.cmd --run-diagnostics"
set "A733_UART_RC=%ERRORLEVEL%"
echo A733_UART_RELAY_EXIT=%A733_UART_RC%
endlocal & exit /b %A733_UART_RC%

:diagnostics
set "A733_UART_LOG_ACTIVE=1"
echo [A733-CONSOLE] wpeinit-begin
"%SystemRoot%\System32\wpeinit.exe"
echo [A733-CONSOLE] wpeinit-end error=%ERRORLEVEL%
call "%SystemRoot%\System32\A733SmhcInstall.cmd"
exit /b %ERRORLEVEL%
