param(
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe',
    [int]$AudioDevice = -1,
    [string]$AudioDeviceId = '',
    [switch]$ExpectAudioUnavailable,
    [switch]$WasapiExclusive,
    [string]$ReceiverExe = '',
    [string]$PortAudioBin = '',
    [int]$AudioChannels = 2,
    [int]$AudioRate = 48000,
    [int]$AudioDelayMs = 250,
    [int]$VideoDelayMs = 250,
    [int]$SrtLatencyMs = 120,
    [int]$SrtBufferKiB = 0,
    [string[]]$Routes = @('257:0:0', '258:5:1'),
    [string]$AsioOnly = '',
    [string]$SpoutName = '',
    [string]$OmtName = '',
    [int]$OmtChannels = 2,
    [int]$OmtDelayMs = 250,
    [string[]]$OmtRoutes = @('257:0:0', '258:5:1'),
    [string]$OmtSignalChannels = '0,1',
    [int]$OmtProbeSeconds = 16,
    [switch]$RequireOmtClockSync,
    [int]$FirstSeconds = 5,
    [int]$ReceiverSeconds = 20,
    [double]$SenderReadRate = 1.0,
    [string]$LogName = 'srt-loopback'
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
if ([double]::IsNaN($SenderReadRate) -or [double]::IsInfinity($SenderReadRate) -or
    $SenderReadRate -lt 0.995 -or $SenderReadRate -gt 1.005) {
    throw 'SenderReadRate must be between 0.995 and 1.005.'
}
$senderRateText = $SenderReadRate.ToString('0.000000',
    [System.Globalization.CultureInfo]::InvariantCulture)
if ($LogName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$') {
    throw 'LogName must contain only letters, digits, underscores and hyphens.'
}
if ($OmtName -and ($OmtChannels -lt 1 -or $OmtChannels -gt 32 -or
                   $OmtDelayMs -lt 0 -or $OmtDelayMs -gt 5000 -or
                   $OmtRoutes.Count -eq 0 -or -not $OmtSignalChannels -or
                   $OmtProbeSeconds -lt 1 -or $OmtProbeSeconds -gt 600 -or
                   $OmtProbeSeconds -gt $ReceiverSeconds)) {
    throw 'OMT requires 1-32 channels, a route, signal channels and a probe duration within receiver time.'
}
if ($RequireOmtClockSync -and (-not $OmtName -or
    ($AudioDevice -lt 0 -and -not $AudioDeviceId) -or
    $ExpectAudioUnavailable -or $FirstSeconds -lt 15)) {
    throw 'RequireOmtClockSync needs OMT, an available audio device and at least 15 seconds.'
}
if ($SrtBufferKiB -ne 0 -and ($SrtBufferKiB -lt 64 -or $SrtBufferKiB -gt 16384)) {
    throw 'SrtBufferKiB must be 0 or 64..16384.'
}
if ($AudioDelayMs -lt 0 -or $AudioDelayMs -gt 3000 -or
    $VideoDelayMs -lt 0 -or $VideoDelayMs -gt 5000 -or
    $SrtLatencyMs -lt 20 -or $SrtLatencyMs -gt 8000) {
    throw 'AudioDelayMs, VideoDelayMs or SrtLatencyMs is outside the supported range.'
}
if ($ExpectAudioUnavailable -and -not $AudioDeviceId) {
    throw 'ExpectAudioUnavailable requires AudioDeviceId.'
}

$logDir = Join-Path $repo "build/tests/$LogName"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$stdout = Join-Path $logDir 'receiver.stdout.txt'
$stderr = Join-Path $logDir 'receiver.stderr.txt'
Remove-Item -LiteralPath $stdout, $stderr -ErrorAction SilentlyContinue
$port = Get-Random -Minimum 20000 -Maximum 50000
$env:PATH = "$runtimeBin;$PortAudioBin;$spoutBin;$omtBin;$env:PATH"
if ($AsioOnly) { $env:ROOD_ASIO_ONLY = $AsioOnly }
$receiverArgs = @('--port', "$port", '--latency', "$SrtLatencyMs",
                  '--seconds', "$ReceiverSeconds", '--require-media')
if ($SrtBufferKiB -gt 0) { $receiverArgs += @('--srt-buffer-kib', "$SrtBufferKiB") }
if ($AudioDevice -ge 0 -or $AudioDeviceId) {
    if ($AudioDevice -ge 0) { $receiverArgs += @('--audio-device', "$AudioDevice") }
    if ($AudioDeviceId) { $receiverArgs += @('--audio-device-id', $AudioDeviceId) }
    $receiverArgs += @('--audio-channels', "$AudioChannels",
                       '--audio-rate', "$AudioRate", '--audio-delay', "$AudioDelayMs")
    foreach ($route in $Routes) { $receiverArgs += @('--route', $route) }
    if ($WasapiExclusive) { $receiverArgs += '--wasapi-exclusive' }
}
if ($SpoutName) {
    $receiverArgs += @('--spout', $SpoutName, '--video-delay', "$VideoDelayMs")
}
if ($OmtName) {
    $receiverArgs += @('--omt', $OmtName, '--omt-channels', "$OmtChannels",
                       '--omt-delay', "$OmtDelayMs")
    foreach ($route in $OmtRoutes) { $receiverArgs += @('--omt-route', $route) }
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
    for ($attempt = 1; $attempt -le 8; ++$attempt) {
        & $FfmpegPath @SenderArgs
        if ($LASTEXITCODE -eq 0) { return }
        if ($attempt -lt 8) {
            Write-Warning "SRT sender connection failed (attempt $attempt); retrying."
            Start-Sleep -Milliseconds 750
        }
    }
    throw 'FFmpeg sender failed after eight connection attempts.'
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
        $omtProbe = Start-Process -FilePath $probeExe `
            -ArgumentList @("`"$omtAddress`"", "$OmtProbeSeconds", "$OmtChannels", $OmtSignalChannels) `
            -RedirectStandardOutput $omtProbeStdout -RedirectStandardError $omtProbeStderr `
            -WindowStyle Hidden -PassThru
    }
    Start-Sleep -Milliseconds 250

    $firstSender = @(
        '-hide_banner', '-loglevel', 'error',
        '-readrate', $senderRateText, '-f', 'lavfi', '-i', 'testsrc=size=320x180:rate=25',
        '-readrate', $senderRateText, '-f', 'lavfi', '-i', 'sine=frequency=440:sample_rate=48000',
        '-readrate', $senderRateText, '-f', 'lavfi', '-i', 'aevalsrc=0|0|0|0|0|0.15*sin(2*PI*330*t):s=48000:channel_layout=5.1',
        '-t', "$FirstSeconds", '-map', '0:v:0', '-map', '1:a:0', '-map', '2:a:0',
        '-c:v', 'mpeg2video', '-threads:v', '1', '-g', '25', '-b:v', '1M',
        '-c:a:0', 'mp2', '-b:a:0', '192k', '-ac:a:0', '2',
        '-c:a:1', 'ac3', '-b:a:1', '384k', '-f', 'mpegts',
        "srt://127.0.0.1:${port}?mode=caller&latency=$SrtLatencyMs"
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
        "srt://127.0.0.1:${port}?mode=caller&latency=$SrtLatencyMs"
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
    if (($AudioDevice -ge 0 -or $AudioDeviceId) -and -not $ExpectAudioUnavailable -and
        ($output -notmatch 'audio callbacks=[1-9]\d*' -or
         $output -notmatch 'renderedFrames=[1-9]\d*' -or
         ((Test-Path -LiteralPath $stderr) -and
          ((Get-Content -LiteralPath $stderr -Raw) -match 'error audio output:')))) {
        throw "Audio output did not render decoded media. See $stdout and $stderr"
    }
    if (($AudioDevice -ge 0 -or $AudioDeviceId) -and -not $ExpectAudioUnavailable) {
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
        if ($FirstSeconds -ge 15) {
            $firstDisconnect = $output.IndexOf('state disconnected')
            $firstOutput = $output.Substring(0, $firstDisconnect)
            $firstAudio = [regex]::Matches($firstOutput, '(?m)^audio callbacks=.+$') |
                Select-Object -Last 1
            $firstRendered = [regex]::Match($firstAudio.Value, 'renderedFrames=(\d+)')
            $firstRejected = [regex]::Match($firstAudio.Value, 'rejectedFrames=(\d+)')
            if (-not $firstRendered.Success -or -not $firstRejected.Success) {
                throw "First-connection audio statistics are missing. See $stdout"
            }
            $rendered = [long]$firstRendered.Groups[1].Value
            $rejected = [long]$firstRejected.Groups[1].Value
            if ($rendered -lt ($FirstSeconds * $AudioRate * 0.8) -or
                $rejected -gt ($FirstSeconds * $AudioRate * 0.01) -or
                $firstOutput -match 'sampleClockMismatch=1') {
                throw "First-connection audio clock or media continuity failed. See $stdout"
            }
        }
    }
    if ($ExpectAudioUnavailable -and
        ($output -notmatch 'deviceAvailable=0' -or
         $output -notmatch 'reopenAttempts=(?:[2-9]|[1-9]\d+)\b' -or
         $output -match 'renderedFrames=[1-9]\d*')) {
        throw "Audio was not held in a retrying unavailable state. See $stdout"
    }
    if ($SpoutName -and
        ($output -notmatch 'spout received=[1-9]\d* sent=[1-9]\d*' -or
         $output -match 'error Spout output:')) {
        throw "Spout did not send decoded video. See $stdout and $stderr"
    }
    if ($SpoutName -and $FirstSeconds -ge 15) {
        $firstOutput = $output.Substring(0, $output.IndexOf('state disconnected'))
        $firstSpout = [regex]::Matches($firstOutput, '(?m)^spout received=.+$') |
            Select-Object -Last 1
        $received = [regex]::Match($firstSpout.Value, 'received=(\d+)')
        $dropped = [regex]::Match($firstSpout.Value, 'dropped=(\d+)')
        if (-not $received.Success -or -not $dropped.Success -or
            [long]$dropped.Groups[1].Value -gt ([long]$received.Groups[1].Value * 0.01)) {
            throw "First-connection Spout output dropped too many frames. See $stdout"
        }
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
            $omtOutput -notmatch "omtProbe video=[1-9]\d* audio=[1-9]\d* size=\d+x\d+ channels=$OmtChannels pixelSampleSum=[1-9]\d* audioSampleSum=") {
            throw "OMT receiver did not obtain video and routed audio. See $omtProbeStdout and $omtProbeStderr"
        }
        if ($OmtProbeSeconds -ge 60) {
            $videoFirst = [regex]::Match($omtOutput, '(?<!\w)videoTimestamp=(\d+)')
            $videoLast = [regex]::Match($omtOutput, '(?<!\w)videoLastTimestamp=(\d+)')
            $audioFirst = [regex]::Match($omtOutput, '(?<!\w)audioTimestamp=(\d+)')
            $audioLast = [regex]::Match($omtOutput, '(?<!\w)audioLastTimestamp=(\d+)')
            $videoGap = [regex]::Match($omtOutput, '(?<!\w)maxVideoTimestampGapMs=([\d.]+)')
            $audioGap = [regex]::Match($omtOutput, '(?<!\w)maxAudioTimestampGapMs=([\d.]+)')
            if (-not $videoFirst.Success -or -not $videoLast.Success -or
                -not $audioFirst.Success -or -not $audioLast.Success -or
                -not $videoGap.Success -or -not $audioGap.Success) {
                throw "OMT timing statistics are missing. See $omtProbeStdout"
            }
            $videoSpan = ([long]$videoLast.Groups[1].Value -
                          [long]$videoFirst.Groups[1].Value) / 10000000.0
            $audioSpan = ([long]$audioLast.Groups[1].Value -
                          [long]$audioFirst.Groups[1].Value) / 10000000.0
            $maxVideoGap = [double]::Parse($videoGap.Groups[1].Value,
                                          [Globalization.CultureInfo]::InvariantCulture)
            $maxAudioGap = [double]::Parse($audioGap.Groups[1].Value,
                                          [Globalization.CultureInfo]::InvariantCulture)
            if ($videoSpan -lt ($OmtProbeSeconds - 5) -or
                $audioSpan -lt ($OmtProbeSeconds - 5) -or
                $maxVideoGap -gt 100 -or $maxAudioGap -gt 60) {
                throw "OMT long-running media timestamps have gaps. See $omtProbeStdout"
            }
        }
    }
    if ($RequireOmtClockSync) {
        $firstOutput = $output.Substring(0, $output.IndexOf('state disconnected'))
        $omtLines = @([regex]::Matches($firstOutput, '(?m)^omt video=.+$') |
            ForEach-Object { $_.Value })
        if ($omtLines.Count -eq 0) { throw "OMT clock statistics are missing. See $stdout" }
        $lastOmt = $omtLines[-1]
        function Read-OmtField([string]$line, [string]$name) {
            $match = [regex]::Match($line, "(?:^| )$name=([^\s]+)")
            if (-not $match.Success) { throw "Missing OMT $name in $line" }
            return $match.Groups[1].Value
        }
        $video = [long](Read-OmtField $lastOmt 'video')
        $audioPackets = [long](Read-OmtField $lastOmt 'audioPackets')
        $clockVideo = [long](Read-OmtField $lastOmt 'audioClockVideo')
        $clockAudio = [long](Read-OmtField $lastOmt 'audioClockAudio')
        $videoError = [double]::Parse((Read-OmtField $lastOmt 'maxVideoClockErrorMs'),
            [Globalization.CultureInfo]::InvariantCulture)
        $audioError = [double]::Parse((Read-OmtField $lastOmt 'maxAudioClockErrorMs'),
            [Globalization.CultureInfo]::InvariantCulture)
        @(
            "senderReadRate=$SenderReadRate"
            "audioDelayMs=$AudioDelayMs"
            "omtDelayMs=$OmtDelayMs"
            "firstConnectionSeconds=$FirstSeconds"
            "videoFrames=$video"
            "audioPackets=$audioPackets"
            "audioClockVideoFrames=$clockVideo"
            "audioClockAudioPackets=$clockAudio"
            "maxVideoClockErrorMs=$videoError"
            "maxAudioClockErrorMs=$audioError"
        ) | Set-Content -LiteralPath (Join-Path $logDir 'omt-clock-summary.txt') -Encoding utf8
        if ($video -lt ($FirstSeconds * 25 * 0.8) -or
            $audioPackets -lt ($FirstSeconds * 50 * 0.8) -or
            $clockVideo -lt ($video * 0.8) -or $clockAudio -lt ($audioPackets * 0.8) -or
            $videoError -gt 40 -or $audioError -gt 40 -or
            [long](Read-OmtField $lastOmt 'droppedVideo') -ne 0 -or
            [long](Read-OmtField $lastOmt 'rejectedAudio') -ne 0) {
            throw "OMT did not follow the audio device clock. See $stdout and omt-clock-summary.txt"
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
