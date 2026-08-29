param(
    [string]$PluginDir = "D:\MyDir\soft\TrafficMonitor\plugins",
    [string]$AppPath = "D:\MyDir\soft\TrafficMonitor\TrafficMonitor.exe",
    [switch]$NoPush,
    [switch]$NoRestart,
    [switch]$DirectDownload
)

$scriptPath = Join-Path $PSScriptRoot "sync-stock.ps1"
& $scriptPath -PluginDir $PluginDir -AppPath $AppPath -NoPush:$NoPush -NoRestart:$NoRestart -DirectDownload:$DirectDownload
