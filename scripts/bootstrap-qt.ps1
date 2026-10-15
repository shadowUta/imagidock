$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$env:TEMP=Join-Path $taskRoot '.cache'
$env:TMP=$env:TEMP
$env:PIP_CACHE_DIR=Join-Path $taskRoot '.cache/pip'
$env:PYTHONPYCACHEPREFIX=Join-Path $taskRoot '.cache/pycache'
New-Item -ItemType Directory -Force $env:TEMP | Out-Null
if(-not (Test-Path .tools/build-env/Scripts/python.exe)){python -m venv .tools/build-env}
& ./.tools/build-env/Scripts/python.exe -m pip install aqtinstall==3.3.0
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& ./.tools/build-env/Scripts/python.exe -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -m qtmultimedia qtimageformats -O .tools/qt
$taskQt=Join-Path $taskRoot '.tools/qt/6.8.3/msvc2022_64'
# aqt 3.3.0 may finish extraction then fail to decode Chinese qmake output.
# Verify every required component and use Qt's relocation mechanism explicitly.
foreach($taskRequired in @('bin/Qt6Core.dll','bin/Qt6Widgets.dll','bin/Qt6Multimedia.dll','plugins/imageformats/qwebp.dll')){
  if(-not (Test-Path -LiteralPath (Join-Path $taskQt $taskRequired))){throw "Qt installation incomplete: $taskRequired"}
}
Set-Content -LiteralPath (Join-Path $taskQt 'bin/qt.conf') -Value "[Paths]`nPrefix=.." -Encoding utf8
Write-Output 'Qt 6.8.3 components are installed inside .tools/qt.'
