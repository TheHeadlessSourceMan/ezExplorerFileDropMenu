@echo off
setlocal
set "ROOT=%~dp0"
set "VERSION=%~1"
if "%VERSION%"=="" set "VERSION=1.0.0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%ROOT%build.ps1"
if errorlevel 1 exit /b 1

set "ISCC="
where ISCC.exe >nul 2>&1 && set "ISCC=ISCC.exe"
if not defined ISCC if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    echo Inno Setup 6 not found. Install it from https://jrsoftware.org/isdl.php
    exit /b 1
)

"%ISCC%" /DAppVersion=%VERSION% "%ROOT%installer\installer.iss"
if errorlevel 1 exit /b 1

echo Installer: %ROOT%artifacts\installer
endlocal
