param(
    [int]$DeviceIndex = 12,
    [string]$FfmpegPath = 'C:\Program Files\ffmpeg\bin\ffmpeg.exe'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$probe = Join-Path $repo 'build/msvc/rood_pa_probe.exe'
$paBin = Join-Path $repo 'build/deps/portaudio-msvc-install/bin'
$logDir = Join-Path $repo 'build/tests/srt-loopback'
if (-not (Test-Path -LiteralPath $probe)) {
    throw 'rood_pa_probe.exe was not found. Run scripts/build-msvc.cmd first.'
}
if (-not (Test-Path -LiteralPath $paBin)) {
    throw 'PortAudio runtime was not found. Run scripts/bootstrap-portaudio-msvc.cmd first.'
}
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$holderStdout = Join-Path $logDir 'exclusive-holder.stdout.txt'
$holderStderr = Join-Path $logDir 'exclusive-holder.stderr.txt'
Remove-Item -LiteralPath $holderStdout, $holderStderr -ErrorAction SilentlyContinue
$env:PATH = "$paBin;$env:PATH"
$holder = Start-Process -FilePath $probe `
    -ArgumentList @('--timing', "$DeviceIndex", 'exclusive', '8', '2') `
    -RedirectStandardOutput $holderStdout -RedirectStandardError $holderStderr `
    -WindowStyle Hidden -PassThru
try {
    Start-Sleep -Seconds 1
    $holder.Refresh()
    if ($holder.HasExited) {
        throw "The exclusive device holder exited before the test. See $holderStderr"
    }
    & (Join-Path $PSScriptRoot 'test-srt-loopback.ps1') `
        -FfmpegPath $FfmpegPath -AudioDevice $DeviceIndex -WasapiExclusive `
        -FirstSeconds 12 -ReceiverSeconds 24
    $outputPath = Join-Path $logDir 'receiver.stdout.txt'
    $output = Get-Content -LiteralPath $outputPath -Raw
    $firstDisconnect = $output.IndexOf('state disconnected')
    if ($firstDisconnect -lt 0) { throw "No SRT disconnect was recorded. See $outputPath" }
    $beforeDisconnect = $output.Substring(0, $firstDisconnect)
    if ($beforeDisconnect -notmatch 'deviceAvailable=0' -or
        $beforeDisconnect -notmatch 'recoveries=[1-9]\d*' -or
        $beforeDisconnect -notmatch 'renderedFrames=[1-9]\d*.*deviceAvailable=1') {
        throw "Audio did not recover while SRT remained connected. See $outputPath"
    }
    $holder.Refresh()
    if (-not $holder.HasExited) { $holder | Wait-Process -Timeout 10; $holder.Refresh() }
    if ($holder.ExitCode -ne 0) {
        throw "The exclusive device holder failed. See $holderStderr"
    }
    Write-Host 'Audio recovery passed: exclusive device unavailable, reopened, and rendered media during one SRT connection.'
}
finally {
    $holder.Refresh()
    if (-not $holder.HasExited) { Stop-Process -Id $holder.Id -Force }
}
