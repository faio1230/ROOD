@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%
pushd "%~dp0.."
if errorlevel 1 exit /b %errorlevel%
cmake --preset windows-msvc-media-asio-test
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-msvc-media-asio-test --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows-msvc-media-asio-test
if errorlevel 1 exit /b %errorlevel%
popd
