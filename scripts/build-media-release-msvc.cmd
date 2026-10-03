@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

cmake --preset windows-msvc-media-release
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-msvc-media-release --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows-msvc-media-release
if errorlevel 1 exit /b %errorlevel%

echo ROOD release candidate built and tested at build\msvc-media-release
