@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

set "REPOSITORY=%~dp0.."
set "VCPKG_ROOT=%REPOSITORY%\build\deps\vcpkg-src"
set "EXPECTED_COMMIT=9e593bb18ea69cc5095e012465dcd675a822ed0d"

if not exist "%REPOSITORY%\build\deps" mkdir "%REPOSITORY%\build\deps"
if errorlevel 1 exit /b %errorlevel%
if not exist "%VCPKG_ROOT%\.git" (
  git clone --depth 1 --branch 2026.07.29 https://github.com/microsoft/vcpkg.git "%VCPKG_ROOT%"
  if errorlevel 1 exit /b %errorlevel%
)
for /f "delims=" %%I in ('git -C "%VCPKG_ROOT%" rev-parse HEAD') do set "ACTUAL_COMMIT=%%I"
if not "%ACTUAL_COMMIT%"=="%EXPECTED_COMMIT%" (
  echo vcpkg source commit mismatch: %ACTUAL_COMMIT% 1>&2
  exit /b 1
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
  call "%VCPKG_ROOT%\bootstrap-vcpkg.bat" -disableMetrics
  if errorlevel 1 exit /b %errorlevel%
)

set "BINARY_SOURCE_OPTION="
if /I "%ROOD_VCPKG_SOURCE_BUILD%"=="1" set "BINARY_SOURCE_OPTION=--binarysource=clear"
"%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows --x-install-root "%REPOSITORY%\build\deps\vcpkg-installed" --no-print-usage %BINARY_SOURCE_OPTION%
if errorlevel 1 exit /b %errorlevel%

echo Pinned libsrt and LGPL-target FFmpeg development libraries installed.
