param(
    [Parameter(Mandatory = $true)][string]$DriverName,
    [int]$Channels = 8,
    [int]$SampleRate = 48000,
    [int]$Seconds = 10,
    [switch]$ListOnly,
    [string]$LogName = 'asio-manual'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$probe = Join-Path $repo 'build/msvc-media-asio/rood_pa_probe.exe'
$paBin = Join-Path $repo 'build/deps/portaudio-asio-msvc-test-install/bin'
if (-not (Test-Path -LiteralPath $probe) -or -not (Test-Path -LiteralPath $paBin)) {
    throw 'ASIO probe is missing. Run scripts/build-media-asio-msvc.cmd first.'
}
if ([string]::IsNullOrWhiteSpace($DriverName) -or
    $Channels -lt 1 -or $Channels -gt 256 -or
    $SampleRate -lt 8000 -or $SampleRate -gt 384000 -or
    $Seconds -lt 1 -or $Seconds -gt 60 -or
    $LogName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$') {
    throw 'Invalid driver name, format, duration or log name.'
}

$logDir = Join-Path $repo "build/tests/$LogName"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null

function Invoke-BoundedProbe {
    param([string[]]$Arguments, [string]$Prefix, [int]$TimeoutSeconds)
    $stdout = Join-Path $logDir "$Prefix.stdout.txt"
    $stderr = Join-Path $logDir "$Prefix.stderr.txt"
    Remove-Item -LiteralPath $stdout, $stderr -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $probe -ArgumentList $Arguments `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
        -WindowStyle Hidden -PassThru
    try {
        $process | Wait-Process -Timeout $TimeoutSeconds -ErrorAction Stop
    } catch {
        $process.Refresh()
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
        throw "ASIO $Prefix timed out after $TimeoutSeconds seconds. See $logDir"
    }
    $process.Refresh()
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        Stdout = Get-Content -LiteralPath $stdout -Raw
        Stderr = Get-Content -LiteralPath $stderr -Raw
    }
}

$previousPath = $env:PATH
$previousAllowlist = $env:ROOD_ASIO_ONLY
try {
    $env:PATH = "$paBin;$previousPath"
    $env:ROOD_ASIO_ONLY = $DriverName
    $listed = Invoke-BoundedProbe -Arguments @('--list') -Prefix 'list' -TimeoutSeconds 10
    if ($listed.ExitCode -ne 0) {
        throw "ASIO device listing failed: $($listed.Stderr)"
    }
    $device = $null
    foreach ($match in [regex]::Matches($listed.Stdout,
            '(?m)^(\d+) \| ASIO \| (.+?) \| output channels=(\d+) \|')) {
        if ($match.Groups[2].Value -eq $DriverName) { $device = $match; break }
    }
    if (-not $device) {
        throw "ASIO driver '$DriverName' was not available. See $logDir"
    }
    $index = [int]$device.Groups[1].Value
    $maximumChannels = [int]$device.Groups[3].Value
    Write-Host "ASIO device $index`: $DriverName ($maximumChannels output channels)"
    $channelNames = Invoke-BoundedProbe -Arguments @('--asio-channels', "$index") `
        -Prefix 'channels' -TimeoutSeconds 10
    if ($channelNames.ExitCode -ne 0) {
        throw "ASIO channel-name inspection failed: $($channelNames.Stderr)"
    }
    $lines = [regex]::Matches($channelNames.Stdout, '(?m)^ASIO output channel (\d+): ')
    if ($lines.Count -ne $maximumChannels) {
        throw "ASIO driver listed $($lines.Count) names for $maximumChannels output channels. See $logDir"
    }
    for ($channel = 0; $channel -lt $maximumChannels; ++$channel) {
        if ([int]$lines[$channel].Groups[1].Value -ne $channel) {
            throw "ASIO output channel numbering is incomplete. See $logDir"
        }
    }
    Write-Host $channelNames.Stdout
    if ($ListOnly) {
        Write-Host "ASIO channel names recorded. Logs: $logDir"
        return
    }
    if ($Channels -gt $maximumChannels) {
        throw "Requested $Channels channels, but the driver reports $maximumChannels."
    }
    $timing = Invoke-BoundedProbe -Arguments @(
        '--timing', "$index", 'default', "$Seconds", "$Channels", "$SampleRate"
    ) -Prefix 'timing' -TimeoutSeconds ($Seconds + 15)
    Write-Host $timing.Stdout
    if ($timing.Stderr) { Write-Warning $timing.Stderr.Trim() }
    if ($timing.ExitCode -ne 0) {
        throw "ASIO timing probe failed. See $logDir"
    }
    Write-Host "ASIO timing probe passed. Logs: $logDir"
} finally {
    $env:PATH = $previousPath
    $env:ROOD_ASIO_ONLY = $previousAllowlist
}
