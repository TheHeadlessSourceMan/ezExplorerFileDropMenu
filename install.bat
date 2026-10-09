@echo off
setlocal
set "ARTIFACT=%~1"
if "%ARTIFACT%"=="" set "ARTIFACT=%~dp0artifacts\release"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\install.ps1" -ArtifactDirectory "%ARTIFACT%"
exit /b %errorlevel%
