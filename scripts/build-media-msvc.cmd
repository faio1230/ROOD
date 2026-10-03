@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

pushd "%~dp0.."
if errorlevel 1 exit /b %errorlevel%

cmake --preset windows-msvc-media-dev
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-msvc-media-dev --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows-msvc-media-dev
if errorlevel 1 exit /b %errorlevel%

set "PATH=%CD%\build\deps\vcpkg-installed\x64-windows\bin;%CD%\build\deps\spout2-msvc-install\bin;%CD%\build\deps\omt-v1.0.0.16\Libraries\Winx64;%PATH%"
"%CD%\build\msvc-media\rood_deps_probe.exe"
if errorlevel 1 exit /b %errorlevel%

popd
