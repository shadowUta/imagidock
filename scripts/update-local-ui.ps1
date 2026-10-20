param([switch]$SkipBuild)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $taskRoot
if(-not $SkipBuild){
    & (Join-Path $PSScriptRoot 'build.ps1') -Target ImagiDock
    if($LASTEXITCODE -ne 0){throw 'UI build failed.'}
}
$taskPackage=Join-Path $taskRoot 'dist/ImagiDock-0.1.0-rc.1-win-x64'
$taskBinary=Join-Path $taskRoot 'build/bin/ImagiDock.exe'
foreach($taskRequired in @('ImagiDock.exe','imagidock-service.exe','Qt6Widgets.dll','platforms/qwindows.dll')){
    if(-not (Test-Path -LiteralPath (Join-Path $taskPackage $taskRequired))){throw 'Assemble the original portable runtime first.'}
}
# Same application directory keeps the existing default library and helpers.
# The public RC executable and ZIP are retained for reproducible downloads.
$taskUpdated=Join-Path $taskPackage 'ImagiDock-linear-v6.exe'
Copy-Item -LiteralPath $taskBinary -Destination $taskUpdated -Force
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs/UI_REFINEMENT.md') -Destination (Join-Path $taskPackage 'docs/UI_REFINEMENT-local.md') -Force
$taskReceipt=[ordered]@{
    version='0.1.0-rc.1+linear.20261020.6'
    entry='ImagiDock-linear-v6.exe'
    sha256=(Get-FileHash -LiteralPath $taskUpdated -Algorithm SHA256).Hash.ToLowerInvariant()
    bytes=(Get-Item -LiteralPath $taskUpdated).Length
    library='data/library'
    launcher='../../启动创想坞.cmd'
    original_release_retained=$true
}
$taskReceipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskPackage 'local-ui-build.json') -Encoding utf8
$taskReceipt | ConvertTo-Json
