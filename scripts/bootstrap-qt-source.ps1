$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$directory = Join-Path $repo 'build/deps/qt-source'
$archive = Join-Path $directory 'qtbase-everywhere-src-6.10.3.tar.xz'
$url = 'https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz'
$expected = '383dc907816338f0cba72088a524c07458dfc69ce684ca9132fcc4fe91c24b0b'

New-Item -ItemType Directory -Path $directory -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    $partial = "$archive.partial"
    Invoke-WebRequest -Uri $url -OutFile $partial -TimeoutSec 120
    $downloadHash = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($downloadHash -ne $expected) {
        throw "Downloaded Qt source hash mismatch: $downloadHash"
    }
    Move-Item -LiteralPath $partial -Destination $archive
}
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) {
    throw "Qt source archive hash mismatch: $actual"
}
$licenseDir = Join-Path $directory 'LICENSES'
& tar -xf $archive -C $directory --strip-components=1 `
    qtbase-everywhere-src-6.10.3/LICENSES
if ($LASTEXITCODE -ne 0) { throw 'Could not extract Qt license texts.' }
if (-not (Test-Path -LiteralPath (Join-Path $licenseDir 'LGPL-3.0-only.txt')) -or
    -not (Test-Path -LiteralPath (Join-Path $licenseDir 'GPL-3.0-only.txt')) -or
    (Get-ChildItem -LiteralPath $licenseDir -File).Count -ne 38) {
    throw 'Qt license texts were not found in the verified source archive.'
}
Write-Host "Qt 6.10.3 qtbase source and license texts verified: $directory"
