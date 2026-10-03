$ErrorActionPreference = 'Stop'

$repository = Split-Path -Parent $PSScriptRoot
$source = Join-Path $repository 'build/deps/portaudio-src'
$build = Join-Path $repository 'build/deps/portaudio-build'
$install = Join-Path $repository 'build/deps/portaudio-install'
$expectedCommit = '147dd722548358763a8b649b3e4b41dfffbcfbb6'

New-Item -ItemType Directory -Force -Path (Join-Path $repository 'build/deps') | Out-Null
if (-not (Test-Path -LiteralPath $source)) {
    git clone --depth 1 --branch v19.7.0 https://github.com/PortAudio/portaudio.git $source
    if ($LASTEXITCODE -ne 0) { throw 'PortAudio clone failed' }
}

$actualCommit = (git -C $source rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $expectedCommit) {
    throw "PortAudio source commit mismatch: $actualCommit"
}

$make = (Get-Command mingw32-make -ErrorAction Stop).Source
cmake -S $source -B $build -G 'MinGW Makefiles' `
    "-DCMAKE_MAKE_PROGRAM=$make" `
    '-DCMAKE_BUILD_TYPE=Release' `
    "-DCMAKE_INSTALL_PREFIX=$install" `
    '-DPA_BUILD_SHARED=ON' '-DPA_BUILD_STATIC=OFF' `
    '-DPA_USE_ASIO=OFF' '-DPA_USE_WASAPI=ON' `
    '-DPA_USE_WDMKS=OFF' '-DPA_USE_DS=OFF' '-DPA_USE_WMME=OFF' `
    '-DPA_BUILD_TESTS=OFF' '-DPA_BUILD_EXAMPLES=OFF'
if ($LASTEXITCODE -ne 0) { throw 'PortAudio configure failed' }

cmake --build $build --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'PortAudio build failed' }
cmake --install $build
if ($LASTEXITCODE -ne 0) { throw 'PortAudio install failed' }

Write-Host "PortAudio v19.7.0 installed in $install"
