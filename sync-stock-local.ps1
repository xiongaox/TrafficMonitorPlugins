param(
    [string]$PluginDir = "",
    [string]$AppPath = "",
    [switch]$NoRestart
)

$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  TrafficMonitor Stock Plugin Local Build & Sync  " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$startTime = Get-Date

# 1. 自动检测 TrafficMonitor 安装路径
if (-not $PluginDir -or -not $AppPath) {
    $candidateDirs = @(
        "D:\Program Files (x86)\NIR\TrafficMonitor",
        "D:\MyDir\soft\TrafficMonitor",
        "C:\Program Files (x86)\TrafficMonitor",
        "C:\Program Files\TrafficMonitor"
    )
    $foundDir = $null
    foreach ($cand in $candidateDirs) {
        if (Test-Path $cand) {
            $foundDir = $cand
            break
        }
    }
    if (-not $foundDir) {
        $foundDir = "D:\Program Files (x86)\NIR\TrafficMonitor"
    }

    if (-not $PluginDir) {
        $PluginDir = Join-Path $foundDir "plugins"
    }
    if (-not $AppPath) {
        $AppPath = Join-Path $foundDir "TrafficMonitor.exe"
    }
}

Write-Host "[*] Plugin Dir : $PluginDir" -ForegroundColor Gray
Write-Host "[*] App Path   : $AppPath" -ForegroundColor Gray

# 2. 定位本地 MSBuild.exe
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Error "vswhere.exe not found! Please check Visual Studio Build Tools installation."
    exit 1
}

$vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) {
    Write-Error "No Visual Studio / Build Tools with MSBuild found!"
    exit 1
}

$msbuild = Join-Path $vsPath "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    Write-Error "MSBuild.exe not found at: $msbuild"
    exit 1
}

Write-Host "[*] Using MSBuild: $msbuild" -ForegroundColor Gray

# 3. 本地快速增量编译 (utilities + Stock)
$solutionDir = (Resolve-Path $PSScriptRoot).Path + "\"

Write-Host "[*] [1/2] Building utilities (x64 Release)..." -ForegroundColor Yellow
$utilProj = Join-Path $PSScriptRoot "utilities\utilities.vcxproj"
& $msbuild $utilProj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solutionDir" /v:m /nologo
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to build utilities.vcxproj"
    exit 1
}

Write-Host "[*] [2/2] Building Stock Plugin (x64 Release)..." -ForegroundColor Yellow
$stockProj = Join-Path $PSScriptRoot "Plugins\Stock\Stock.vcxproj"
& $msbuild $stockProj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solutionDir" /v:m /nologo
if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to build Stock.vcxproj"
    exit 1
}

$builtDll = Join-Path $PSScriptRoot "bin\x64\Release\Stock.dll"
if (-not (Test-Path $builtDll)) {
    Write-Error "Stock.dll not found after build: $builtDll"
    exit 1
}

# 4. 关闭 TrafficMonitor，覆盖 DLL，重启
if (-not (Test-Path $PluginDir)) {
    New-Item -ItemType Directory -Path $PluginDir -Force | Out-Null
}
$targetDll = Join-Path $PluginDir "Stock.dll"

$needRestart = $false
$tmProcesses = Get-Process -Name "TrafficMonitor" -ErrorAction SilentlyContinue
if ($tmProcesses -and (-not $NoRestart)) {
    Write-Host "[*] Closing running TrafficMonitor..." -ForegroundColor Yellow
    foreach ($proc in $tmProcesses) {
        try {
            $proc.Kill()
            $proc.WaitForExit(3000)
        } catch {
            Write-Warning "Could not kill process directly, trying Stop-Process -Force..."
            Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 500
    $needRestart = $true
}

Write-Host "[*] Copying Stock.dll to plugins directory..." -ForegroundColor Yellow
Copy-Item -Path $builtDll -Destination $targetDll -Force

$dllItem = Get-Item $targetDll
$dllSizeMB = [math]::Round($dllItem.Length / 1MB, 2)
Write-Host "[+] Updated: $targetDll (${dllSizeMB} MB)" -ForegroundColor Green

# 5. 启动 / 重启 TrafficMonitor
if ((-not $NoRestart) -and (Test-Path $AppPath)) {
    $appDir = Split-Path $AppPath
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $AppPath
    $psi.WorkingDirectory = $appDir
    $psi.UseShellExecute = $true
    [System.Diagnostics.Process]::Start($psi) | Out-Null
    Write-Host "[+] TrafficMonitor started successfully!" -ForegroundColor Green
}

$elapsed = [math]::Round(((Get-Date) - $startTime).TotalSeconds, 1)
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Success! Local sync completed in ${elapsed}s!  " -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Cyan
