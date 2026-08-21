@echo off

setlocal

set "SOURCE_DIR=C:\Privata-src"
set "NETWORK_SOURCE=\\advance2000.com\Data\FolderRedirection\kblank\Documents\GitHub\Privata"
set "BUILD_DIR=C:\CraftRoot\build\privata"
set "CMAKE_EXE=C:\CraftRoot\dev-utils\bin\cmake.exe"
set "WIX=C:\PROGRA~2\WiX Toolset v3.14"
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

if not exist "%CMAKE_EXE%" (
    echo CMake was not found: %CMAKE_EXE%
    exit /b 1
)
if not exist "%BUILD_DIR%\build.ninja" (
    echo Build directory is not configured: %BUILD_DIR%
    echo Run build-all.bat first.
    exit /b 1
)
if not exist "%VCVARS%" (
    echo Visual Studio environment script was not found: %VCVARS%
    exit /b 1
)
if not exist "%WIX%\bin\candle.exe" (
    echo WiX Toolset v3 was not found: %WIX%
    exit /b 1
)
if not exist "%SOURCE_DIR%" mkdir "%SOURCE_DIR%"

robocopy "%NETWORK_SOURCE%" "%SOURCE_DIR%" /E /XD .git /NFL /NDL /NJH /NJS
if errorlevel 8 (
    echo Source synchronization failed.
    exit /b %errorlevel%
)

copy /Y "%NETWORK_SOURCE%\VERSION.cmake" "%SOURCE_DIR%\VERSION.cmake" >nul
if errorlevel 1 (
    echo Could not synchronize VERSION.cmake.
    exit /b %errorlevel%
)

pushd C:\CraftRoot
call "%VCVARS%"
if errorlevel 1 exit /b %errorlevel%
set "PATH=%WIX%\bin;C:\CraftRoot\dev-utils\bin;%PATH%"

"%CMAKE_EXE%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
    -DCMAKE_INSTALL_PREFIX=C:/CraftRoot ^
    -DCMAKE_PREFIX_PATH=C:/CraftRoot ^
    -DBUILD_SHARED_LIBS=ON ^
    -DBUILD_WITH_WEBENGINE=ON ^
    -DBUILD_WIN_MSI=ON ^
    "-DSVG_CONVERTER=C:/Program Files/Inkscape/bin/inkscape.com" ^
    -B "%BUILD_DIR%" ^
    -S "%SOURCE_DIR%"
if errorlevel 1 exit /b %errorlevel%

"%CMAKE_EXE%" --build "%BUILD_DIR%" --parallel
if errorlevel 1 exit /b %errorlevel%

echo Stopping running Privata processes before install...
taskkill /IM privata.exe /T /F >nul 2>&1
taskkill /IM privatacmd.exe /T /F >nul 2>&1
for /f "tokens=2 delims=," %%P in ('tasklist /m privata_csync.dll /fo csv /nh 2^>nul') do (
    taskkill /PID %%~P /T /F >nul 2>&1
)

"%CMAKE_EXE%" --install "%BUILD_DIR%"
exit /b %errorlevel%
