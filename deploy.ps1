$src = "C:\Users\xiongaox\Downloads\00code\TrafficMonitorPlugins\bin\x64\Release\Stock.dll"
$dst = "D:\Program Files (x86)\NIR\TrafficMonitor\plugins\Stock.dll"
Copy-Item -LiteralPath $src -Destination $dst -Force
$dstHash = (Get-FileHash $dst -Algorithm SHA256).Hash
$srcHash = (Get-FileHash $src -Algorithm SHA256).Hash
Write-Output "DST: $dstHash"
Write-Output "SRC: $srcHash"
if ($dstHash -eq $srcHash) { Write-Output "MATCH" } else { Write-Output "MISMATCH" }
