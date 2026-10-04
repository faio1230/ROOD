@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

for %%I in ("%~dp0..") do set "REPOSITORY=%%~fI"
pushd "%REPOSITORY%"
if errorlevel 1 exit /b %errorlevel%
set "RESULT=0"

powershell -NoProfile -ExecutionPolicy Bypass -File scripts\check-privacy-deps.ps1
if errorlevel 1 goto failed

set "PRIVACY_PREFIX=%CD%\build\deps\qt\6.10.3\msvc2022_64;%CD%\build\deps\portaudio-privacy-install;%CD%\build\deps\vcpkg-privacy-installed\x64-windows;%CD%\build\deps\spout2-msvc-install"
cmake --preset windows-msvc-media-release -B build\msvc-media-privacy "-DCMAKE_PREFIX_PATH=%PRIVACY_PREFIX%"
if errorlevel 1 goto failed
cmake --build build\msvc-media-privacy --parallel 4
if errorlevel 1 goto failed
ctest --test-dir build\msvc-media-privacy --output-on-failure
if errorlevel 1 goto failed

set "PATH=%CD%\build\deps\qt\6.10.3\msvc2022_64\bin;%CD%\build\deps\vcpkg-privacy-installed\x64-windows\bin;%CD%\build\deps\portaudio-privacy-install\bin;%CD%\build\deps\spout2-msvc-install\bin;%CD%\build\deps\omt-v1.0.0.16\Libraries\Winx64;%PATH%"
build\msvc-media-privacy\rood_deps_probe.exe
if errorlevel 1 goto failed
goto cleanup

:failed
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" set "RESULT=1"
:cleanup
popd
exit /b %RESULT%
