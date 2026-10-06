@echo off
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0deploy_2026.ps1"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Deploy failed with code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo [OK] Deploy completed successfully.
