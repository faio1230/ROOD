param(
    [string[]]$DriverNames = @(),
    [int]$TimeoutSeconds = 10,
    [switch]$IncludeAllDrivers,
    [string]$LogName = 'asio-registry-audit'
)

$ErrorActionPreference = 'Stop'
if ($TimeoutSeconds -lt 2 -or $TimeoutSeconds -gt 60 -or
    $LogName -notmatch '^[a-zA-Z0-9][a-zA-Z0-9_-]{0,63}$') {
    throw 'Invalid timeout or log name.'
}
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$probe = Join-Path $repo 'build/msvc-media-asio/rood_pa_probe.exe'
$paBin = Join-Path $repo 'build/deps/portaudio-asio-msvc-test-install/bin'
if (-not (Test-Path -LiteralPath $probe -PathType Leaf) -or
    -not (Test-Path -LiteralPath $paBin -PathType Container)) {
    throw 'ASIO probe is missing. Run scripts/build-media-asio-msvc.cmd first.'
}
if ($DriverNames.Count -eq 0) {
    $registryPath = 'HKLM:\SOFTWARE\ASIO'
    if (-not (Test-Path -LiteralPath $registryPath)) {
        throw 'The 64-bit ASIO registry key was not found.'
    }
    $DriverNames = @(Get-ChildItem -LiteralPath $registryPath |
        Sort-Object PSChildName | ForEach-Object { $_.PSChildName })
}
if ($DriverNames.Count -eq 0 -or @($DriverNames | Where-Object {
    [string]::IsNullOrWhiteSpace($_)
}).Count -gt 0) {
    throw 'No valid ASIO driver names were supplied.'
}
if ($IncludeAllDrivers) { $DriverNames = @('') + $DriverNames }

$logDir = Join-Path $repo "build/tests/$LogName"
New-Item -ItemType Directory -Path $logDir -Force | Out-Null
$previousPath = $env:PATH
$previousAllowlist = $env:ROOD_ASIO_ONLY
$results = @()
try {
    $env:PATH = "$paBin;$previousPath"
    for ($index = 0; $index -lt $DriverNames.Count; ++$index) {
        $name = $DriverNames[$index]
        $displayName = if ($name) { $name } else { '(all registered drivers)' }
        $prefix = '{0:d2}' -f $index
        $stdout = Join-Path $logDir "$prefix.stdout.txt"
        $stderr = Join-Path $logDir "$prefix.stderr.txt"
        Remove-Item -LiteralPath $stdout, $stderr -ErrorAction SilentlyContinue
        $env:ROOD_ASIO_ONLY = if ($name) { $name } else { $null }
        $started = Get-Date
        $process = Start-Process -FilePath $probe -ArgumentList '--list' `
            -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
            -WindowStyle Hidden -PassThru
        $timedOut = $false
        try {
            $process | Wait-Process -Timeout $TimeoutSeconds -ErrorAction Stop
        } catch {
            $process.Refresh()
            if (-not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                $process.WaitForExit()
                $timedOut = $true
            }
        }
        $process.Refresh()
        $text = if (Test-Path -LiteralPath $stdout) {
            Get-Content -LiteralPath $stdout -Raw
        } else { '' }
        $lines = @($text -split '\r?\n' | Where-Object {
            $_ -match '^\d+ \| ASIO \| '
        })
        $selected = if ($name) {
            @($lines | Where-Object {
                $_ -match '^\d+ \| ASIO \| (.+?) \|' -and $Matches[1] -eq $name
            })
        } else { $lines }
        $status = if ($timedOut) { 'timeout' }
            elseif ($process.ExitCode -ne 0) { 'error' }
            elseif ($selected.Count -gt 0 -and -not $name) { 'listed' }
            elseif ($selected.Count -gt 0) { 'available' }
            else { 'unavailable' }
        $results += [pscustomobject]@{
            driver = $displayName
            status = $status
            exitCode = if ($timedOut) { '' } else { $process.ExitCode }
            elapsedSeconds = [math]::Round(((Get-Date) - $started).TotalSeconds, 2)
            listedDevice = $selected -join ' | '
            stdout = [IO.Path]::GetFileName($stdout)
            stderr = [IO.Path]::GetFileName($stderr)
        }
        Write-Host "$prefix $status $displayName"
    }
} finally {
    $env:PATH = $previousPath
    $env:ROOD_ASIO_ONLY = $previousAllowlist
}
$summary = Join-Path $logDir 'summary.csv'
$results | Export-Csv -LiteralPath $summary -NoTypeInformation -Encoding utf8
Write-Host "ASIO registry audit: $summary"
