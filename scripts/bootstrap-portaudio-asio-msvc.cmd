@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

set "REPOSITORY=%~dp0.."
set "SOURCE=%REPOSITORY%\build\deps\portaudio-asio-src"
set "SDK=%REPOSITORY%\build\deps\asiosdk-source\ASIOSDK"
set "BUILD=%REPOSITORY%\build\deps\portaudio-asio-msvc-test-build"
set "INSTALL=%REPOSITORY%\build\deps\portaudio-asio-msvc-test-install"

if not exist "%SOURCE%\CMakeLists.txt" (
  echo Run scripts\prepare-portaudio-asio.ps1 first to prepare the pinned source and ASIO SDK. 1>&2
  exit /b 1
)
if not exist "%SDK%\common\asiosys.h" (
  echo ASIO SDK 2.3.4 was not found. 1>&2
  exit /b 1
)

cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_INSTALL_PREFIX=%INSTALL%" ^
  "-DASIOSDK_ROOT_DIR=%SDK%" ^
  "-DASIOSDK_INCLUDE_DIR=%SDK%\common" ^
  -DPA_BUILD_SHARED=ON -DPA_BUILD_STATIC=OFF ^
  -DPA_USE_ASIO=ON -DPA_USE_WASAPI=ON ^
  -DPA_USE_WDMKS=OFF -DPA_USE_DS=OFF -DPA_USE_WMME=OFF ^
  -DPA_BUILD_TESTS=OFF -DPA_BUILD_EXAMPLES=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD%" --parallel 4
if errorlevel 1 exit /b %errorlevel%
cmake --install "%BUILD%"
if errorlevel 1 exit /b %errorlevel%

echo PortAudio v19.7.0 MSVC ASIO test build installed at %INSTALL%
