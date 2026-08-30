$ErrorActionPreference = 'Stop'
$solDir = (Get-Location).Path + '\'
$msbuild = 'D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$src = 'bin\x64\Release\Stock.dll'

# 自动探测本机全部 TrafficMonitor 安装目录（以目录下存在 TrafficMonitor.exe 为准）。
# 多个安装共用 AppData 里的同一份 config.ini，插件显示项勾选按 item id 持久化，
# 任一目录的 Stock.dll 版本落后都会在切换实例时把勾选状态挤掉（显示项重启后取消勾选），
# 因此必须把所有安装目录全部同步替换。
$candidateDirs = @(
    'D:\Program Files (x86)\NIR\TrafficMonitor',
    'D:\MyDir\soft\TrafficMonitor',
    'C:\Program Files (x86)\TrafficMonitor',
    'C:\Program Files\TrafficMonitor'
)
$tmDirs = @($candidateDirs | Where-Object { Test-Path (Join-Path $_ 'TrafficMonitor.exe') })
if ($tmDirs.Count -eq 0) {
    $tmDirs = @('D:\Program Files (x86)\NIR\TrafficMonitor')
}
$tmDirs | ForEach-Object { Write-Host "[*] TrafficMonitor dir: $_" -ForegroundColor Cyan }

$tm = Get-Process -Name 'TrafficMonitor' -ErrorAction SilentlyContinue
if ($tm) {
    Write-Host '[!] Warning: TrafficMonitor process detected. If file is locked, replacement will fail.' -ForegroundColor Yellow
}

Write-Host '[*] Building utilities (x64 Release)...' -ForegroundColor Cyan
& $msbuild utilities\utilities.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solDir" /m /v:m
if ($LASTEXITCODE -ne 0) {
    Write-Error 'Build utilities failed.'
    exit 1
}

Write-Host '[*] Building Stock Plugin (x64 Release)...' -ForegroundColor Cyan
& $msbuild Plugins\Stock\Stock.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solDir" /m /v:m
if ($LASTEXITCODE -ne 0) {
    Write-Error 'Build Stock failed.'
    exit 1
}

if (-not (Test-Path $src)) {
    Write-Error "Source file not found: $src"
    exit 1
}

$hashSrc = (Get-FileHash -Path $src -Algorithm MD5).Hash
$failed = @()
foreach ($tmDir in $tmDirs) {
    $dst = Join-Path $tmDir 'plugins\Stock.dll'
    try {
        if (Test-Path $dst) {
            $stream = [System.IO.File]::Open($dst, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
            $stream.Close()
        }
        Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop

        $hashDst = (Get-FileHash -Path $dst -Algorithm MD5).Hash
        if ($hashSrc -ne $hashDst) {
            throw 'Hash mismatch between source and destination!'
        }

        $sizeMB = [math]::Round((Get-Item $dst).Length / 1MB, 2)
        Write-Host "[+] Successfully replaced: $dst ($sizeMB MB)" -ForegroundColor Green
        Write-Host "[+] Verified MD5: $hashDst" -ForegroundColor Green
    } catch {
        Write-Host '=========================================================' -ForegroundColor Red
        Write-Host "[ERROR] Failed to replace Stock.dll in $tmDir!" -ForegroundColor Red
        Write-Host "Reason: $($_.Exception.Message)" -ForegroundColor Red
        Write-Host 'TrafficMonitor may be running and locking the file.' -ForegroundColor Yellow
        Write-Host '=========================================================' -ForegroundColor Red
        $failed += $tmDir
    }
}
if ($failed.Count -gt 0) {
    Write-Host "[!] Skipped (locked): $($failed -join ', ')" -ForegroundColor Yellow
    exit 1
}
