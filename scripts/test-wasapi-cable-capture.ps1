param(
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe',
    [switch]$WasapiExclusive
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not (Test-Path -LiteralPath $FfmpegPath -PathType Leaf)) {
    throw "FFmpeg CLI was not found: $FfmpegPath"
}
$devices = & (Join-Path $PSScriptRoot 'list-audio-devices-msvc.cmd')
if ($LASTEXITCODE -ne 0) { throw 'PortAudio device listing failed.' }
$device = [regex]::Match(($devices -join "`n"),
    '(?m)^(\d+) \| Windows WASAPI \| CABLE Input \(VB-Audio Virtual Cable\) \|')
if (-not $device.Success) { throw 'VB-Audio CABLE Input was not found in the WASAPI output list.' }

$mode = if ($WasapiExclusive) { 'exclusive' } else { 'shared' }
$logDir = Join-Path $repo "build/tests/wasapi-cable-$mode"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$capture = Join-Path $logDir 'cable-output-pcm16.wav'
$captureStdout = Join-Path $logDir 'capture.stdout.txt'
$captureStderr = Join-Path $logDir 'capture.stderr.txt'
$captureArgs = "-hide_banner -loglevel error -f dshow -i `"audio=CABLE Output (VB-Audio Virtual Cable)`" -t 25 -ar 48000 -ac 2 -c:a pcm_s16le `"$capture`" -y"
$recorder = Start-Process -FilePath $FfmpegPath -ArgumentList $captureArgs `
    -WorkingDirectory $logDir -RedirectStandardOutput $captureStdout `
    -RedirectStandardError $captureStderr -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 1
    $recorder.Refresh()
    if ($recorder.HasExited) {
        throw "Cable capture stopped before the SRT test. See $captureStderr"
    }
    & (Join-Path $PSScriptRoot 'test-srt-loopback.ps1') `
        -FfmpegPath $FfmpegPath -AudioDevice ([int]$device.Groups[1].Value) `
        -WasapiExclusive:$WasapiExclusive -FirstSeconds 5 -ReceiverSeconds 20 `
        -LogName "wasapi-cable-loopback-$mode"
    if ($LASTEXITCODE -ne 0) { throw 'SRT loopback test failed.' }
    try {
        $recorder | Wait-Process -Timeout 30 -ErrorAction Stop
    } catch {
        throw "Cable capture timed out. See $captureStderr"
    }
    $recorder.Refresh()
    if ($recorder.ExitCode -ne 0) {
        throw "Cable capture failed with code $($recorder.ExitCode). See $captureStderr"
    }
    & python (Join-Path $PSScriptRoot 'analyze-cable-capture.py') $capture
    if ($LASTEXITCODE -ne 0) { throw "Cable signal analysis failed. See $capture" }
    Write-Host "Recorded WASAPI $mode routing: $capture"
} finally {
    $recorder.Refresh()
    if (-not $recorder.HasExited) { Stop-Process -Id $recorder.Id -Force }
}
