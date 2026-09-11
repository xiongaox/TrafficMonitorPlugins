# eastmoney_cdn_watchdog.ps1
# 东财 CDN 节点看门狗：由计划任务每小时以 SYSTEM 运行。
# 检查当前生效的 push2.eastmoney.com 节点是否存活；若已失效，
# 从候选 IP（当前固定解析 + 三个东财域名的 DNS 解析）中自动挑一个可用节点重写 hosts 固定解析。
# 日志：C:\ProgramData\eastmoney_cdn_watchdog\watchdog.log

$ErrorActionPreference = 'Continue'
$curl      = "$env:windir\System32\curl.exe"
$hostsFile = "$env:windir\System32\drivers\etc\hosts"
$workDir   = 'C:\ProgramData\eastmoney_cdn_watchdog'
$logFile   = Join-Path $workDir 'watchdog.log'
$domains   = @('push2.eastmoney.com', 'push2his.eastmoney.com', 'push2ex.eastmoney.com')
$testUrl   = 'https://push2.eastmoney.com/api/qt/clist/get?pn=1&pz=1&po=1&np=1&fltt=2&invt=2&fid=f62&fs=m:90+t:2&fields=f12'
$klineUrl  = 'https://push2his.eastmoney.com/api/qt/stock/kline/get?secid=1.000001&klt=101&fqt=1&end=20500101&lmt=1&fields1=f1,f2,f3&fields2=f51,f52,f53'
$referer   = 'Referer: https://quote.eastmoney.com'

try { New-Item -ItemType Directory -Force -Path $workDir | Out-Null } catch {}

function Log([string]$msg) {
    try {
        if (Test-Path $logFile) {
            if ((Get-Item $logFile).Length -gt 1MB) {
                Set-Content -Path $logFile -Value '' -Encoding UTF8
            }
        }
        Add-Content -Path $logFile -Value "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $msg" -Encoding UTF8 -ErrorAction Stop
    } catch {}
}

# 用正确 SNI 测试某个 IP 能否同时服务 push2 与 push2his 两个接口
function Test-Node([string]$ip) {
    try {
        $c1 = & $curl -s -o NUL --max-time 8 --resolve "push2.eastmoney.com:443:$ip" -w '%{http_code}' $testUrl -H $referer 2>$null
        $c2 = & $curl -s -o NUL --max-time 8 --resolve "push2his.eastmoney.com:443:$ip" -w '%{http_code}' $klineUrl -H $referer 2>$null
        return ($c1 -eq '200' -and $c2 -eq '200')
    } catch { return $false }
}

function Get-CurrentPin {
    try {
        $line = Select-String -Path $hostsFile -Pattern '^\s*([\d.]+)\s+push2\.eastmoney\.com\s*$' | Select-Object -First 1
        if ($line) { return $line.Matches[0].Groups[1].Value }
    } catch {}
    return $null
}

# 显式查询公共 DNS 服务器获取真实 DNS 结果（-Server 绕过 hosts 文件，否则固定解析会掩盖真实结果）
function Get-DnsIps([string]$domain) {
    foreach ($server in @('223.5.5.5', '114.114.114.114', '8.8.8.8')) {
        try {
            $ips = @(Resolve-DnsName $domain -Type A -Server $server -ErrorAction Stop |
                ForEach-Object { $_.IPAddress } | Where-Object { $_ })
            if ($ips.Count -gt 0) { return $ips }
        } catch {}
    }
    return @()
}

# 重写 hosts 固定解析（保留其他行，清理旧的东财固定解析块）
function Set-Pin([string]$ip) {
    $lines = Get-Content $hostsFile
    $kept = $lines | Where-Object {
        ($_ -notmatch '^\s*[\d.]+\s+push2(ex|his)?\.eastmoney\.com\s*$') -and
        (-not ($_ -match '^\s*#' -and $_ -match 'eastmoney|东财'))
    }
    $stamp = Get-Date -Format 'yyyy-MM-dd HH:mm'
    $new = @($kept, '', "# eastmoney CDN pin (auto-updated by watchdog $stamp)", "$ip push2.eastmoney.com", "$ip push2his.eastmoney.com", "$ip push2ex.eastmoney.com")
    Set-Content -Path $hostsFile -Value $new -Encoding Default
    ipconfig /flushdns | Out-Null
}

# --- 主流程 ---
$pin = Get-CurrentPin
$candidates = New-Object System.Collections.Generic.List[string]
if ($pin) { $candidates.Add($pin) }
foreach ($d in $domains) {
    foreach ($ip in (Get-DnsIps $d)) {
        if (-not $candidates.Contains($ip)) { $candidates.Add($ip) }
    }
}
if ($candidates.Count -eq 0) { Log 'ERROR: 无候选 IP（DNS 解析全部失败？）'; exit 1 }

# 当前生效节点：有固定解析用固定解析，否则用 push2 的首个 DNS 解析
$current = $pin
if (-not $current) { $current = (Get-DnsIps 'push2.eastmoney.com') | Select-Object -First 1 }

if ($current -and (Test-Node $current)) {
    Log "OK: 当前节点 $current 存活，无需处理"
    exit 0
}

Log "FAIL: 当前节点 $current 失效，候选: $($candidates -join ', ')"
foreach ($ip in $candidates) {
    if ($ip -eq $current) { continue }
    if (Test-Node $ip) {
        Set-Pin $ip
        Log "FIXED: 已切换到节点 $ip 并重写 hosts"
        exit 0
    }
}
Log 'ERROR: 所有候选节点均不可用，保留原固定解析'
exit 1
