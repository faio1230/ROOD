param(
    [string]$SdkZip = ''
)

$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $repository 'build/deps'
$source = Join-Path $deps 'portaudio-asio-src'
$build = Join-Path $deps 'portaudio-asio-test-build'
$install = Join-Path $deps 'portaudio-asio-test-install'
$sdkSource = Join-Path $deps 'asiosdk-source/ASIOSDK'

& (Join-Path $PSScriptRoot 'prepare-portaudio-asio.ps1') -SdkZip $SdkZip

$make = (Get-Command mingw32-make -ErrorAction Stop).Source
cmake -S $source -B $build -G 'MinGW Makefiles' `
    "-DCMAKE_MAKE_PROGRAM=$make" `
    '-DCMAKE_BUILD_TYPE=Release' `
    "-DCMAKE_INSTALL_PREFIX=$install" `
    "-DASIOSDK_ROOT_DIR=$sdkSource" `
    "-DASIOSDK_INCLUDE_DIR=$(Join-Path $sdkSource 'common')" `
    '-DPA_BUILD_SHARED=ON' '-DPA_BUILD_STATIC=OFF' `
    '-DPA_USE_ASIO=ON' '-DPA_USE_WASAPI=ON' `
    '-DPA_USE_WDMKS=OFF' '-DPA_USE_DS=OFF' '-DPA_USE_WMME=OFF' `
    '-DPA_BUILD_TESTS=OFF' '-DPA_BUILD_EXAMPLES=OFF'
if ($LASTEXITCODE -ne 0) { throw 'PortAudio ASIO configure failed' }

cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'PortAudio ASIO build failed' }
cmake --install $build
if ($LASTEXITCODE -ne 0) { throw 'PortAudio ASIO install failed' }

Write-Host "PortAudio ASIO test build installed in $install"
Write-Host 'Set ROOD_ASIO_ONLY to one driver name before enumerating devices.'
