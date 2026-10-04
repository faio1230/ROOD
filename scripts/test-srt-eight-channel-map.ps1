param(
    [string]$FfmpegPath = '',
    [string]$ReceiverExe = '',
    [int]$AudioDevice = -1,
    [int]$AudioRate = 48000,
    [string]$PortAudioBin = '',
    [string]$AsioOnly = '',
    [string]$LogName = 'srt-eight-channel-map'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $FfmpegPath) {
    $FfmpegPath = Join-Path $repo 'build/deps/ffmpeg-test-cli/ffmpeg.exe'
}
if (-not $ReceiverExe) {
    $ReceiverExe = Join-Path $repo 'build/msvc-media-release/rood_ingest.exe'
}

$routes = @(
    '257:0:0', '257:1:1',
    '258:0:2', '258:1:3', '258:2:4',
    '258:3:5', '258:4:6', '258:5:7'
)
$options = @{
    FfmpegPath = $FfmpegPath
    ReceiverExe = $ReceiverExe
    DistinctAudioTones = $true
    OmtName = 'ROOD-8ch-map'
    OmtChannels = 8
    OmtRoutes = $routes
    OmtSignalChannels = '0,1,2,3,4,5,6,7'
    OmtToneHz = '300,400,500,600,700,80,900,1000'
    OmtProbeSeconds = 8
    FirstSeconds = 12
    ReceiverSeconds = 25
    LogName = $LogName
}
if ($AudioDevice -ge 0) {
    $options.AudioDevice = $AudioDevice
    $options.AudioChannels = 8
    $options.AudioRate = $AudioRate
    $options.Routes = $routes
    if ($PortAudioBin) { $options.PortAudioBin = $PortAudioBin }
    if ($AsioOnly) { $options.AsioOnly = $AsioOnly }
}

& (Join-Path $PSScriptRoot 'test-srt-loopback.ps1') @options
$probeOutput = Get-Content -LiteralPath (Join-Path $repo "build/tests/$LogName/omt-probe.stdout.txt") -Raw
$toneLine = [regex]::Match($probeOutput, '(?m)^omtProbe tonePowerRatios=.+$')
if (-not $toneLine.Success) { throw 'OMT tone results are missing.' }
Write-Host $toneLine.Value
