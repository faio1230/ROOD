param(
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe',
    [string]$ReceiverExe = '',
    [string]$LogName = 'srt-abrupt-disconnect'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $ReceiverExe) {
    $ReceiverExe = Join-Path $repo 'build/msvc-media-release/rood_ingest.exe'
}
if (-not (Test-Path -LiteralPath $FfmpegPath) -or
    -not (Test-Path -LiteralPath $ReceiverExe)) {
    throw 'Build the media Release preset and provide an FFmpeg executable.'
}
if ($LogName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$') {
    throw 'LogName must contain only letters, digits, underscores and hyphens.'
}

$logDir = Join-Path $repo "build/tests/$LogName"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$receiverStdout = Join-Path $logDir 'receiver.stdout.txt'
$receiverStderr = Join-Path $logDir 'receiver.stderr.txt'
$firstStdout = Join-Path $logDir 'first-sender.stdout.txt'
$firstStderr = Join-Path $logDir 'first-sender.stderr.txt'
$runtimeBins = @(
    'build/deps/vcpkg-installed/x64-windows/bin'
    'build/deps/portaudio-msvc-install/bin'
    'build/deps/spout2-msvc-install/bin'
    'build/deps/omt-v1.0.0.16/Libraries/Winx64'
) | ForEach-Object { Join-Path $repo $_ }
$env:PATH = ($runtimeBins -join ';') + ';' + $env:PATH
$port = Get-Random -Minimum 20000 -Maximum 50000
$url = "srt://127.0.0.1:${port}?mode=caller&latency=120"
$receiver = $null
$firstSender = $null

function Wait-ReceiverPattern([string]$pattern, [int]$count, [int]$timeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($timeoutSeconds)
    do {
        if (Test-Path -LiteralPath $receiverStdout) {
            $output = [string](Get-Content -LiteralPath $receiverStdout -Raw)
            if (([regex]::Matches($output, $pattern)).Count -ge $count) { return }
        }
        $receiver.Refresh()
        if ($receiver.HasExited) { break }
        Start-Sleep -Milliseconds 100
    } while ((Get-Date) -lt $deadline)
    throw "Receiver did not report $count occurrence(s) of '$pattern'. See $receiverStdout"
}

try {
    $receiver = Start-Process -FilePath $ReceiverExe `
        -ArgumentList @('--port', "$port", '--seconds', '35', '--require-media') `
        -RedirectStandardOutput $receiverStdout -RedirectStandardError $receiverStderr `
        -WindowStyle Hidden -PassThru
    Wait-ReceiverPattern 'state listening' 1 6

    $firstArgs = @(
        '-hide_banner', '-loglevel', 'error',
        '-re', '-f', 'lavfi', '-i', 'testsrc=size=160x90:rate=25',
        '-re', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
        '-t', '60', '-map', '0:v:0', '-map', '1:a:0',
        '-c:v', 'mpeg2video', '-threads:v', '1', '-c:a', 'mp2',
        '-f', 'mpegts', $url
    )
    for ($attempt = 1; $attempt -le 8; ++$attempt) {
        $firstSender = Start-Process -FilePath $FfmpegPath -ArgumentList $firstArgs `
            -RedirectStandardOutput $firstStdout -RedirectStandardError $firstStderr `
            -WindowStyle Hidden -PassThru
        Start-Sleep -Milliseconds 750
        $firstSender.Refresh()
        if (-not $firstSender.HasExited) { break }
        if ($attempt -eq 8) {
            throw "First sender could not connect after eight attempts. See $firstStderr"
        }
        Start-Sleep -Milliseconds 500
    }
    Wait-ReceiverPattern 'frame index=1 id=257 kind=audio' 1 12
    $firstSender.Refresh()
    if ($firstSender.HasExited) {
        throw "First sender exited before interruption. See $firstStderr"
    }
    Stop-Process -Id $firstSender.Id
    $firstSender | Wait-Process -Timeout 5
    Wait-ReceiverPattern 'state listening' 2 12

    $secondArgs = @(
        '-hide_banner', '-loglevel', 'error',
        '-re', '-f', 'lavfi', '-i', 'testsrc=size=160x90:rate=25',
        '-re', '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000',
        '-t', '3', '-map', '0:v:0', '-map', '1:a:0',
        '-c:v', 'mpeg2video', '-threads:v', '1', '-c:a', 'mp2',
        '-f', 'mpegts', $url
    )
    $senderSucceeded = $false
    for ($attempt = 1; $attempt -le 5; ++$attempt) {
        & $FfmpegPath @secondArgs
        if ($LASTEXITCODE -eq 0) {
            $senderSucceeded = $true
            break
        }
        Start-Sleep -Milliseconds 500
    }
    if (-not $senderSucceeded) { throw 'The second sender could not connect.' }

    $receiver | Wait-Process -Timeout 40
    $receiver.Refresh()
    if ($null -ne $receiver.ExitCode -and $receiver.ExitCode -ne 0) {
        throw "Receiver failed with exit code $($receiver.ExitCode). See $receiverStderr"
    }
    $output = Get-Content -LiteralPath $receiverStdout -Raw
    if (([regex]::Matches($output, 'state connected')).Count -ne 2 -or
        ([regex]::Matches($output, 'state disconnected')).Count -ne 2 -or
        ([regex]::Matches($output, 'frame index=0 id=256 kind=video pts=\d+')).Count -lt 2 -or
        ([regex]::Matches($output, 'frame index=1 id=257 kind=audio pts=\d+')).Count -lt 2 -or
        $output -notmatch 'summary audioTracks=2 videoTracks=2 audioFrames=[1-9]\d* videoFrames=[1-9]\d*') {
        throw "Abrupt-disconnect recovery failed. See $receiverStdout"
    }
    Write-Host "Abrupt SRT sender loss and reconnect passed. Log: $receiverStdout"
} finally {
    if ($firstSender) {
        $firstSender.Refresh()
        if (-not $firstSender.HasExited) { Stop-Process -Id $firstSender.Id }
    }
    if ($receiver) {
        $receiver.Refresh()
        if (-not $receiver.HasExited) { Stop-Process -Id $receiver.Id }
    }
}
