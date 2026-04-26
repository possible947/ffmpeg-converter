@echo off
:: windows_build_fpc.bat — thin launcher for windows_build_fpc.ps1
:: Builds the Pascal CLI and GUI, then stages all outputs + bundled DLLs
:: into fpc\bin\.
::
:: Usage (same flags as the .ps1):
::   windows_build_fpc.bat
::   windows_build_fpc.bat -CLIOnly
::   windows_build_fpc.bat -GUIOnly
::   windows_build_fpc.bat -Clean
::   windows_build_fpc.bat -Clean -GUIOnly

setlocal
set SCRIPT_DIR=%~dp0
powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%SCRIPT_DIR%windows_build_fpc.ps1" %*
exit /b %ERRORLEVEL%
