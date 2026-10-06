@echo off
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_2025.ps1"
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed with code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo [OK] Build completed successfully.
