@echo off
setlocal
call "%~dp0msvc-env.cmd"
if errorlevel 1 exit /b %errorlevel%

set "REPOSITORY=%~dp0.."
set "SOURCE=%REPOSITORY%\build\deps\spout2-src"
set "BUILD=%REPOSITORY%\build\deps\spout2-msvc-build"
set "INSTALL=%REPOSITORY%\build\deps\spout2-msvc-install"
set "EXPECTED_COMMIT=c2bcc12147711d12ace7d5f08e869d774d840f8a"

if not exist "%REPOSITORY%\build\deps" mkdir "%REPOSITORY%\build\deps"
if errorlevel 1 exit /b %errorlevel%

if not exist "%SOURCE%\.git" (
  git init "%SOURCE%"
  if errorlevel 1 exit /b %errorlevel%
  git -C "%SOURCE%" fetch --depth 1 https://github.com/leadedge/Spout2.git %EXPECTED_COMMIT%
  if errorlevel 1 exit /b %errorlevel%
  git -C "%SOURCE%" checkout --detach FETCH_HEAD
  if errorlevel 1 exit /b %errorlevel%
)
for /f "delims=" %%I in ('git -C "%SOURCE%" rev-parse HEAD') do set "ACTUAL_COMMIT=%%I"
if not "%ACTUAL_COMMIT%"=="%EXPECTED_COMMIT%" (
  echo Spout2 source commit mismatch: %ACTUAL_COMMIT% 1>&2
  exit /b 1
)

cmake -S "%SOURCE%" -B "%BUILD%" -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  "-DCMAKE_INSTALL_PREFIX=%INSTALL%" ^
  -DSPOUT_BUILD_CMT=OFF -DSPOUT_BUILD_LIBRARY=ON -DSPOUT_BUILD_SPOUTDX=OFF
if errorlevel 1 exit /b %errorlevel%
cmake --build "%BUILD%" --parallel 4
if errorlevel 1 exit /b %errorlevel%
cmake --install "%BUILD%"
if errorlevel 1 exit /b %errorlevel%

echo Spout2 2.007.017 installed at %INSTALL%
