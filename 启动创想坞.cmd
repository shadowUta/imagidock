@echo off
if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v4.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v4.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v3.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v3.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v2.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v2.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-ui.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-ui.exe"
) else (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock.exe"
)
