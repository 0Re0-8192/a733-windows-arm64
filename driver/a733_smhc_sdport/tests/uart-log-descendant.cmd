@echo off
start "" /b cmd.exe /d /c "ping.exe -n 7 127.0.0.1 >nul & echo DESCENDANT_END"
exit /b 0
