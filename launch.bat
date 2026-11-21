@echo off
cd /d "%~dp0"
if not exist FileDesk.exe call build.bat
if not exist FileDesk.exe goto failed
start "" "%~dp0FileDesk.exe"
exit /b 0
:failed
echo Build failed. See README.md for C++ compiler requirements.
pause
