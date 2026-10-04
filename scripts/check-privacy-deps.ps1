$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$bins = @(
    (Join-Path $repo 'build/deps/vcpkg-privacy-installed/x64-windows/bin')
    (Join-Path $repo 'build/deps/portaudio-privacy-install/bin')
)
$scanned = 0
foreach ($bin in $bins) {
    if (-not (Test-Path -LiteralPath $bin -PathType Container)) {
        throw 'A privacy dependency runtime directory is missing.'
    }
    $report = & (Join-Path $PSScriptRoot 'audit-stage-privacy.ps1') `
        -StagePath $bin -OutputPath (Join-Path $bin 'PRIVACY-AUDIT.json') -OnlyDlls
    if ($report.scannedFiles -eq 0 -or
        @($report.localUserProfilePathFiles).Count -gt 0 -or
        @($report.credentialPatternFiles).Count -gt 0) {
        throw "Runtime DLL privacy audit failed: $bin"
    }
    $scanned += $report.scannedFiles
}
Write-Host "Runtime dependency DLL privacy audit passed: $scanned files, no local user path or credential pattern."
