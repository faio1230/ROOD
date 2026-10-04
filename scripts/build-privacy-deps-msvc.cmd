@echo off
setlocal

rem Build PortAudio and vcpkg media dependencies through a generic drive path.
rem The drive maps to this checkout, so all files remain under the workspace.
rem Run the pinned bootstrap scripts first to obtain source checkouts and vcpkg.
if exist R:\ (
  echo R: is already in use. Choose a machine with a free R: drive. 1>&2
  exit /b 1
)
for %%I in ("%~dp0..") do set "REPOSITORY=%%~fI"
if not exist "%REPOSITORY%\build\deps\portaudio-src\.git" (
  echo PortAudio source is missing. Run scripts\bootstrap-portaudio-msvc.cmd first. 1>&2
  exit /b 1
)
if not exist "%REPOSITORY%\build\deps\vcpkg-src\vcpkg.exe" (
  echo vcpkg is missing. Run scripts\bootstrap-media-deps.cmd first. 1>&2
  exit /b 1
)

subst R: "%REPOSITORY%"
if errorlevel 1 exit /b %errorlevel%
set "RESULT=0"
pushd R:\
if errorlevel 1 goto failed
set "PUSHED=1"
call R:\scripts\msvc-env.cmd
if errorlevel 1 goto failed

for /f "delims=" %%I in ('git -C R:\build\deps\portaudio-src rev-parse HEAD') do set "PORTAUDIO_COMMIT=%%I"
if not "%PORTAUDIO_COMMIT%"=="147dd722548358763a8b649b3e4b41dfffbcfbb6" (
  echo PortAudio source commit mismatch. 1>&2
  goto failed
)
for /f "delims=" %%I in ('git -C R:\build\deps\vcpkg-src rev-parse HEAD') do set "VCPKG_COMMIT=%%I"
if not "%VCPKG_COMMIT%"=="9e593bb18ea69cc5095e012465dcd675a822ed0d" (
  echo vcpkg source commit mismatch. 1>&2
  goto failed
)

cmake -S R:\build\deps\portaudio-src -B R:\build\deps\portaudio-privacy-build -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_INSTALL_PREFIX=R:\build\deps\portaudio-privacy-install ^
  -DPA_BUILD_SHARED=ON -DPA_BUILD_STATIC=OFF ^
  -DPA_USE_ASIO=OFF -DPA_USE_WASAPI=ON ^
  -DPA_USE_WDMKS=OFF -DPA_USE_DS=OFF -DPA_USE_WMME=OFF ^
  -DPA_BUILD_TESTS=OFF -DPA_BUILD_EXAMPLES=OFF
if errorlevel 1 goto failed
cmake --build R:\build\deps\portaudio-privacy-build --parallel 4
if errorlevel 1 goto failed
cmake --install R:\build\deps\portaudio-privacy-build
if errorlevel 1 goto failed

set "VCPKG_ROOT=R:\build\deps\vcpkg-src"
R:\build\deps\vcpkg-src\vcpkg.exe install --triplet x64-windows ^
  --x-install-root R:\build\deps\vcpkg-privacy-installed ^
  --x-buildtrees-root R:\build\deps\vcpkg-privacy-buildtrees ^
  --x-packages-root R:\build\deps\vcpkg-privacy-packages ^
  --binarysource=clear --clean-after-build --no-print-usage
if errorlevel 1 goto failed
powershell -NoProfile -ExecutionPolicy Bypass -File R:\scripts\check-privacy-deps.ps1
if errorlevel 1 goto failed
goto cleanup

:failed
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" set "RESULT=1"
:cleanup
if defined PUSHED popd
subst R: /D
if errorlevel 1 if "%RESULT%"=="0" set "RESULT=1"
exit /b %RESULT%
