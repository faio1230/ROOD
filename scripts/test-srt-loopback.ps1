param(
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$receiverExe = Join-Path $repo 'build/msvc-media/rood_ingest.exe'
$runtimeBin = Join-Path $repo 'build/deps/vcpkg-installed/x64-windows/bin'
if (-not (Test-Path -LiteralPath $receiverExe)) {
    throw 'rood_ingest.exe was not found. Run scripts/build-media-msvc.cmd first.'
}
if (-not (Test-Path -LiteralPath $FfmpegPath)) {
    throw "FFmpeg CLI was not found: $FfmpegPath"
}

$logDir = Join-Path $repo 'build/tests/srt-loopback'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stdout = Join-Path $logDir 'receiver.stdout.txt'
$stderr = Join-Path $logDir 'receiver.stderr.txt'
Remove-Item -LiteralPath $stdout, $stderr -ErrorAction SilentlyContinue
$port = Get-Random -Minimum 20000 -Maximum 50000
$env:PATH = "$runtimeBin;$env:PATH"
$receiver = Start-Process -FilePath $receiverExe -ArgumentList @(
    '--port', "$port", '--seconds', '20', '--require-media'
) -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden -PassThru

try {
    $listening = $false
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        if ((Test-Path -LiteralPath $stdout) -and
            ((Get-Content -LiteralPath $stdout -Raw) -match 'state listening')) {
            $listening = $true
            break
        }
        $receiver.Refresh()
        if ($receiver.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if (-not $listening) {
        throw "Receiver did not start listening. See $stderr"
    }

    & $FfmpegPath -hide_banner -loglevel error `
        -re -f lavfi -i 'testsrc=size=320x180:rate=25' `
        -re -f lavfi -i 'sine=frequency=440:sample_rate=48000' `
        -re -f lavfi -i 'anullsrc=channel_layout=5.1:sample_rate=48000' `
        -t 5 -map 0:v:0 -map 1:a:0 -map 2:a:0 `
        -c:v mpeg2video -threads:v 1 -g 25 -b:v 1M `
        -c:a:0 mp2 -b:a:0 192k -ac:a:0 2 `
        -c:a:1 ac3 -b:a:1 384k -f mpegts `
        "srt://127.0.0.1:${port}?mode=caller&latency=120"
    if ($LASTEXITCODE -ne 0) { throw "FFmpeg sender failed with exit code $LASTEXITCODE" }

    $readyAgain = $false
    for ($attempt = 0; $attempt -lt 50; ++$attempt) {
        $snapshot = Get-Content -LiteralPath $stdout -Raw
        if (([regex]::Matches($snapshot, 'state listening')).Count -ge 2) {
            $readyAgain = $true
            break
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $readyAgain) { throw "Receiver did not resume listening. See $stdout" }

    & $FfmpegPath -hide_banner -loglevel error `
        -re -f lavfi -i 'testsrc=size=160x90:rate=25' `
        -re -f lavfi -i 'sine=frequency=880:sample_rate=48000' `
        -t 3 -map 0:v:0 -map 1:a:0 `
        -c:v mpeg2video -threads:v 1 -b:v 500k `
        -c:a mp2 -b:a 128k -f mpegts `
        "srt://127.0.0.1:${port}?mode=caller&latency=120"
    if ($LASTEXITCODE -ne 0) { throw "Second FFmpeg sender failed with exit code $LASTEXITCODE" }

    $receiver | Wait-Process -Timeout 25
    $receiver.Refresh()
    if ($receiver.ExitCode -ne 0) { throw "Receiver failed with exit code $($receiver.ExitCode). See $stderr" }
    $output = Get-Content -LiteralPath $stdout -Raw
    if ($output -notmatch 'track index=0 id=256 kind=video' -or
        $output -notmatch 'track index=1 id=257 kind=audio.*channels=2' -or
        $output -notmatch 'track index=2 id=258 kind=audio.*channels=6' -or
        $output -notmatch 'frame index=0.*pts=\d+' -or
        $output -notmatch 'frame index=1.*pts=\d+' -or
        $output -notmatch 'frame index=2.*pts=\d+' -or
        ([regex]::Matches($output, 'frame index=0 id=256 kind=video')).Count -lt 4 -or
        ([regex]::Matches($output, 'frame index=1 id=257 kind=audio')).Count -lt 4 -or
        ([regex]::Matches($output, 'state connected')).Count -ne 2 -or
        ([regex]::Matches($output, 'state disconnected')).Count -ne 2 -or
        $output -notmatch 'summary audioTracks=3 videoTracks=2 audioFrames=[1-9]\d* videoFrames=[1-9]\d*') {
        throw "Loopback output did not contain the expected tracks, PTS and frames. See $stdout"
    }
    Write-Host 'SRT loopback passed: video, stereo and 5.1 audio, PTS, disconnect and reconnect.'
    Write-Host "Log: $stdout"
}
finally {
    $receiver.Refresh()
    if (-not $receiver.HasExited) { Stop-Process -Id $receiver.Id -Force }
}
