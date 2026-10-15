param([string]$Target='', [switch]$ConfigureOnly)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
$env:TEMP=Join-Path $taskRoot '.cache'
$env:TMP=$env:TEMP
$env:CL=''
$env:VSLANG='1033'
& "$env:SystemRoot\System32\chcp.com" 65001 | Out-Null
$taskVswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$taskVs=& $taskVswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(-not $taskVs){throw 'MSVC x64 build tools were not found.'}
& (Join-Path $taskVs 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
Set-Location -LiteralPath $taskRoot
$taskCmake=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
# MSVC's linker still interprets absolute Unicode TEMP through the ANSI codepage.
# Every build process runs inside this workspace; a relative temp stays inside it.
$env:TEMP='.'
$env:TMP='.'
$taskNinja=Join-Path $taskVs 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
$taskQt=Join-Path $taskRoot '.tools\qt\6.8.3\msvc2022_64'
# Qt tools on Chinese Windows print native-codepage paths; qt.conf keeps relocation independent of aqt's qmake-output decoder.
Set-Content -LiteralPath (Join-Path $taskQt 'bin\qt.conf') -Value "[Paths]`nPrefix=.." -Encoding utf8
$env:PATH=(Join-Path $taskQt 'bin')+';'+$env:PATH
& $taskCmake -S . -B build -G Ninja "-DCMAKE_MAKE_PROGRAM=$taskNinja" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TRY_COMPILE_CONFIGURATION=Release -DCMAKE_C_FLAGS_DEBUG=/Od -DCMAKE_CXX_FLAGS_DEBUG=/Od "-DCMAKE_INSTALL_PREFIX=$taskRoot/dist"
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
if(-not $ConfigureOnly){
  if($Target){& $taskCmake --build build --target $Target --parallel 4}
  else{& $taskCmake --build build --parallel 4}
  exit $LASTEXITCODE
}
