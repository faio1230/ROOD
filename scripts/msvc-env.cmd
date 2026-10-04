@echo off
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo Visual Studio Installer vswhere.exe was not found. 1>&2
  exit /b 1
)
set "VS_PATH="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS_PATH=%%I"
if not defined VS_PATH (
  echo Visual Studio C++ tools were not found. 1>&2
  exit /b 1
)
if not exist "%VS_PATH%\Common7\Tools\VsDevCmd.bat" (
  echo Visual Studio developer environment was not found. 1>&2
  exit /b 1
)

call "%VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
if errorlevel 1 exit /b %errorlevel%

set "PATH=%VS_PATH%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
where ninja.exe >nul 2>&1
if errorlevel 1 (
  echo Ninja was not found. 1>&2
  exit /b 1
)
