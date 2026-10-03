@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

cmake --preset windows-msvc-media-asio-release-test
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-msvc-media-asio-release-test --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows-msvc-media-asio-release-test
if errorlevel 1 exit /b %errorlevel%

echo ROOD ASIO release performance test built at build\msvc-media-asio-release
