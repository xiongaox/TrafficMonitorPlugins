<#
.SYNOPSIS
    一键推送代码 -> 监听云端 x64 快速编译 -> 自动下载并热替换 TrafficMonitor 插件。

.EXAMPLE
    .\sync-stock.ps1
    .\sync-stock.ps1 -NoPush
#>

param(
    [string]$PluginDir = "D:\Program Files (x86)\NIR\TrafficMonitor\plugins",
    [string]$AppPath = "D:\Program Files (x86)\NIR\TrafficMonitor\TrafficMonitor.exe",
    [switch]$NoPush,
    [switch]$NoRestart
)

$ErrorActionPreference = "Stop"
$RepoOwner = "xiongaox"
$RepoName = "TrafficMonitorPlugins"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  TrafficMonitor Stock 插件一键云端编译与同步工具" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

# 1. 检查并推送代码
if (-not $NoPush) {
    $status = git status --porcelain
    if ($status) {
        Write-Host "⚠️ 检测到工作区有未提交的修改，正在自动提交..." -ForegroundColor Yellow
        git add .
        $commitMsg = "sync: automated build trigger $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        git commit -m $commitMsg
    }

    Write-Host "🚀 正在推送最新代码到 GitHub (main 分支)..." -ForegroundColor Green
    git push origin main
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Git 推送失败，请检查网络或 Git 配置。"
        exit 1
    }
}

$localHead = (git rev-parse HEAD).Trim()
Write-Host "📌 当前目标 Commit: $($localHead.Substring(0, 7))" -ForegroundColor Gray

# 2. 等待并轮询 GitHub Actions 编译结果
Write-Host "⏳ 正在等待 GitHub Actions 启动编译..." -ForegroundColor Yellow
$startTime = Get-Date
$runFound = $null

for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    try {
        $runsResp = Invoke-RestMethod -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/actions/runs?event=push&per_page=5" -Headers @{"User-Agent"="PowerShell"}
        foreach ($run in $runsResp.workflow_runs) {
            if ($run.head_sha -eq $localHead) {
                $runFound = $run
                break
            }
        }
    } catch {
        # 忽略网络抖动
    }
    if ($runFound) { break }
}

if (-not $runFound) {
    # 如果没精确匹配到 head_sha，取最新的一个运行
    try {
        $runsResp = Invoke-RestMethod -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/actions/runs?per_page=1" -Headers @{"User-Agent"="PowerShell"}
        $runFound = $runsResp.workflow_runs[0]
    } catch {}
}

if (-not $runFound) {
    Write-Error "未能获取到 GitHub Actions 构建任务，请检查 GitHub 网络连接。"
    exit 1
}

Write-Host "🎯 追踪构建任务 ID: $($runFound.id) ($($runFound.html_url))" -ForegroundColor Gray

# 轮询构建状态
while ($true) {
    $elapsed = [int]((Get-Date) - $startTime).TotalSeconds
    Write-Host "`r⏳ 云端 x64 编译进行中... 已用时 ${elapsed}s  " -NoNewline -ForegroundColor Cyan

    try {
        $check = Invoke-RestMethod -Uri "https://api.github.com/repos/$RepoOwner/$RepoName/actions/runs/$($runFound.id)" -Headers @{"User-Agent"="PowerShell"}
        if ($check.status -eq "completed") {
            Write-Host ""
            if ($check.conclusion -eq "success") {
                Write-Host "✅ 云端编译成功！总耗时: ${elapsed}s" -ForegroundColor Green
                break
            } else {
                Write-Host "❌ 云端编译失败 (结论: $($check.conclusion))！" -ForegroundColor Red
                Write-Host "查看日志: $($check.html_url)" -ForegroundColor Yellow
                exit 1
            }
        }
    } catch {
        # 忽略单次网络重试
    }

    Start-Sleep -Seconds 3
}

# 3. 下载最新的 Stock.dll (通过 Release 公开下载)
Write-Host "📥 正在下载最新的 Stock.dll..." -ForegroundColor Yellow
$tempZip = Join-Path $env:TEMP "Stock_x64_$(Get-Random).zip"
$tempExtractDir = Join-Path $env:TEMP "Stock_x64_$(Get-Random)"

$downloadUrl = "https://github.com/$RepoOwner/$RepoName/releases/download/latest-stock/Stock_x64.zip"

try {
    Invoke-WebRequest -Uri $downloadUrl -OutFile $tempZip -Headers @{"User-Agent"="PowerShell"}
} catch {
    Write-Host "Release 资源就绪中，稍等 3 秒重试..." -ForegroundColor Gray
    Start-Sleep -Seconds 3
    Invoke-WebRequest -Uri $downloadUrl -OutFile $tempZip -Headers @{"User-Agent"="PowerShell"}
}

# 解压
Expand-Archive -Path $tempZip -DestinationPath $tempExtractDir -Force
$newDllPath = Join-Path $tempExtractDir "Stock.dll"
if (-not (Test-Path $newDllPath)) {
    # 递归查找
    $newDllPath = (Get-ChildItem -Path $tempExtractDir -Filter "Stock.dll" -Recurse | Select-Object -First 1).FullName
}

if (-not $newDllPath -or -not (Test-Path $newDllPath)) {
    Write-Error "解压包中未找到 Stock.dll！"
    exit 1
}

# 4. 替换插件 & 重启 TrafficMonitor
if (-not (Test-Path $PluginDir)) {
    New-Item -ItemType Directory -Path $PluginDir -Force | Out-Null
}

$targetDll = Join-Path $PluginDir "Stock.dll"

# 检查 TrafficMonitor 是否在运行
$tmProcess = Get-Process -Name "TrafficMonitor" -ErrorAction SilentlyContinue
$needRestart = $false

if ($tmProcess -and (-not $NoRestart)) {
    Write-Host "🔄 检测到 TrafficMonitor 正在运行，正在重启以加载新插件..." -ForegroundColor Yellow
    Stop-Process -Name "TrafficMonitor" -Force
    Start-Sleep -Milliseconds 800
    $needRestart = $true
}

# 拷贝覆盖
Copy-Item -Path $newDllPath -Destination $targetDll -Force
$dllSize = (Get-Item $targetDll).Length / 1MB
Write-Host "📦 成功更新插件: $targetDll ($([math]::Round($dllSize, 2)) MB)" -ForegroundColor Green

# 重启应用
if ($needRestart -and (Test-Path $AppPath)) {
    Start-Process -FilePath $AppPath -WorkingDirectory (Split-Path $AppPath)
    Write-Host "🚀 TrafficMonitor 已自动重启并加载新插件！" -ForegroundColor Green
}

# 清理临时文件
Remove-Item -Path $tempZip, $tempExtractDir -Recurse -Force -ErrorAction SilentlyContinue

Write-Host "🎉 全部同步完成！" -ForegroundColor Cyan
