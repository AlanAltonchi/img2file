@echo off
setlocal

set "INSTALL_DIR=%LOCALAPPDATA%\img2file"
set "TASK_NAME=Img2File"

:: Kill running instance
taskkill /IM img2file.exe /F >nul 2>&1

:: Remove startup entry
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "%TASK_NAME%" /f >nul 2>&1
:: Clean up old scheduled task if it exists
schtasks /Delete /TN "%TASK_NAME%" /F >nul 2>&1

:: Delete files
if exist "%INSTALL_DIR%" rmdir /S /Q "%INSTALL_DIR%"

echo Uninstalled.
