param(
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe',
    [int]$AudioDevice = -1,
    [switch]$WasapiExclusive,
    [string]$ReceiverExe = '',
    [string]$PortAudioBin = '',
    [int]$AudioChannels = 2,
    [int]$AudioRate = 48000,
    [string[]]$Routes = @('257:0:0', '258:5:1'),
    [string]$AsioOnly = '',
    [string]$SpoutName = '',
    [string]$OmtName = '',
    [int]$FirstSeconds = 5,
    [int]$ReceiverSeconds = 20
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $ReceiverExe) { $ReceiverExe = Join-Path $repo 'build/msvc-media/rood_ingest.exe' }
$runtimeBin = Join-Path $repo 'build/deps/vcpkg-installed/x64-windows/bin'
$spoutBin = Join-Path $repo 'build/deps/spout2-msvc-install/bin'
$omtBin = Join-Path $repo 'build/deps/omt-v1.0.0.16/Libraries/Winx64'
if (-not $PortAudioBin) { $PortAudioBin = Join-Path $repo 'build/deps/portaudio-msvc-install/bin' }
if (-not (Test-Path -LiteralPath $ReceiverExe)) {
    throw 'rood_ingest.exe was not found. Run scripts/build-media-msvc.cmd first.'
}
if (-not (Test-Path -LiteralPath $FfmpegPath)) {
    throw "FFmpeg CLI was not found: $FfmpegPath"
}
if ($FirstSeconds -lt 3 -or $FirstSeconds -gt 3600 -or
    $ReceiverSeconds -lt ($FirstSeconds + 10) -or $ReceiverSeconds -gt 3700) {
    throw 'ReceiverSeconds must be at least FirstSeconds + 10.'
}

$logDir = Join-Path $repo 'build/tests/srt-loopback'
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stdout = Join-Path $logDir 'receiver.stdout.txt'
$stderr = Join-Path $logDir 'receiver.stderr.txt'
Remove-Item -LiteralPath $stdout, $stderr -ErrorAction SilentlyContinue
$port = Get-Random -Minimum 20000 -Maximum 50000
$env:PATH = "$runtimeBin;$PortAudioBin;$spoutBin;$omtBin;$env:PATH"
if ($AsioOnly) { $env:ROOD_ASIO_ONLY = $AsioOnly }
$receiverArgs = @('--port', "$port", '--seconds', "$ReceiverSeconds", '--require-media')
if ($AudioDevice -ge 0) {
    $receiverArgs += @('--audio-device', "$AudioDevice", '--audio-channels', "$AudioChannels",
                       '--audio-rate', "$AudioRate", '--audio-delay', '250')
    foreach ($route in $Routes) { $receiverArgs += @('--route', $route) }
    if ($WasapiExclusive) { $receiverArgs += '--wasapi-exclusive' }
}
if ($SpoutName) { $receiverArgs += @('--spout', $SpoutName) }
if ($OmtName) {
    $receiverArgs += @('--omt', $OmtName, '--omt-channels', '2',
                       '--omt-route', '257:0:0', '--omt-route', '258:5:1')
}
$receiver = Start-Process -FilePath $ReceiverExe -ArgumentList $receiverArgs `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
$spoutProbe = $null
if ($SpoutName) {
    $probeExe = Join-Path (Split-Path -Parent $ReceiverExe) 'rood_spout_probe.exe'
    if (-not (Test-Path -LiteralPath $probeExe)) {
        throw 'rood_spout_probe.exe was not found. Run scripts/build-media-msvc.cmd first.'
    }
    $probeStdout = Join-Path $logDir 'spout-probe.stdout.txt'
    $probeStderr = Join-Path $logDir 'spout-probe.stderr.txt'
    Remove-Item -LiteralPath $probeStdout, $probeStderr -ErrorAction SilentlyContinue
    $spoutProbe = Start-Process -FilePath $probeExe -ArgumentList @($SpoutName, '16') `
        -RedirectStandardOutput $probeStdout -RedirectStandardError $probeStderr `
        -WindowStyle Hidden -PassThru
}
$omtProbe = $null

function Invoke-TestSender {
    param([string[]]$SenderArgs)
    for ($attempt = 1; $attempt -le 3; ++$attempt) {
        & $FfmpegPath @SenderArgs
        if ($LASTEXITCODE -eq 0) { return }
        if ($attempt -lt 3) {
            Write-Warning "SRT sender connection failed (attempt $attempt); retrying."
            Start-Sleep -Seconds 1
        }
    }
    throw 'FFmpeg sender failed after three connection attempts.'
}

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
    if ($OmtName) {
        $probeExe = Join-Path (Split-Path -Parent $ReceiverExe) 'rood_omt_probe.exe'
        if (-not (Test-Path -LiteralPath $probeExe)) {
            throw 'rood_omt_probe.exe was not found. Run scripts/build-media-msvc.cmd first.'
        }
        $receiverOutput = Get-Content -LiteralPath $stdout -Raw
        $addressMatch = [regex]::Match($receiverOutput, '(?m)^omt address=(.+)$')
        if (-not $addressMatch.Success -or -not $addressMatch.Groups[1].Value.Trim()) {
            throw "OMT sender address was not reported. See $stdout"
        }
        $omtAddress = $addressMatch.Groups[1].Value.Trim()
        $omtProbeStdout = Join-Path $logDir 'omt-probe.stdout.txt'
        $omtProbeStderr = Join-Path $logDir 'omt-probe.stderr.txt'
        Remove-Item -LiteralPath $omtProbeStdout, $omtProbeStderr -ErrorAction SilentlyContinue
        $omtProbe = Start-Process -FilePath $probeExe -ArgumentList @("`"$omtAddress`"", '16') `
            -RedirectStandardOutput $omtProbeStdout -RedirectStandardError $omtProbeStderr `
            -WindowStyle Hidden -PassThru
    }
    Start-Sleep -Milliseconds 250

    $firstSender = @(
        '-hide_banner', '-loglevel', 'error',
        '-re', '-f', 'lavfi', '-i', 'testsrc=size=320x180:rate=25',
        '-re', '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
        '-re', '-f', 'lavfi', '-i', 'aevalsrc=0|0|0|0|0|0.15*sin(2*PI*330*t):s=48000:channel_layout=5.1',
        '-t', "$FirstSeconds", '-map', '0:v:0', '-map', '1:a:0', '-map', '2:a:0',
        '-c:v', 'mpeg2video', '-threads:v', '1', '-g', '25', '-b:v', '1M',
        '-c:a:0', 'mp2', '-b:a:0', '192k', '-ac:a:0', '2',
        '-c:a:1', 'ac3', '-b:a:1', '384k', '-f', 'mpegts',
        "srt://127.0.0.1:${port}?mode=caller&latency=120"
    )
    Invoke-TestSender -SenderArgs $firstSender

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
    Start-Sleep -Milliseconds 1500

    $secondSender = @(
        '-hide_banner', '-loglevel', 'error',
        '-re', '-f', 'lavfi', '-i', 'testsrc=size=160x90:rate=25',
        '-re', '-f', 'lavfi', '-i', 'sine=frequency=880:sample_rate=48000',
        '-t', '3', '-map', '0:v:0', '-map', '1:a:0',
        '-c:v', 'mpeg2video', '-threads:v', '1', '-b:v', '500k',
        '-c:a', 'mp2', '-b:a', '128k', '-f', 'mpegts',
        "srt://127.0.0.1:${port}?mode=caller&latency=120"
    )
    Invoke-TestSender -SenderArgs $secondSender

    $receiver | Wait-Process -Timeout ($ReceiverSeconds + 5)
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
    if ($AudioDevice -ge 0 -and
        ($output -notmatch 'audio callbacks=[1-9]\d*' -or
         $output -notmatch 'renderedFrames=[1-9]\d*' -or
         ((Test-Path -LiteralPath $stderr) -and
          ((Get-Content -LiteralPath $stderr -Raw) -match 'error audio output:')))) {
        throw "Audio output did not render decoded media. See $stdout and $stderr"
    }
    if ($AudioDevice -ge 0) {
        $secondConnection = $output.LastIndexOf('state connected')
        $before = [regex]::Matches($output.Substring(0, $secondConnection), 'renderedFrames=(\d+)')
        $after = [regex]::Matches($output.Substring($secondConnection), 'renderedFrames=(\d+)')
        $maxBefore = ($before | ForEach-Object { [long]$_.Groups[1].Value } | Measure-Object -Maximum).Maximum
        $maxAfter = ($after | ForEach-Object { [long]$_.Groups[1].Value } | Measure-Object -Maximum).Maximum
        if ($maxAfter -le $maxBefore) {
            throw "The second connection produced no audio output. See $stdout"
        }
        if ($FirstSeconds -ge 15 -and $output -notmatch 'driftLocked=1') {
            throw "Clock recovery did not reach its measurement phase. See $stdout"
        }
    }
    if ($SpoutName -and
        ($output -notmatch 'spout received=[1-9]\d* sent=[1-9]\d*' -or
         $output -match 'error Spout output:')) {
        throw "Spout did not send decoded video. See $stdout and $stderr"
    }
    if ($spoutProbe) {
        $spoutProbe | Wait-Process -Timeout 20
        $spoutProbe.Refresh()
        $probeOutput = Get-Content -LiteralPath $probeStdout -Raw
        if ($spoutProbe.ExitCode -ne 0 -or
            $probeOutput -notmatch 'spoutProbe frames=[1-9]\d* size=\d+x\d+ pixelSampleSum=[1-9]\d*') {
            throw "Spout receiver did not obtain image pixels. See $probeStdout and $probeStderr"
        }
    }
    if ($omtProbe) {
        $omtProbe | Wait-Process -Timeout 20
        $omtProbe.Refresh()
        $omtOutput = Get-Content -LiteralPath $omtProbeStdout -Raw
        if ($omtProbe.ExitCode -ne 0 -or
            $omtOutput -notmatch 'omtProbe video=[1-9]\d* audio=[1-9]\d* size=\d+x\d+ channels=2 pixelSampleSum=[1-9]\d* audioSampleSum=') {
            throw "OMT receiver did not obtain video and routed audio. See $omtProbeStdout and $omtProbeStderr"
        }
    }
    Write-Host 'SRT loopback passed: video, stereo and 5.1 audio, PTS, disconnect and reconnect.'
    Write-Host "Log: $stdout"
}
finally {
    $receiver.Refresh()
    if (-not $receiver.HasExited) { Stop-Process -Id $receiver.Id -Force }
    if ($spoutProbe) {
        $spoutProbe.Refresh()
        if (-not $spoutProbe.HasExited) { Stop-Process -Id $spoutProbe.Id -Force }
    }
    if ($omtProbe) {
        $omtProbe.Refresh()
        if (-not $omtProbe.HasExited) { Stop-Process -Id $omtProbe.Id -Force }
    }
}
