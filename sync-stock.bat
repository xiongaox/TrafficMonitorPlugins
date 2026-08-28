@echo off
chcp 65001 >nul
cd /d "%~dp0"
title TrafficMonitor Stock Plugin Fast Sync Tool (x64)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sync-stock.ps1" %*

echo.
echo Press any key to exit...
pause >nul
