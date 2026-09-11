@echo off
title 东财 CDN 节点修复
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo 正在请求管理员权限，请在 UAC 弹窗点"是"...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
set "HOSTS=%SystemRoot%\System32\drivers\etc\hosts"
if not exist "%HOSTS%.bak_eastmoney" copy /y "%HOSTS%" "%HOSTS%.bak_eastmoney" >nul
findstr /C:"push2.eastmoney.com" "%HOSTS%" >nul 2>&1
if %errorlevel%==0 (
    echo [已存在] hosts 里已有东财固定解析条目，跳过添加。
    goto :flush
)
>>"%HOSTS%" echo.
>>"%HOSTS%" echo # 东财 CDN 节点固定解析 (2026-09-07 添加，CDN 恢复后可用 undo_eastmoney_cdn.bat 回滚)
>>"%HOSTS%" echo 117.184.38.248 push2.eastmoney.com
>>"%HOSTS%" echo 117.184.38.248 push2his.eastmoney.com
>>"%HOSTS%" echo 117.184.38.248 push2ex.eastmoney.com
echo [完成] 已添加固定解析 117.184.38.248 -> push2 / push2his / push2ex.eastmoney.com
:flush
ipconfig /flushdns >nul
echo [完成] 已刷新 DNS 缓存。
echo.
echo 请重启 TrafficMonitor 让 Stock 插件生效。
pause
