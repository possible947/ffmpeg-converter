@echo off
:: windows_build.bat — thin launcher for windows_build.ps1
:: Runs the PowerShell build script without requiring the user to change
:: the execution policy manually.
::
:: Usage (same flags as the .ps1):
::   windows_build.bat
::   windows_build.bat -Config Debug
::   windows_build.bat -Clean
::   windows_build.bat -Clean -Config Debug
::   windows_build.bat -Rebuild
::   windows_build.bat -Target ALL_BUILD

setlocal
set SCRIPT_DIR=%~dp0
powershell.exe -NoProfile -ExecutionPolicy Bypass ^
    -File "%SCRIPT_DIR%windows_build.ps1" %*
exit /b %ERRORLEVEL%
