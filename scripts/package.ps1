param([switch]$SkipAdapters)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$taskPackage=Join-Path $taskRoot 'dist/ImagiDock-0.1.0-rc.1-win-x64'
$taskQt=Join-Path $taskRoot '.tools/qt/6.8.3/msvc2022_64'
$env:TEMP=Join-Path $taskRoot '.cache'
$env:TMP=$env:TEMP
$env:PATH=(Join-Path $taskQt 'bin')+';'+$env:PATH
New-Item -ItemType Directory -Force $taskPackage | Out-Null
$taskExecutables=@('ImagiDock','imagidock-service','imagidock-render','imagidock-media','imagidock-sai1')
foreach($taskName in $taskExecutables){Copy-Item -LiteralPath "build/bin/$taskName.exe" -Destination $taskPackage}
& (Join-Path $taskQt 'bin/windeployqt.exe') --release --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --dir $taskPackage (Join-Path $taskPackage 'ImagiDock.exe')
if($LASTEXITCODE -ne 0){throw 'Qt runtime deployment failed.'}
$taskVs=& 'C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe' -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$taskRedist=Get-ChildItem -LiteralPath (Join-Path $taskVs 'VC/Redist/MSVC') -Directory | Where-Object {Test-Path -LiteralPath (Join-Path $_.FullName 'x64')} | Sort-Object Name -Descending | Select-Object -First 1
$taskCrt=Get-ChildItem -LiteralPath (Join-Path $taskRedist.FullName 'x64') -Directory -Filter '*CRT' | Select-Object -First 1
Copy-Item -Path (Join-Path $taskCrt.FullName '*.dll') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskQt 'bin/Qt6Concurrent.dll') -Destination $taskPackage
Copy-Item -LiteralPath (Join-Path $taskQt 'plugins/platforms/qoffscreen.dll') -Destination (Join-Path $taskPackage 'platforms')
New-Item -ItemType Directory -Force (Join-Path $taskPackage 'translations') | Out-Null
Copy-Item -LiteralPath (Join-Path $taskQt 'translations/qtbase_zh_CN.qm') -Destination (Join-Path $taskPackage 'translations')
Set-Content -LiteralPath (Join-Path $taskPackage 'qt.conf') -Value "[Paths]`nPrefix=.`nPlugins=." -Encoding utf8
Copy-Item -LiteralPath 'helpers' -Destination $taskPackage -Recurse -Force
Copy-Item -LiteralPath 'docs' -Destination $taskPackage -Recurse -Force
foreach($taskFile in @('README.md','LICENSE.txt','THIRD_PARTY_NOTICES.md')){Copy-Item -LiteralPath $taskFile -Destination $taskPackage}
New-Item -ItemType Directory -Force (Join-Path $taskPackage 'licenses/qt-sbom') | Out-Null
Copy-Item -Path (Join-Path $taskQt 'sbom/*') -Destination (Join-Path $taskPackage 'licenses/qt-sbom')
Copy-Item -LiteralPath '.tools/filament/LICENSE' -Destination (Join-Path $taskPackage 'licenses/filament-Apache-2.0.txt')
Copy-Item -LiteralPath '.tools/assimp/assimp-5.4.3/LICENSE' -Destination (Join-Path $taskPackage 'licenses/assimp.txt')
Copy-Item -LiteralPath '.tools/zstd/zstd-1.5.7/LICENSE' -Destination (Join-Path $taskPackage 'licenses/zstd.txt')
if(-not $SkipAdapters){
  & ./.tools/adapters-env/Scripts/python.exe scripts/package-adapters.py $taskPackage
  if($LASTEXITCODE -ne 0){throw 'Portable adapters deployment failed.'}
}
& ./.tools/adapters-env/Scripts/python.exe scripts/package-licenses.py $taskPackage
if($LASTEXITCODE -ne 0){throw 'Third-party license deployment failed.'}
Write-Output "Portable directory: $taskPackage"
