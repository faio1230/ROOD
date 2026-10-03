@echo off
setlocal

call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

cmake --preset windows-msvc-dev
if errorlevel 1 exit /b %errorlevel%
cmake --build --preset windows-msvc-dev --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --preset windows-msvc-dev
if errorlevel 1 exit /b %errorlevel%

echo MSVC and Qt build completed successfully.
