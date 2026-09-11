<#
.SYNOPSIS
    编译 Stock 插件并同步到本机全部 TrafficMonitor 安装目录。

.DESCRIPTION
    由原 build-local.ps1（多目录同步）、sync-stock-local.ps1（vswhere 定位 MSBuild、
    单目录覆盖、runas 重启）与 deploy.ps1（单目录替换 + SHA256 校验）合并而来，
    是本地编译 / 部署 Stock 插件的唯一入口。

    多个 TrafficMonitor 安装共用 AppData 里的同一份 config.ini，插件显示项勾选按
    item id 持久化，任一目录的 Stock.dll 版本落后都会在切换实例时把勾选状态挤掉
    （显示项重启后取消勾选），因此默认把所有探测到的安装目录全部同步替换。

.PARAMETER PluginDir
    显式指定目标 plugins 目录，可传多个。省略则自动探测本机全部 TrafficMonitor 安装。

.PARAMETER AppPath
    显式指定 TrafficMonitor.exe，用于同步后重启。省略则取第一个探测到的安装目录。

.PARAMETER NoRestart
    不结束也不重启 TrafficMonitor（dll 被占用时会替换失败）。

.PARAMETER NoBuild
    跳过编译，直接用现有的 bin\x64\Release\Stock.dll 做同步。

.EXAMPLE
    .\build-local.ps1
    编译并同步到本机全部 TrafficMonitor 安装目录，然后以管理员身份重启。

.EXAMPLE
    .\build-local.ps1 -NoRestart -NoBuild
    不编译、不重启，只把现有 Stock.dll 复制到各安装目录。

.EXAMPLE
    .\build-local.ps1 -NoBuild -NoRestart -PluginDir 'D:\Program Files (x86)\NIR\TrafficMonitor\plugins'
    只把现有 Stock.dll 部署到指定目录（等价于原 deploy.ps1）。
#>
param(
    [string[]]$PluginDir = @(),
    [string]$AppPath = "",
    [switch]$NoRestart,
    [switch]$NoBuild
)

$ErrorActionPreference = 'Stop'

$solDir = (Resolve-Path $PSScriptRoot).Path + '\'
$src = Join-Path $PSScriptRoot 'bin\x64\Release\Stock.dll'

# ---------------------------------------------------------------- 1. 定位 MSBuild
function Get-MSBuildPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    # vswhere 不可用或未命中时的常见安装位置兜底
    $fallbacks = @(
        'D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
    )
    foreach ($f in $fallbacks) { if (Test-Path $f) { return $f } }
    throw 'MSBuild.exe 未找到（vswhere 与常见路径均未命中），请检查 Visual Studio Build Tools 安装。'
}

# ------------------------------------------------------- 2. 解析目标目录 / 重启路径
$candidateDirs = @(
    'D:\Program Files (x86)\NIR\TrafficMonitor',
    'D:\MyDir\soft\TrafficMonitor',
    'C:\Program Files (x86)\TrafficMonitor',
    'C:\Program Files\TrafficMonitor'
)

if ($PluginDir.Count -eq 0) {
    # 以目录下存在 TrafficMonitor.exe 为准，避免误选残留的空目录
    $tmDirs = @($candidateDirs | Where-Object { Test-Path (Join-Path $_ 'TrafficMonitor.exe') })
    if ($tmDirs.Count -eq 0) {
        Write-Warning '未探测到任何 TrafficMonitor 安装目录，回退到默认路径。'
        $tmDirs = @($candidateDirs[0])
    }
    $PluginDir = @($tmDirs | ForEach-Object { Join-Path $_ 'plugins' })
    if (-not $AppPath) { $AppPath = Join-Path $tmDirs[0] 'TrafficMonitor.exe' }
}

Write-Host '==================================================' -ForegroundColor Cyan
Write-Host '  TrafficMonitor Stock Plugin Local Build & Sync   ' -ForegroundColor Cyan
Write-Host '==================================================' -ForegroundColor Cyan
$PluginDir | ForEach-Object { Write-Host "[*] Target: $_" -ForegroundColor Gray }

# -------------------------------------------------------------------- 3. 编译
if (-not $NoBuild) {
    $msbuild = Get-MSBuildPath
    Write-Host "[*] MSBuild: $msbuild" -ForegroundColor Gray

    $projects = @('utilities\utilities.vcxproj', 'Plugins\Stock\Stock.vcxproj')
    for ($i = 0; $i -lt $projects.Count; $i++) {
        $proj = $projects[$i]
        Write-Host "[*] [$($i + 1)/$($projects.Count)] Building $proj ..." -ForegroundColor Cyan
        & $msbuild (Join-Path $PSScriptRoot $proj) /nologo /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solDir" /m /v:m
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $proj" }
    }
}

if (-not (Test-Path $src)) { throw "Source file not found: $src" }

# ------------------------------------------- 4. 结束运行中的 TrafficMonitor（可选）
$needRestart = $false
if (-not $NoRestart) {
    $procs = @(Get-Process -Name 'TrafficMonitor' -ErrorAction SilentlyContinue)
    if ($procs.Count -gt 0) {
        Write-Host "[*] 关闭运行中的 TrafficMonitor（$($procs.Count) 个进程）..." -ForegroundColor Yellow
        foreach ($p in $procs) {
            try {
                $p.Kill()
                $p.WaitForExit(3000) | Out-Null
            } catch {
                Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
            }
        }
        Start-Sleep -Milliseconds 500
        $needRestart = $true
    }
} else {
    $running = @(Get-Process -Name 'TrafficMonitor' -ErrorAction SilentlyContinue)
    if ($running.Count -gt 0) {
        Write-Host '[!] 检测到 TrafficMonitor 正在运行，-NoRestart 下 dll 可能被占用导致替换失败。' -ForegroundColor Yellow
    }
}

# --------------------------------------------------- 5. 逐目录同步 + SHA256 校验
$hashSrc = (Get-FileHash -Path $src -Algorithm SHA256).Hash
$failed = @()

foreach ($dir in $PluginDir) {
    $dst = Join-Path $dir 'Stock.dll'
    try {
        if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
        Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop

        $hashDst = (Get-FileHash -Path $dst -Algorithm SHA256).Hash
        if ($hashSrc -ne $hashDst) { throw 'SHA256 mismatch between source and destination!' }

        $sizeMB = [math]::Round((Get-Item $dst).Length / 1MB, 2)
        Write-Host "[+] Replaced: $dst ($sizeMB MB)" -ForegroundColor Green
        Write-Host "[+] Verified SHA256: $hashDst" -ForegroundColor Green
    } catch {
        Write-Host '=========================================================' -ForegroundColor Red
        Write-Host "[ERROR] Failed to replace Stock.dll in $dir !" -ForegroundColor Red
        Write-Host "Reason: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host 'TrafficMonitor may be running and locking the file.' -ForegroundColor Yellow
        Write-Host '=========================================================' -ForegroundColor Red
        $failed += $dir
    }
}

# --------------------------------------------------- 6. 以管理员身份重启 TrafficMonitor
if ($needRestart -and $AppPath -and (Test-Path $AppPath)) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $AppPath
    $psi.WorkingDirectory = Split-Path $AppPath
    $psi.Verb = 'runas'
    $psi.UseShellExecute = $true
    try {
        [System.Diagnostics.Process]::Start($psi) | Out-Null
        Write-Host '[+] TrafficMonitor started with Administrator privileges!' -ForegroundColor Green
    } catch {
        Write-Warning "UAC elevation cancelled or failed: $($_.Exception.Message)"
    }
}

if ($failed.Count -gt 0) {
    Write-Host "[!] Skipped (locked/failed): $($failed -join ', ')" -ForegroundColor Yellow
    exit 1
}

Write-Host '[+] All done! Stock.dll synchronized.' -ForegroundColor Cyan
