param(
    [int]$AudioDevice = 12,
    [string]$SpoutName = 'ROOD-Hour',
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe',
    [switch]$AnalyzeOnly
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$logDir = Join-Path $repo 'build/tests/srt-hour'
if (-not $AnalyzeOnly) {
    & (Join-Path $PSScriptRoot 'test-srt-loopback.ps1') `
        -FfmpegPath $FfmpegPath -AudioDevice $AudioDevice -SpoutName $SpoutName `
        -FirstSeconds 3600 -ReceiverSeconds 3630 -LogName 'srt-hour'
}

$logPath = Join-Path $logDir 'receiver.stdout.txt'
$output = Get-Content -LiteralPath $logPath -Raw
$firstDisconnect = $output.IndexOf('state disconnected')
if ($firstDisconnect -lt 0) { throw "The first SRT connection did not end cleanly. See $logPath" }
$first = $output.Substring(0, $firstDisconnect)
$audioLines = [regex]::Matches($first, '(?m)^audio callbacks=.+$')
$audioDetailedLines = [regex]::Matches($first, '(?m)^audio callbacks=.+driftLocked=.+$')
$videoLines = [regex]::Matches($first, '(?m)^spout received=.+$')
if ($audioLines.Count -eq 0 -or $audioDetailedLines.Count -eq 0 -or
    $videoLines.Count -eq 0) {
    throw "Audio or Spout statistics were not recorded. See $logPath"
}
$audio = $audioLines[$audioLines.Count - 1].Value
$audioDetailed = $audioDetailedLines[$audioDetailedLines.Count - 1].Value
$video = $videoLines[$videoLines.Count - 1].Value
$read = {
    param([string]$line, [string]$field)
    $match = [regex]::Match($line, "(?:^| )${field}=([^ ]+)")
    if (-not $match.Success) { throw "Missing $field in: $line" }
    return $match.Groups[1].Value
}
$rendered = [long](& $read $audio 'renderedFrames')
$underflows = [long](& $read $audio 'deviceUnderflows')
$rejected = [long](& $read $audio 'rejectedFrames')
$timestampRegressions = [long](& $read $audio 'timestampRegressions')
$silent = [long](& $read $audio 'silentFrames')
$locked = [int](& $read $audioDetailed 'driftLocked')
$driftPpm = [double]::Parse((& $read $audioDetailed 'driftPpm'), [Globalization.CultureInfo]::InvariantCulture)
$driftErrorMs = [double]::Parse((& $read $audioDetailed 'driftErrorMs'), [Globalization.CultureInfo]::InvariantCulture)
$received = [long](& $read $video 'received')
$sent = [long](& $read $video 'sent')
$dropped = [long](& $read $video 'dropped')
$failed = [long](& $read $video 'failed')
$syncValues = [regex]::Matches($first, 'audioSyncErrorMs=(-?\d+(?:\.\d+)?)') |
    ForEach-Object { [Math]::Abs([double]::Parse($_.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)) }
$maxSyncErrorMs = ($syncValues | Measure-Object -Maximum).Maximum
$driftValues = [regex]::Matches($first, 'driftErrorMs=(-?\d+(?:\.\d+)?)') |
    ForEach-Object { [Math]::Abs([double]::Parse($_.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)) }
$maxDriftErrorMs = ($driftValues | Measure-Object -Maximum).Maximum
$bufferValues = [regex]::Matches($first, 'receiveBufferMs=(\d+)') |
    ForEach-Object { [int]$_.Groups[1].Value }
$maxReceiveBufferMs = ($bufferValues | Measure-Object -Maximum).Maximum
$rejectedFraction = $rejected / [double][Math]::Max($rendered, 1)
$droppedFraction = $dropped / [double][Math]::Max($received, 1)
$summary = @(
    "firstConnectionSeconds=3600"
    "audioRenderedFrames=$rendered"
    "audioDeviceUnderflows=$underflows"
    "audioRejectedFrames=$rejected"
    "audioRejectedFraction=$rejectedFraction"
    "audioSilentFrames=$silent"
    "audioTimestampRegressions=$timestampRegressions"
    "driftLocked=$locked"
    "driftCorrectionPpm=$driftPpm"
    "driftErrorMs=$driftErrorMs"
    "spoutReceivedFrames=$received"
    "spoutSentFrames=$sent"
    "spoutDroppedFrames=$dropped"
    "spoutDroppedFraction=$droppedFraction"
    "spoutFailedFrames=$failed"
    "maxLoggedAudioSyncErrorMs=$maxSyncErrorMs"
    "maxLoggedDriftErrorMs=$maxDriftErrorMs"
    "maxLoggedReceiveBufferMs=$maxReceiveBufferMs"
) -join [Environment]::NewLine
$summaryPath = Join-Path $logDir 'summary.txt'
Set-Content -LiteralPath $summaryPath -Value $summary -Encoding utf8
if ($rendered -lt 160000000 -or $underflows -ne 0 -or
    $rejectedFraction -gt 0.0005 -or $locked -ne 1 -or
    $received -lt 85000 -or $sent -lt 84000 -or
    $droppedFraction -gt 0.0002 -or $failed -ne 0 -or
    [Math]::Abs($driftErrorMs) -gt 50 -or
    $maxDriftErrorMs -gt 50 -or $maxSyncErrorMs -gt 40) {
    throw "The hour-long media stability thresholds failed. See $summaryPath and $logPath"
}
Write-Host "One-hour SRT stability test passed. Summary: $summaryPath"
