@echo off
setlocal
pushd "%~dp0.."
if errorlevel 1 exit /b %errorlevel%
set "PATH=%CD%\build\deps\portaudio-msvc-install\bin;%PATH%"
"%CD%\build\msvc-media\rood_pa_probe.exe" --list
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
