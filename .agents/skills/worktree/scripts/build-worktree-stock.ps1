<#
.SYNOPSIS
    在指定的 Worktree 目录下编译 Stock.dll，严格不自启动 PluginTester.exe。

.DESCRIPTION
    1. 定位 MSBuild。
    2. 对指定 Worktree 目录的 Stock 插件进行 Release x64 增量编译。
    3. 校验产物生成情况。
    4. 明确禁止自动拉起 PluginTester.exe，提示用户手动启动调试。
#>

[CmdletBinding()]
param(
    [string]$WorktreePath = ""
)

$ErrorActionPreference = 'Stop'

# 1. 确定目标工作目录
if (-not $WorktreePath) {
    $WorktreePath = (Get-Location).Path
} else {
    $WorktreePath = [System.IO.Path]::GetFullPath($WorktreePath)
}

$solDir = $WorktreePath.TrimEnd('\') + '\'
$stockProj = Join-Path $WorktreePath 'Plugins\Stock\Stock.vcxproj'
$utilitiesProj = Join-Path $WorktreePath 'utilities\utilities.vcxproj'
$targetDll = Join-Path $WorktreePath 'bin\x64\Release\Stock.dll'
$testerExe = Join-Path $WorktreePath 'bin\x64\Release\PluginTester.exe'

if (-not (Test-Path $stockProj)) {
    throw "未在路径 '$WorktreePath' 找到 Plugins\Stock\Stock.vcxproj，请确认传入了正确的 worktree 目录！"
}

# 2. 定位 MSBuild
function Get-MSBuildPath {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath
        if ($vsPath) {
            $candidate = Join-Path $vsPath 'MSBuild\Current\Bin\MSBuild.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    $fallbacks = @(
        'D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe'
    )
    foreach ($f in $fallbacks) { if (Test-Path $f) { return $f } }
    throw 'MSBuild.exe 未找到，请检查 Visual Studio Build Tools 安装。'
}

$msbuild = Get-MSBuildPath

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  TrafficMonitorPlugins Worktree Stock Build      " -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "[*] Worktree: $WorktreePath" -ForegroundColor Gray
Write-Host "[*] MSBuild : $msbuild" -ForegroundColor Gray

# 3. 检查是否有 PluginTester 锁死 dll
$lockingProcs = Get-Process -Name 'PluginTester' -ErrorAction SilentlyContinue
if ($lockingProcs) {
    Write-Warning "检测到 PluginTester 进程正在运行，可能会锁定 Stock.dll 导致链接失败 (LNK1104)。"
    Write-Warning "如遇编译报错，请先关闭测试器。"
}

# 4. 准备 SolutionDir 参数（路径含空格时末尾反斜杠需转义，防止吞掉后续参数）
$solDirArg = "/p:SolutionDir=`"$($WorktreePath.TrimEnd('\'))\\`""

# 5. 编译 utilities (如 utilities.lib 不存在时先编译)
$utilLib = Join-Path $WorktreePath 'lib\x64\Release\utilities.lib'
if (-not (Test-Path $utilLib)) {
    Write-Host "[*] 编译基础依赖库 utilities.vcxproj ..." -ForegroundColor Cyan
    & $msbuild $utilitiesProj /nologo /p:Configuration=Release /p:Platform=x64 $solDirArg /m /v:m
    if ($LASTEXITCODE -ne 0) { throw "编译 utilities.vcxproj 失败！" }
}

# 6. 编译 Stock.vcxproj
Write-Host "[*] 正在编译 Stock.vcxproj (Release|x64) ..." -ForegroundColor Cyan
$buildStartTime = Get-Date
& $msbuild $stockProj /nologo /p:Configuration=Release /p:Platform=x64 $solDirArg /m /v:m
if ($LASTEXITCODE -ne 0) {
    throw "编译 Stock.vcxproj 失败！"
}

# 6. 校验输出产物
if (-not (Test-Path $targetDll)) {
    throw "产物未找到: $targetDll"
}
$item = Get-Item $targetDll
$hash = (Get-FileHash -Path $targetDll -Algorithm SHA256).Hash
$sizeKB = [math]::Round($item.Length / 1KB, 1)

Write-Host "==================================================" -ForegroundColor Green
Write-Host "[+] Stock.dll 编译成功！" -ForegroundColor Green
Write-Host "    产物路径: $targetDll ($sizeKB KB)" -ForegroundColor Green
Write-Host "    修改时间: $($item.LastWriteTime)" -ForegroundColor Green
Write-Host "    SHA256  : $hash" -ForegroundColor Green
Write-Host ""
Write-Host "【重要提示】" -ForegroundColor Yellow
Write-Host "根据项目规范，Agent 严格不会自动启动测试器。" -ForegroundColor Yellow
Write-Host "请由用户手动双击或在终端启动测试器进行调试：" -ForegroundColor Cyan
Write-Host ">>> $testerExe" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Green
