@echo off
title 东财 CDN 看门狗 - 卸载
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo 正在请求管理员权限，请在 UAC 弹窗点"是"...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
schtasks /Delete /TN "EastmoneyCDNWatchdog" /F
echo [完成] 看门狗已卸载（hosts 固定解析保留；如需一并移除请运行 undo_eastmoney_cdn.bat）。
pause
