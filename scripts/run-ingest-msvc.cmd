@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b %errorlevel%
set "PATH=%CD%\build\deps\vcpkg-installed\x64-windows\bin;%CD%\build\deps\portaudio-msvc-install\bin;%PATH%"
"%CD%\build\msvc-media\rood_ingest.exe" %*
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
