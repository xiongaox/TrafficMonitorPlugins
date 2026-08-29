$ErrorActionPreference = 'Stop'
$solDir = (Get-Location).Path + '\'
$msbuild = 'D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$src = 'bin\x64\Release\Stock.dll'
$dst = 'D:\MyDir\soft\TrafficMonitor\plugins\Stock.dll'

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

try {
    if (Test-Path $dst) {
        $stream = [System.IO.File]::Open($dst, [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
        $stream.Close()
    }
    Copy-Item -Path $src -Destination $dst -Force -ErrorAction Stop

    $hashSrc = (Get-FileHash -Path $src -Algorithm MD5).Hash
    $hashDst = (Get-FileHash -Path $dst -Algorithm MD5).Hash
    if ($hashSrc -ne $hashDst) {
        throw 'Hash mismatch between source and destination!'
    }

    $sizeMB = [math]::Round((Get-Item $dst).Length / 1MB, 2)
    Write-Host "[+] Successfully replaced: $dst ($sizeMB MB)" -ForegroundColor Green
    Write-Host "[+] Verified MD5: $hashDst" -ForegroundColor Green
} catch {
    Write-Host '=========================================================' -ForegroundColor Red
    Write-Host '[ERROR] Failed to replace Stock.dll!' -ForegroundColor Red
    Write-Host "Reason: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host 'TrafficMonitor may be running and locking the file.' -ForegroundColor Yellow
    Write-Host 'Please exit TrafficMonitor in system tray and retry!' -ForegroundColor Yellow
    Write-Host '=========================================================' -ForegroundColor Red
    exit 1
}
