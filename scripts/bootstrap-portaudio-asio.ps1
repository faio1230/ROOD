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
$patch = Join-Path $repository 'patches/portaudio-v19.7.0-asio-allowlist.patch'
$expectedPortAudioCommit = '147dd722548358763a8b649b3e4b41dfffbcfbb6'
$expectedSdkHash = 'D5EBF0C20DD2C5F43771FD0C1418F4B361BF52434EE670097CFA6B3A335E2ECA'

New-Item -ItemType Directory -Force -Path $deps | Out-Null
if (-not $SdkZip) { $SdkZip = Join-Path $deps 'asiosdk.zip' }
if (-not (Test-Path -LiteralPath $SdkZip)) {
    Invoke-WebRequest -Uri 'https://www.steinberg.net/asiosdk' -OutFile $SdkZip
}
$actualSdkHash = (Get-FileHash -LiteralPath $SdkZip -Algorithm SHA256).Hash
if ($actualSdkHash -ne $expectedSdkHash) {
    throw "ASIO SDK ZIP hash mismatch: $actualSdkHash. Check the SDK version and license before updating the pin."
}
if (-not (Test-Path -LiteralPath $sdkSource)) {
    Expand-Archive -LiteralPath $SdkZip -DestinationPath (Join-Path $deps 'asiosdk-source')
}

if (-not (Test-Path -LiteralPath $source)) {
    git clone --depth 1 --branch v19.7.0 https://github.com/PortAudio/portaudio.git $source
    if ($LASTEXITCODE -ne 0) { throw 'PortAudio clone failed' }
}
$actualCommit = (git -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedPortAudioCommit) {
    throw "PortAudio source commit mismatch: $actualCommit"
}

git -C $source apply --reverse --check $patch 2>$null
if ($LASTEXITCODE -ne 0) {
    git -C $source apply --check $patch
    if ($LASTEXITCODE -ne 0) { throw 'PortAudio test patch cannot be applied' }
    git -C $source apply $patch
    if ($LASTEXITCODE -ne 0) { throw 'PortAudio test patch failed' }
}

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
