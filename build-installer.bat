@echo off

setlocal EnableExtensions EnableDelayedExpansion

set "WIX=C:\PROGRA~2\WiX Toolset v3.14"
set "MSI_DIR=C:\CraftRoot\msi"
set "INSTALL_DIR=C:\CraftRoot\bin"

if not exist "!WIX!\bin\candle.exe" (
    echo WiX Toolset v3 was not found: %WIX%
    exit /b 1
)
if not exist "!MSI_DIR!\make-msi.bat" (
    echo MSI scripts were not found: %MSI_DIR%
    echo Run build-all.bat first.
    exit /b 1
)
if not exist "!INSTALL_DIR!\privata.exe" (
    echo Installed Privata files were not found: %INSTALL_DIR%
    echo Run build-project.bat first.
    exit /b 1
)

pushd "!MSI_DIR!"
set "PATH=!WIX!\bin;C:\CraftRoot\dev-utils\bin;!PATH!"
cmd /d /c call "!MSI_DIR!\make-msi.bat" "!INSTALL_DIR!"
exit /b !errorlevel!
