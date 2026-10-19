@echo off
if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-ui.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-ui.exe"
) else (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock.exe"
)
