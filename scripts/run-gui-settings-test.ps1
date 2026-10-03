param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$PortAudioBin
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bins = @(
    'build/deps/qt/6.10.3/msvc2022_64/bin'
    'build/deps/vcpkg-installed/x64-windows/bin'
    'build/deps/spout2-msvc-install/bin'
    'build/deps/omt-v1.0.0.16/Libraries/Winx64'
) | ForEach-Object { Join-Path $repo $_ }
$env:PATH = $PortAudioBin + ';' + ($bins -join ';') + ';' + $env:PATH
$env:QT_QPA_PLATFORM = 'offscreen'
$env:ROOD_ASIO_ONLY = 'ROOD GUI settings test: no ASIO driver'
& $Executable
exit $LASTEXITCODE
