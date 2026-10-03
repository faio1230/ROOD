$ErrorActionPreference = 'Stop'

$repository = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $repository 'build/deps'
$zip = Join-Path $deps 'omt-v1.0.0.16.zip'
$install = Join-Path $deps 'omt-v1.0.0.16'
$library = Join-Path $install 'Libraries/Winx64/libomt.lib'
$expectedHash = 'C70E67F7E2A7ED5B4C389D99AF62796A8C9C7BE23C8DEBFAE3FD8020C1DC66B9'
$url = 'https://github.com/openmediatransport/libomtnet/releases/download/v1.0.0.16/OpenMediaTransport.Binaries.Release.v1.0.0.16.zip'

New-Item -ItemType Directory -Force -Path $deps | Out-Null
if (-not (Test-Path -LiteralPath $zip)) {
    Invoke-WebRequest -Uri $url -OutFile $zip
}
$actualHash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash
if ($actualHash -ne $expectedHash) {
    throw "OMT archive SHA-256 mismatch: $actualHash"
}
if (-not (Test-Path -LiteralPath $library)) {
    Expand-Archive -LiteralPath $zip -DestinationPath $install -Force
}
if (-not (Test-Path -LiteralPath $library)) {
    throw 'OMT Windows x64 library was not found in the archive'
}

Write-Host "OMT v1.0.0.16 libraries installed at $install"
