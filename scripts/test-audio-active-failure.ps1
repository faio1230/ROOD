param(
    [int]$AudioDevice = 12,
    [int]$AudioRate = 48000,
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe',
    [string]$ReceiverExe = '',
    [string]$PortAudioBin = '',
    [string]$AsioOnly = '',
    [switch]$WithVideoOutputs,
    [switch]$AnalyzeOnly,
    [string]$LogName = 'audio-active-failure'
)

$ErrorActionPreference = 'Stop'
if ($AudioDevice -lt 0 -or $AudioRate -lt 8000 -or $AudioRate -gt 384000 -or
    $LogName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$') {
    throw 'Invalid audio device or log name.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$receiver = if ($ReceiverExe) { $ReceiverExe } else {
    Join-Path $repo 'build/msvc-media/rood_ingest.exe'
}
if (-not $AnalyzeOnly -and -not (Test-Path -LiteralPath $receiver -PathType Leaf)) {
    throw 'Debug media receiver is missing. Run scripts/build-media-msvc.cmd first.'
}
if (-not $AnalyzeOnly) {
    $previousFault = $env:ROOD_TEST_ABORT_AUDIO_AFTER_CALLBACKS
    try {
        # Debug builds stop only the first PortAudio stream after 100 callbacks.
        $env:ROOD_TEST_ABORT_AUDIO_AFTER_CALLBACKS = '100'
        $outputs = @{ AudioRate = $AudioRate; MinFirstAudioRenderPercent = 75 }
        if ($PortAudioBin) { $outputs.PortAudioBin = $PortAudioBin }
        if ($AsioOnly) { $outputs.AsioOnly = $AsioOnly }
        if ($WithVideoOutputs) {
            $outputs.SpoutName = 'ROOD-ACTIVE-FAILURE-SPOUT'
            $outputs.OmtName = 'ROOD-ACTIVE-FAILURE-OMT'
            $outputs.OmtProbeSeconds = 16
            $outputs.MaxFirstSpoutDropPercent = 10
        }
        & (Join-Path $PSScriptRoot 'test-srt-loopback.ps1') `
            -FfmpegPath $FfmpegPath -ReceiverExe $receiver -AudioDevice $AudioDevice `
            -FirstSeconds 22 -ReceiverSeconds 35 -LogName $LogName @outputs
    } finally {
        $env:ROOD_TEST_ABORT_AUDIO_AFTER_CALLBACKS = $previousFault
    }
}

$stdout = Join-Path $repo "build/tests/$LogName/receiver.stdout.txt"
$output = Get-Content -LiteralPath $stdout -Raw
$firstDisconnect = $output.IndexOf('state disconnected')
if ($firstDisconnect -lt 0) { throw "No SRT disconnect was recorded. See $stdout" }
$firstOutput = $output.Substring(0, $firstDisconnect)
$audioMatches = @([regex]::Matches($firstOutput, '(?m)^audio callbacks=.+$'))
$lines = @($audioMatches |
    ForEach-Object { $_.Value })
$preFailureIndex = -1
$recoveredIndex = -1
for ($index = 0; $index -lt $lines.Count; ++$index) {
    if ($lines[$index] -match 'deviceAvailable=1' -and
        $lines[$index] -match 'callbacks=[1-9]\d*' -and
        $lines[$index] -match 'streamFailures=0') {
        $preFailureIndex = $index
    }
    if ($preFailureIndex -ge 0 -and $index -gt $preFailureIndex -and
        $lines[$index] -match 'deviceAvailable=1' -and
        $lines[$index] -match 'recoveries=[1-9]\d*' -and
        $lines[$index] -match 'streamFailures=[1-9]\d*' -and
        $lines[$index] -match 'renderedFrames=[1-9]\d*') {
        $recoveredIndex = $index
        break
    }
}
if ($preFailureIndex -lt 0 -or $recoveredIndex -lt 0) {
    throw "Debug callback stop did not recover during the first SRT connection. See $stdout"
}
$outputSummary = @()
if ($WithVideoOutputs) {
    foreach ($item in @(
        @{ Pattern = '(?m)^spout received=.+$'; Field = 'sent'; Name = 'Spout' },
        @{ Pattern = '(?m)^omt video=.+$'; Field = 'video'; Name = 'OMT' }
    )) {
        $observations = @()
        foreach ($audioIndex in @($preFailureIndex, $recoveredIndex)) {
            $before = $firstOutput.Substring(0, $audioMatches[$audioIndex].Index)
            $matches = @([regex]::Matches($before, $item.Pattern))
            $fieldMatch = if ($matches.Count -gt 0) {
                [regex]::Match($matches[-1].Value, "(?:^| )$($item.Field)=(\d+)")
            } else { $null }
            if (-not $fieldMatch -or -not $fieldMatch.Success) {
                throw "$($item.Name) statistics are missing around audio recovery. See $stdout"
            }
            $observations += [long]$fieldMatch.Groups[1].Value
        }
        if ($observations[1] -le $observations[0]) {
            throw "$($item.Name) stopped while audio recovered. See $stdout"
        }
        $outputSummary += "$($item.Name)Before=$($observations[0]) After=$($observations[1])"
    }
    $omtProbePath = Join-Path $repo "build/tests/$LogName/omt-probe.stdout.txt"
    $omtProbe = Get-Content -LiteralPath $omtProbePath -Raw
    foreach ($field in @('videoTimestampRegressions', 'audioTimestampRegressions')) {
        $value = [regex]::Match($omtProbe, "(?:^| )$field=(\d+)")
        if (-not $value.Success -or [long]$value.Groups[1].Value -ne 0) {
            throw "OMT timestamps regressed during audio recovery. See $omtProbePath"
        }
    }
    foreach ($field in @('maxVideoTimestampGapMs', 'maxAudioTimestampGapMs')) {
        $value = [regex]::Match($omtProbe, "(?:^| )$field=([\d.]+)")
        if (-not $value.Success -or
            [double]::Parse($value.Groups[1].Value,
                [Globalization.CultureInfo]::InvariantCulture) -gt 1500) {
            throw "OMT timestamp gap exceeded 1.5 seconds during audio recovery. See $omtProbePath"
        }
        $outputSummary += "$field=$($value.Groups[1].Value)"
    }
    $lastOmt = @([regex]::Matches($firstOutput, '(?m)^omt video=.+$'))[-1].Value
    $video = [regex]::Match($lastOmt, '(?:^| )video=(\d+)')
    $dropped = [regex]::Match($lastOmt, '(?:^| )droppedVideo=(\d+)')
    if (-not $video.Success -or -not $dropped.Success -or
        [long]$dropped.Groups[1].Value -gt ([long]$video.Groups[1].Value * 0.1)) {
        throw "OMT dropped more than 10% of video during audio recovery. See $stdout"
    }
    $outputSummary += "omtDroppedVideo=$($dropped.Groups[1].Value)"
    $packets = [regex]::Match($lastOmt, '(?:^| )audioPackets=(\d+)')
    $rejected = [regex]::Match($lastOmt, '(?:^| )rejectedAudio=(\d+)')
    if (-not $packets.Success -or -not $rejected.Success -or
        [long]$rejected.Groups[1].Value -gt
            ([long]$packets.Groups[1].Value * 960 * 0.1)) {
        throw "OMT rejected more than 10% of audio frames during recovery. See $stdout"
    }
    $outputSummary += "omtRejectedAudioFrames=$($rejected.Groups[1].Value)"
    $lastSpout = @([regex]::Matches($firstOutput, '(?m)^spout received=.+$'))[-1].Value
    $received = [regex]::Match($lastSpout, '(?:^| )received=(\d+)')
    $spoutDropped = [regex]::Match($lastSpout, '(?:^| )dropped=(\d+)')
    if (-not $received.Success -or -not $spoutDropped.Success -or
        [long]$spoutDropped.Groups[1].Value -gt
            ([long]$received.Groups[1].Value * 0.1)) {
        throw "Spout dropped more than 10% of video during audio recovery. See $stdout"
    }
    $outputSummary += "spoutDroppedVideo=$($spoutDropped.Groups[1].Value)"
}
@(
    'fault=first audio stream returns paAbort after 100 callbacks'
    "beforeFailureStats=$($lines[$preFailureIndex])"
    "recoveredStats=$($lines[$recoveredIndex])"
    $outputSummary
) | Set-Content -LiteralPath (Join-Path $repo "build/tests/$LogName/recovery-summary.txt") -Encoding utf8
Write-Host 'Active audio stream failure recovered and rendered SRT media before disconnect.'
