@echo off
setlocal

set "INSTALL_DIR=%LOCALAPPDATA%\img2file"
set "TASK_NAME=Img2File"

:: Find exe: next to this script (release), or in build/ (dev)
set "EXE_SRC=%~dp0img2file.exe"
if not exist "%EXE_SRC%" set "EXE_SRC=%~dp0build\img2file.exe"

if not exist "%EXE_SRC%" (
    echo ERROR: img2file.exe not found.
    exit /b 1
)

:: Create install directory
if not exist "%INSTALL_DIR%" mkdir "%INSTALL_DIR%"

:: Kill existing instance if running
taskkill /IM img2file.exe /F >nul 2>&1

:: Copy exe
copy /Y "%EXE_SRC%" "%INSTALL_DIR%\img2file.exe" >nul

:: Register to run at logon (current user, no admin needed)
reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Run" /v "%TASK_NAME%" /t REG_SZ /d "\"%INSTALL_DIR%\img2file.exe\"" /f >nul 2>&1

:: Start it now
start "" "%INSTALL_DIR%\img2file.exe"

echo Installed and running. Paste images into Explorer folders with Ctrl+V.
