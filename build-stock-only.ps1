$ErrorActionPreference = 'Stop'
$solDir = 'C:\Users\xiongaox\Downloads\00code\TrafficMonitorPlugins\'
$msbuild = 'D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
& $msbuild "${solDir}Plugins\Stock\Stock.vcxproj" /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solDir" /m /v:m
if ($LASTEXITCODE -ne 0) { Write-Error 'Build Stock failed.'; exit 1 }
Write-Host '[+] Stock.dll build OK'
