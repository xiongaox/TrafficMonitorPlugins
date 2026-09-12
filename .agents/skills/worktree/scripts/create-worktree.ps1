<#
.SYNOPSIS
    为 StockPlusPlus 创建独立的 git worktree 开发环境。

.DESCRIPTION
    1. 检查主仓库是否有未提交的代码变更（如果脏则中止）。
    2. 生成规范的 8 位 git-id，并在 "D:\Program Files (x86)\NIR\worktree\StockPlusPlus" 下创建 "main-<git-id>" 目录。
    3. 创建并检出无斜杠的分支 "main-<git-id>"（严格避免 Windows Git 缺陷）。
    4. 仅复制 Stock.dll、PluginTester.exe 及相关配置到 bin\x64\Release（不引入其他插件）。
    5. 在 Release 写入 git_id 标识供测试器界面与标题展示。
    6. 同步 lib\x64\Release 到新目录以支持后续编译。
    7. 在 Worktree 根目录下创建快捷方式，方便在外部一键启动测试器。
#>

[CmdletBinding()]
param(
    [string]$WorktreeBase = "D:\Program Files (x86)\NIR\worktree\StockPlusPlus",
    [string]$GitId = ""
)

$ErrorActionPreference = 'Stop'

# 1. 定位主仓库根目录
$repoRoot = (git rev-parse --show-toplevel 2>$null)
if (-not $repoRoot) {
    throw "当前目录不是 git 仓库！请在 StockPlusPlus 项目目录下运行本脚本。"
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
Write-Host "  StockPlusPlus Worktree Setup" -ForegroundColor Cyan
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

# 6. 初始化工作区的 Release 产物环境 (仅复制 PluginTester.exe 与 Stock.dll 相关文件，不引入其他无关插件)
$srcRelease = Join-Path $repoRoot 'bin\x64\Release'
$dstRelease = Join-Path $targetWorktreePath 'bin\x64\Release'
if (Test-Path $srcRelease) {
    Write-Host "[*] 正在同步 bin\x64\Release 运行环境 (仅限 Stock 与测试器)..." -ForegroundColor Cyan
    if (-not (Test-Path $dstRelease)) {
        New-Item -ItemType Directory -Path $dstRelease -Force | Out-Null
    }

    # 仅复制与 Stock.dll 和 PluginTester.exe 相关的产物与配置
    $stockPatterns = @('Stock.*', 'stock_trades.db*', 'PluginTester.*')
    foreach ($pat in $stockPatterns) {
        Get-ChildItem -Path $srcRelease -Filter $pat -File | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $dstRelease -Force
        }
    }

    # 写入 Git ID 标识到 git_id.txt 和 PluginTester.exe.ini，供测试器窗口展示
    $gitIdFile = Join-Path $dstRelease 'git_id.txt'
    [System.IO.File]::WriteAllText($gitIdFile, $branchName, [System.Text.Encoding]::UTF8)

    $testerIni = Join-Path $dstRelease 'PluginTester.exe.ini'
    if (Test-Path $testerIni) {
        $iniContent = [System.IO.File]::ReadAllText($testerIni, [System.Text.Encoding]::Default)
        if ($iniContent -match '\[config\]') {
            if ($iniContent -notmatch 'git_id\s*=') {
                $iniContent = $iniContent.Replace('[config]', "[config]`r`ngit_id = $branchName")
            } else {
                $iniContent = [regex]::Replace($iniContent, 'git_id\s*=.*', "git_id = $branchName")
            }
            [System.IO.File]::WriteAllText($testerIni, $iniContent, [System.Text.Encoding]::Default)
        }
    }

    Write-Host "[+] 已同步 PluginTester.exe 与 Stock.dll，并注入分支标识 [$branchName]。" -ForegroundColor Green
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

# 8. 在 Worktree 根目录下创建便捷启动快捷方式（在外面直接双击即可启动测试器）
try {
    $wsh = New-Object -ComObject WScript.Shell
    $testerExe = Join-Path $dstRelease "PluginTester.exe"

    $lnkPath1 = Join-Path $targetWorktreePath "启动测试器.lnk"
    $shortcut1 = $wsh.CreateShortcut($lnkPath1)
    $shortcut1.TargetPath = $testerExe
    $shortcut1.WorkingDirectory = $dstRelease
    $shortcut1.Description = "TrafficMonitor 插件测试器 [$branchName]"
    $shortcut1.IconLocation = "$testerExe,0"
    $shortcut1.Save()

    $lnkPath2 = Join-Path $targetWorktreePath "PluginTester.lnk"
    $shortcut2 = $wsh.CreateShortcut($lnkPath2)
    $shortcut2.TargetPath = $testerExe
    $shortcut2.WorkingDirectory = $dstRelease
    $shortcut2.Description = "TrafficMonitor 插件测试器 [$branchName]"
    $shortcut2.IconLocation = "$testerExe,0"
    $shortcut2.Save()

    Write-Host "[+] 已在工作树根目录创建快捷方式: 启动测试器.lnk & PluginTester.lnk" -ForegroundColor Green
} catch {
    Write-Warning "创建快捷方式失败: $($_.Exception.Message)"
}

Write-Host "==================================================" -ForegroundColor Green
Write-Host "[+] Worktree 创建并初始化成功！" -ForegroundColor Green
Write-Host "    路径: $targetWorktreePath" -ForegroundColor Green
Write-Host "    分支: $branchName" -ForegroundColor Green
Write-Host "    快捷方式: $(Join-Path $targetWorktreePath '启动测试器.lnk')" -ForegroundColor Green
Write-Host "    测试器: $(Join-Path $dstRelease 'PluginTester.exe')" -ForegroundColor Green
Write-Host "    插件: $(Join-Path $dstRelease 'Stock.dll')" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
