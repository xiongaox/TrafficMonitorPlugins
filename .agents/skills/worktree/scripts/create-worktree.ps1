<#
.SYNOPSIS
    为 TrafficMonitorPlugins 创建独立的 git worktree 开发环境。

.DESCRIPTION
    1. 检查主仓库是否有未提交的代码变更（如果脏则中止）。
    2. 生成规范的 8 位 git-id，并在 "D:\Program Files (x86)\NIR\worktree\TrafficMonitorPlugins" 下创建 "main-<git-id>" 目录。
    3. 创建并检出无斜杠的分支 "main-<git-id>"（严格避免 Windows Git 缺陷）。
    4. 复制 bin\x64\Release（含 PluginTester.exe、Stock.dll、ini等）与 lib\x64\Release 到新目录，确保调试与编译环境立即可用。
#>

[CmdletBinding()]
param(
    [string]$WorktreeBase = "D:\Program Files (x86)\NIR\worktree\TrafficMonitorPlugins",
    [string]$GitId = ""
)

$ErrorActionPreference = 'Stop'

# 1. 定位主仓库根目录
$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) {
    throw "当前目录不是 git 仓库！请在 TrafficMonitorPlugins 项目目录下运行本脚本。"
}
$repoRoot = [System.IO.Path]::GetFullPath($repoRoot)

# 2. 检查本地是否有未提交的改动 (Dirty Check)
$dirtyStatus = git -C "$repoRoot" status --porcelain
if ($dirtyStatus) {
    Write-Host "================================================================" -ForegroundColor Red
    Write-Host "[ERROR] 检测到本地还有未提交的改动！" -ForegroundColor Red
    Write-Host "请先保存并提交本地 git 改动（或执行 git stash）后再进行 worktree 操作。" -ForegroundColor Yellow
    Write-Host "================================================================" -ForegroundColor Red
    git -C "$repoRoot" status --short
    exit 1
}

# 3. 确定 Git ID 与分支名称 (必须无斜杠，如 main-02491967)
if (-not $GitId) {
    $commitShort = (git -C "$repoRoot" rev-parse --short=8 HEAD).Trim()
    $candidateBranch = "main-$commitShort"
    
    # 检查分支或目录是否已存在，若存在则生成唯一 8 位十六进制 ID
    $branchExists = git -C "$repoRoot" rev-parse --verify --quiet "refs/heads/$candidateBranch"
    $targetDirCandidate = Join-Path $WorktreeBase $candidateBranch
    
    if ($branchExists -or (Test-Path $targetDirCandidate)) {
        $uniqueHex = [System.Guid]::NewGuid().ToString("N").Substring(0, 8)
        $GitId = $uniqueHex
    } else {
        $GitId = $commitShort
    }
}

$branchName = "main-$GitId"
$targetWorktreePath = Join-Path $WorktreeBase $branchName

# 确保无斜杠约束
if ($branchName -match '/') {
    throw "分支名称严禁包含 '/' 字符，以规避 Windows Git 分支删除漏洞！当前分支名: $branchName"
}

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  TrafficMonitorPlugins Worktree Setup" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "[*] 仓库根目录: $repoRoot" -ForegroundColor Gray
Write-Host "[*] 目标分支  : $branchName" -ForegroundColor Gray
Write-Host "[*] 目标路径  : $targetWorktreePath" -ForegroundColor Gray

# 4. 创建基目录
if (-not (Test-Path $WorktreeBase)) {
    New-Item -ItemType Directory -Path $WorktreeBase -Force | Out-Null
}

# 5. 执行 git worktree add
Write-Host "[*] 正在执行 git worktree add ..." -ForegroundColor Cyan
git -C "$repoRoot" worktree add -b "$branchName" "$targetWorktreePath" HEAD
if ($LASTEXITCODE -ne 0) {
    throw "git worktree add 执行失败！"
}

# 6. 初始化工作区的 Release 产物环境 (PluginTester.exe, Stock.dll, 配置文件等)
$srcRelease = Join-Path $repoRoot 'bin\x64\Release'
$dstRelease = Join-Path $targetWorktreePath 'bin\x64\Release'
if (Test-Path $srcRelease) {
    Write-Host "[*] 正在同步 bin\x64\Release 运行环境..." -ForegroundColor Cyan
    if (-not (Test-Path $dstRelease)) {
        New-Item -ItemType Directory -Path $dstRelease -Force | Out-Null
    }
    Copy-Item -Path "$srcRelease\*" -Destination $dstRelease -Recurse -Force
    Write-Host "[+] 已同步 PluginTester.exe、Stock.dll 及测试器运行配置。" -ForegroundColor Green
} else {
    Write-Warning "主仓库中未找到 bin\x64\Release 目录！"
}

# 7. 同步 lib 静态库目录 (Stock 链接依赖 utilities.lib)
$srcLib = Join-Path $repoRoot 'lib\x64\Release'
$dstLib = Join-Path $targetWorktreePath 'lib\x64\Release'
if (Test-Path $srcLib) {
    Write-Host "[*] 正在同步 lib\x64\Release 依赖库..." -ForegroundColor Cyan
    if (-not (Test-Path $dstLib)) {
        New-Item -ItemType Directory -Path $dstLib -Force | Out-Null
    }
    Copy-Item -Path "$srcLib\*" -Destination $dstLib -Recurse -Force
    Write-Host "[+] 已同步 utilities.lib 依赖库。" -ForegroundColor Green
}

Write-Host "==================================================" -ForegroundColor Green
Write-Host "[+] Worktree 创建并初始化成功！" -ForegroundColor Green
Write-Host "    路径: $targetWorktreePath" -ForegroundColor Green
Write-Host "    分支: $branchName" -ForegroundColor Green
Write-Host "    测试器: $(Join-Path $dstRelease 'PluginTester.exe')" -ForegroundColor Green
Write-Host "    插件: $(Join-Path $dstRelease 'Stock.dll')" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
