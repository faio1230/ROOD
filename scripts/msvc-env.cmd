@echo off
set "VS_PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools"
if not exist "%VS_PATH%\Common7\Tools\VsDevCmd.bat" (
  echo Visual Studio 2026 C++ Build Tools were not found. 1>&2
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
