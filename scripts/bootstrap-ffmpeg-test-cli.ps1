$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$directory = Join-Path $repo 'build/deps/ffmpeg-test-cli'
$archive = Join-Path $directory 'ffmpeg-8.1.2-essentials_build.7z'
$exe = Join-Path $directory 'ffmpeg.exe'
$url = 'https://www.gyan.dev/ffmpeg/builds/packages/ffmpeg-8.1.2-essentials_build.7z'
$expected = 'e25b682664025d49034c981afb4bae36238a40f29a3cc1c713ad9a8b5b3528f6'

New-Item -ItemType Directory -Path $directory -Force | Out-Null
if (-not (Test-Path -LiteralPath $archive)) {
    $partial = "$archive.partial"
    Invoke-WebRequest -Uri $url -OutFile $partial -TimeoutSec 300
    $downloadHash = (Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($downloadHash -ne $expected) {
        throw "FFmpeg test CLI download hash mismatch: $downloadHash"
    }
    Move-Item -LiteralPath $partial -Destination $archive
}
$actual = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
if ($actual -ne $expected) { throw "FFmpeg test CLI archive hash mismatch: $actual" }

$sevenZipPath = (Get-Command 7z -ErrorAction SilentlyContinue | Select-Object -First 1).Source
if (-not $sevenZipPath) {
    $candidate = Join-Path ([Environment]::GetFolderPath('ProgramFiles')) '7-Zip/7z.exe'
    if (Test-Path -LiteralPath $candidate) { $sevenZipPath = $candidate }
}
if (-not $sevenZipPath) { throw '7-Zip is required to extract the FFmpeg test CLI.' }
& $sevenZipPath e -y "-o$directory" $archive ffmpeg.exe -r | Out-Null
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
    throw 'Could not extract ffmpeg.exe from the verified test archive.'
}
$version = & $exe -version | Select-Object -First 1
if ($LASTEXITCODE -ne 0 -or $version -notmatch '^ffmpeg version 8\.1\.2\b') {
    throw "Unexpected FFmpeg test CLI version: $version"
}
$protocols = & $exe -hide_banner -protocols 2>&1
if ($LASTEXITCODE -ne 0 -or ($protocols -join "`n") -notmatch '(?m)^\s*srt\s*$') {
    throw 'The FFmpeg test CLI lacks SRT support.'
}
Write-Host "Verified FFmpeg 8.1.2 test CLI with SRT: $exe"
Write-Host 'This GPLv3 test tool stays in build/deps and is not an ROOD runtime dependency.'
