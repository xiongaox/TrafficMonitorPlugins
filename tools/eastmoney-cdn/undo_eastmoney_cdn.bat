@echo off
title 东财 CDN 节点修复 - 回滚
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo 正在请求管理员权限，请在 UAC 弹窗点"是"...
    powershell -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
set "HOSTS=%SystemRoot%\System32\drivers\etc\hosts"
if not exist "%HOSTS%.bak_eastmoney" (
    echo [失败] 找不到备份 hosts.bak_eastmoney，请手动删除 hosts 里以 117.184.38.248 开头的三行。
    pause
    exit /b
)
copy /y "%HOSTS%.bak_eastmoney" "%HOSTS%" >nul
echo [完成] 已恢复 hosts 备份（注意：备份之后 hosts 的其他改动会被一并还原）。
ipconfig /flushdns >nul
echo [完成] 已刷新 DNS 缓存。
pause
