# Local Build and Copy Script
$solDir = (Get-Location).Path + "\"
$msbuild = "D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"

& $msbuild utilities\utilities.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solDir" /m /v:m
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $msbuild Plugins\Stock\Stock.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir="$solDir" /m /v:m
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Copy-Item "bin\x64\Release\Stock.dll" "D:\MyDir\soft\TrafficMonitor\plugins\Stock.dll" -Force
Write-Host "[+] Local build succeeded and Stock.dll replaced."
