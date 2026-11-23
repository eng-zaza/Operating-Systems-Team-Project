@echo off
setlocal
cd /d "%~dp0"
where cl >nul 2>nul
if not errorlevel 1 goto compile
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto missing
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%i"
if not defined VSROOT goto missing
call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
:compile
if not exist build mkdir build
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT /DUNICODE /D_UNICODE src\main.cpp /Fo:build\main.obj /Fe:FileDesk.exe /link /SUBSYSTEM:WINDOWS /MANIFEST:EMBED
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /utf-8 /MT /DUNICODE /D_UNICODE tests\store_tests.cpp /Fo:build\tests.obj /Fe:build\store_tests.exe
if errorlevel 1 exit /b 1
build\store_tests.exe
exit /b %errorlevel%
:missing
echo Install Visual Studio C++ Build Tools with Desktop development with C++.
exit /b 1
