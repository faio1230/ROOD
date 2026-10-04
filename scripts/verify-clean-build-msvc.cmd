@echo off
setlocal
set "ROOD_VERIFY_DIR=%~1"
if "%ROOD_VERIFY_DIR%"=="" set "ROOD_VERIFY_DIR=%~dp0..\build\repro-verify-%RANDOM%"
if exist "%ROOD_VERIFY_DIR%" (
  echo Choose a path that does not exist for the clean build check. 1>&2
  exit /b 1
)

call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

cmake --preset windows-msvc-media-release -B "%ROOD_VERIFY_DIR%"
if errorlevel 1 exit /b %errorlevel%
cmake --build "%ROOD_VERIFY_DIR%" --parallel 4
if errorlevel 1 exit /b %errorlevel%
ctest --test-dir "%ROOD_VERIFY_DIR%" --output-on-failure
if errorlevel 1 exit /b %errorlevel%

set "PATH=%~dp0..\build\deps\qt\6.10.3\msvc2022_64\bin;%~dp0..\build\deps\vcpkg-installed\x64-windows\bin;%~dp0..\build\deps\portaudio-msvc-install\bin;%~dp0..\build\deps\spout2-msvc-install\bin;%~dp0..\build\deps\omt-v1.0.0.16\Libraries\Winx64;%PATH%"
"%ROOD_VERIFY_DIR%\rood_deps_probe.exe"
if errorlevel 1 exit /b %errorlevel%
"%ROOD_VERIFY_DIR%\rood_pa_probe.exe" --list > "%ROOD_VERIFY_DIR%\portaudio-devices.txt"
if errorlevel 1 exit /b %errorlevel%
findstr /C:"| ASIO |" "%ROOD_VERIFY_DIR%\portaudio-devices.txt" >nul
if not errorlevel 1 (
  echo ASIO devices appeared in the WASAPI-only Release build. 1>&2
  exit /b 1
)

echo Clean source build and dependency probe passed in %ROOD_VERIFY_DIR%
