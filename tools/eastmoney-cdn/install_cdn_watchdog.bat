@echo off
title 东财 CDN 看门狗 - 安装
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo 正在请求管理员权限，请在 UAC 弹窗点"是"...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
set "WORK=C:\ProgramData\eastmoney_cdn_watchdog"
if not exist "%WORK%" mkdir "%WORK%"
copy /y "%~dp0eastmoney_cdn_watchdog.ps1" "%WORK%\" >nul
schtasks /Create /TN "EastmoneyCDNWatchdog" /TR "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File %WORK%\eastmoney_cdn_watchdog.ps1" /SC HOURLY /ST 00:05 /RU SYSTEM /F
if %errorlevel%==0 (
    echo [完成] 看门狗已安装：每小时自动检查东财 CDN 节点，失效自动切换。
    echo [完成] 现在立即运行一次验证...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%WORK%\eastmoney_cdn_watchdog.ps1"
    echo [完成] 安装结束。日志位置：%WORK%\watchdog.log
) else (
    echo [失败] 计划任务创建失败。
)
pause
