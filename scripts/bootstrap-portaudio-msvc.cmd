@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

set "REPOSITORY=%~dp0.."
set "SOURCE=%REPOSITORY%\build\deps\portaudio-src"
set "BUILD=%REPOSITORY%\build\deps\portaudio-msvc-build"
set "INSTALL=%REPOSITORY%\build\deps\portaudio-msvc-install"
set "EXPECTED_COMMIT=147dd722548358763a8b649b3e4b41dfffbcfbb6"

if not exist "%REPOSITORY%\build\deps" mkdir "%REPOSITORY%\build\deps"
if errorlevel 1 exit /b %errorlevel%

if not exist "%SOURCE%\.git" (
  git clone --depth 1 --branch v19.7.0 https://github.com/PortAudio/portaudio.git "%SOURCE%"
  if errorlevel 1 exit /b %errorlevel%
)

for /f "delims=" %%I in ('git -C "%SOURCE%" rev-parse HEAD') do set "ACTUAL_COMMIT=%%I"
if not "%ACTUAL_COMMIT%"=="%EXPECTED_COMMIT%" (
  echo PortAudio source commit mismatch: %ACTUAL_COMMIT% 1>&2
  exit /b 1
)

cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_INSTALL_PREFIX=%INSTALL%" ^
  -DPA_BUILD_SHARED=ON -DPA_BUILD_STATIC=OFF ^
  -DPA_USE_ASIO=OFF -DPA_USE_WASAPI=ON ^
  -DPA_USE_WDMKS=OFF -DPA_USE_DS=OFF -DPA_USE_WMME=OFF ^
  -DPA_BUILD_TESTS=OFF -DPA_BUILD_EXAMPLES=OFF
if errorlevel 1 exit /b %errorlevel%

cmake --build "%BUILD%" --parallel 4
if errorlevel 1 exit /b %errorlevel%
cmake --install "%BUILD%"
if errorlevel 1 exit /b %errorlevel%

echo PortAudio v19.7.0 MSVC WASAPI build installed at %INSTALL%
