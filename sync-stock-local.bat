@echo off
chcp 65001 >nul
cd /d "%~dp0"
title TrafficMonitor Stock Plugin Fast Sync Tool (Local - x64)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sync-stock-local.ps1" %*
if %ERRORLEVEL% neq 0 pause
