@echo off

call "%~dp0build-project.bat"
if errorlevel 1 exit /b %errorlevel%

call "%~dp0build-installer.bat"
exit /b %errorlevel%
