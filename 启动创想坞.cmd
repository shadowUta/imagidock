@echo off
if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v8.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v8.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v7.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v7.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v6.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v6.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v5.exe" (
    start "" "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v5.exe"
) else if exist "%~dp0dist\ImagiDock-0.1.0-rc.1-win-x64\ImagiDock-linear-v4.exe" (
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
