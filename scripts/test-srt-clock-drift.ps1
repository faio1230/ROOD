param(
    [int]$AudioDevice = 12,
    [int]$AudioRate = 48000,
    [int]$AudioChannels = 2,
    [string]$PortAudioBin = '',
    [string]$AsioOnly = '',
    [string]$OmtName = '',
    [int]$OmtChannels = 2,
    [int]$OmtProbeSeconds = 16,
    [switch]$RequireOmtClockSync,
    [double]$SenderReadRate = 1.0003,
    [int]$FirstSeconds = 180,
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe',
    [string]$ReceiverExe = '',
    [string]$LogName = 'srt-clock-drift',
    [switch]$AnalyzeOnly
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if ($AudioDevice -lt 0 -or $AudioRate -lt 8000 -or $AudioRate -gt 384000 -or
    $AudioChannels -lt 1 -or $AudioChannels -gt 256 -or
    $FirstSeconds -lt 120 -or $FirstSeconds -gt 3600 -or
    $LogName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$' -or
    [double]::IsNaN($SenderReadRate) -or [double]::IsInfinity($SenderReadRate) -or
    [Math]::Abs($SenderReadRate - 1.0) -lt 0.0001 -or
    [Math]::Abs($SenderReadRate - 1.0) -gt 0.0005) {
    throw 'Use a valid device, log name, 120-3600 seconds and a read rate 100-500 ppm from 1.0.'
}
if (-not $ReceiverExe) {
    $ReceiverExe = Join-Path $repo 'build/msvc-media-release/rood_ingest.exe'
}
if (-not $AnalyzeOnly) {
    $loopback = @{
        FfmpegPath = $FfmpegPath
        ReceiverExe = $ReceiverExe
        AudioDevice = $AudioDevice
        AudioRate = $AudioRate
        AudioChannels = $AudioChannels
        PortAudioBin = $PortAudioBin
        AsioOnly = $AsioOnly
        SpoutName = "ROOD-$LogName"
        OmtName = $OmtName
        OmtChannels = $OmtChannels
        OmtProbeSeconds = $OmtProbeSeconds
        RequireOmtClockSync = $RequireOmtClockSync
        FirstSeconds = $FirstSeconds
        ReceiverSeconds = $FirstSeconds + 12
        SenderReadRate = $SenderReadRate
        LogName = $LogName
    }
    & (Join-Path $PSScriptRoot 'test-srt-loopback.ps1') @loopback
}

$logDir = Join-Path $repo "build/tests/$LogName"
$logPath = Join-Path $logDir 'receiver.stdout.txt'
$output = Get-Content -LiteralPath $logPath -Raw
$disconnect = $output.IndexOf('state disconnected')
if ($disconnect -lt 0) { throw "The first SRT connection did not finish. See $logPath" }
$first = $output.Substring(0, $disconnect)
$audio = @([regex]::Matches($first, '(?m)^audio callbacks=.+driftLocked=.+$') |
    ForEach-Object { $_.Value })
$video = @([regex]::Matches($first, '(?m)^spout received=.+audioSyncErrorMs=.+$') |
    ForEach-Object { $_.Value })
if ($audio.Count -lt 100 -or $video.Count -lt 100) {
    throw "Too few audio or video observations for a long clock test. See $logPath"
}
function Read-Field([string]$line, [string]$name) {
    $match = [regex]::Match($line, "(?:^| )$name=([^ ]+)")
    if (-not $match.Success) { throw "Missing $name in $line" }
    return $match.Groups[1].Value
}
function Read-Double([string]$line, [string]$name) {
    return [double]::Parse((Read-Field $line $name),
        [System.Globalization.CultureInfo]::InvariantCulture)
}
$lastAudio = $audio[-1]
$lastVideo = $video[-1]
$expectedPpm = [Math]::Round(-(($SenderReadRate - 1.0) * 1000000.0), 1)
$observedPpm = Read-Double $lastAudio 'driftPpm'
$finalErrorMs = Read-Double $lastAudio 'driftErrorMs'
$maxErrorMs = ($audio | ForEach-Object {
    [Math]::Abs((Read-Double $_ 'driftErrorMs'))
} | Measure-Object -Maximum).Maximum
$maxSyncMs = ($video | ForEach-Object {
    $value = Read-Field $_ 'audioSyncErrorMs'
    if ($value -ne 'unknown') { [Math]::Abs([double]::Parse($value,
        [System.Globalization.CultureInfo]::InvariantCulture)) }
} | Measure-Object -Maximum).Maximum
$rendered = [long](Read-Field $lastAudio 'renderedFrames')
$rejected = [long](Read-Field $lastAudio 'rejectedFrames')
$underflows = [long](Read-Field $lastAudio 'deviceUnderflows')
$received = [long](Read-Field $lastVideo 'received')
$dropped = [long](Read-Field $lastVideo 'dropped')
$failed = [long](Read-Field $lastVideo 'failed')
$rejectedFraction = $rejected / [double][Math]::Max($rendered, 1)
$droppedFraction = $dropped / [double][Math]::Max($received, 1)
$summary = @(
    "senderReadRate=$SenderReadRate"
    "firstConnectionSeconds=$FirstSeconds"
    "expectedCorrectionPpm=$expectedPpm"
    "observedCorrectionPpm=$observedPpm"
    "finalDriftErrorMs=$finalErrorMs"
    "maxLoggedDriftErrorMs=$maxErrorMs"
    "maxLoggedAudioSpoutSyncErrorMs=$maxSyncMs"
    "audioRenderedFrames=$rendered"
    "audioDeviceUnderflows=$underflows"
    "audioRejectedFrames=$rejected"
    "audioRejectedFraction=$rejectedFraction"
    "spoutReceivedFrames=$received"
    "spoutDroppedFrames=$dropped"
    "spoutDroppedFraction=$droppedFraction"
    "spoutFailedFrames=$failed"
) -join [Environment]::NewLine
$summaryPath = Join-Path $logDir 'drift-summary.txt'
Set-Content -LiteralPath $summaryPath -Value $summary -Encoding utf8

if ([Math]::Abs($observedPpm - $expectedPpm) -gt 100 -or
    [Math]::Abs($finalErrorMs) -gt 50 -or $maxErrorMs -gt 50 -or
    $null -eq $maxSyncMs -or $maxSyncMs -gt 40 -or $underflows -ne 0 -or
    $rejectedFraction -gt 0.001 -or $droppedFraction -gt 0.001 -or
    $failed -ne 0 -or $rendered -lt ($FirstSeconds * $AudioRate * 0.8) -or
    $received -lt ($FirstSeconds * 25 * 0.8) -or
    (Read-Field $lastAudio 'driftLocked') -ne '1' -or
    $first -match 'callbackStalled=1|sampleClockMismatch=1') {
    throw "Synthetic clock-drift test failed. See $summaryPath and $logPath"
}
Write-Host "Synthetic SRT clock-drift test passed. Summary: $summaryPath"
