Continue = 'Stop'
 = (Resolve-Path ).Path + '\'
 = 'D:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
&  ${solDir}Plugins\Stock\Stock.vcxproj /p:Configuration=Release /p:Platform=x64 /p:SolutionDir=" /m /v:m
if ($LASTEXITCODE -ne 0) { Write-Error 'Build Stock failed.'; exit 1 }
Write-Host '[+] Stock.dll build OK'
