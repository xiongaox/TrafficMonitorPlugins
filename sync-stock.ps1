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
Write-Host "  TrafficMonitor Stock Plugin Fast Sync Tool (x64)" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

# 1. Check and push changes
if (-not $NoPush) {
    $status = git status --porcelain
    if ($status) {
        Write-Host "[*] Detected uncommitted changes, committing..." -ForegroundColor Yellow
        git add .
        $msg = "sync: auto trigger $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
        git commit -m $msg
    }

    Write-Host "[*] Pushing code to GitHub (main branch)..." -ForegroundColor Green
    git push origin main
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Git push failed."
        exit 1
    }
}

$localHead = (git rev-parse HEAD).Trim()
Write-Host "[*] Target Commit: $($localHead.Substring(0, 7))" -ForegroundColor Gray

# 2. Wait for GitHub Actions
Write-Host "[*] Waiting for GitHub Actions workflow to start..." -ForegroundColor Yellow
$startTime = Get-Date
$runFound = $null

for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Seconds 2
    try {
        $apiUri = "https://api.github.com/repos/" + $RepoOwner + "/" + $RepoName + "/actions/runs?event=push&per_page=5"
        $runsResp = Invoke-RestMethod -Uri $apiUri -Headers @{"User-Agent"="PowerShell"}
        foreach ($run in $runsResp.workflow_runs) {
            if ($run.head_sha -eq $localHead) {
                $runFound = $run
                break
            }
        }
    } catch {}
    if ($runFound) { break }
}

if (-not $runFound) {
    try {
        $apiUri = "https://api.github.com/repos/" + $RepoOwner + "/" + $RepoName + "/actions/runs?per_page=1"
        $runsResp = Invoke-RestMethod -Uri $apiUri -Headers @{"User-Agent"="PowerShell"}
        $runFound = $runsResp.workflow_runs[0]
    } catch {}
}

if (-not $runFound) {
    Write-Error "Failed to locate GitHub Actions workflow run."
    exit 1
}

Write-Host "[*] Tracking Run ID: $($runFound.id)" -ForegroundColor Gray
Write-Host "[*] Run URL: $($runFound.html_url)" -ForegroundColor Gray

# Poll run completion
while ($true) {
    $elapsed = [int]((Get-Date) - $startTime).TotalSeconds
    Write-Host "`r[*] Building in cloud (x64)... elapsed: ${elapsed}s  " -NoNewline -ForegroundColor Cyan

    try {
        $checkUri = "https://api.github.com/repos/" + $RepoOwner + "/" + $RepoName + "/actions/runs/" + $runFound.id
        $check = Invoke-RestMethod -Uri $checkUri -Headers @{"User-Agent"="PowerShell"}
        if ($check.status -eq "completed") {
            Write-Host ""
            if ($check.conclusion -eq "success") {
                Write-Host "[+] Cloud build succeeded! Total time: ${elapsed}s" -ForegroundColor Green
                break
            } else {
                Write-Host "[-] Cloud build failed (conclusion: $($check.conclusion))!" -ForegroundColor Red
                Write-Host "[-] Check log: $($check.html_url)" -ForegroundColor Yellow
                exit 1
            }
        }
    } catch {}

    Start-Sleep -Seconds 3
}

# 3. Download Stock.dll from Release
Write-Host "[*] Downloading Stock.dll release..." -ForegroundColor Yellow
$tempZip = Join-Path $env:TEMP ("Stock_x64_" + (Get-Random) + ".zip")
$tempExtractDir = Join-Path $env:TEMP ("Stock_x64_" + (Get-Random))

$downloadUrl = "https://github.com/" + $RepoOwner + "/" + $RepoName + "/releases/download/latest-stock/Stock_x64.zip"

$downloaded = $false
for ($retry = 0; $retry -lt 5; $retry++) {
    try {
        Invoke-WebRequest -Uri $downloadUrl -OutFile $tempZip -Headers @{"User-Agent"="PowerShell"}
        if ((Get-Item $tempZip).Length -gt 1000) {
            $downloaded = $true
            break
        }
    } catch {
        Start-Sleep -Seconds 3
    }
}

if (-not $downloaded) {
    Write-Error "Failed to download release zip."
    exit 1
}

# Extract
Expand-Archive -Path $tempZip -DestinationPath $tempExtractDir -Force
$newDllPath = Join-Path $tempExtractDir "Stock.dll"
if (-not (Test-Path $newDllPath)) {
    $found = Get-ChildItem -Path $tempExtractDir -Filter "Stock.dll" -Recurse | Select-Object -First 1
    if ($found) { $newDllPath = $found.FullName }
}

if (-not (Test-Path $newDllPath)) {
    Write-Error "Stock.dll not found in extracted archive."
    exit 1
}

# 4. Replace plugin & restart TrafficMonitor
if (-not (Test-Path $PluginDir)) {
    New-Item -ItemType Directory -Path $PluginDir -Force | Out-Null
}

$targetDll = Join-Path $PluginDir "Stock.dll"
$needRestart = $false

$tmProcess = Get-Process -Name "TrafficMonitor" -ErrorAction SilentlyContinue
if ($tmProcess -and (-not $NoRestart)) {
    Write-Host "[*] TrafficMonitor is currently running." -ForegroundColor Yellow
    try {
        Stop-Process -Name "TrafficMonitor" -Force -ErrorAction Stop
        Start-Sleep -Milliseconds 800
        $needRestart = $true
    } catch {
        Write-Host "[!] TrafficMonitor is running as Administrator." -ForegroundColor Yellow
        Write-Host "[!] Please right-click and exit TrafficMonitor in system tray, or run this script as Administrator." -ForegroundColor Yellow
        while (Get-Process -Name "TrafficMonitor" -ErrorAction SilentlyContinue) {
            Start-Sleep -Seconds 1
        }
        $needRestart = $true
    }
}

Copy-Item -Path $newDllPath -Destination $targetDll -Force
$dllSize = (Get-Item $targetDll).Length / 1MB
Write-Host "[+] Updated plugin: $targetDll ($([math]::Round($dllSize, 2)) MB)" -ForegroundColor Green

if ($needRestart -and (Test-Path $AppPath)) {
    Start-Process -FilePath $AppPath -WorkingDirectory (Split-Path $AppPath)
    Write-Host "[+] TrafficMonitor restarted successfully!" -ForegroundColor Green
}

Remove-Item -Path $tempZip, $tempExtractDir -Recurse -Force -ErrorAction SilentlyContinue
Write-Host "[+] All done! Plugin synchronized." -ForegroundColor Cyan
