param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$MediaBin
)

$ErrorActionPreference = 'Stop'
$env:PATH = (Resolve-Path $MediaBin).Path + ';' + $env:PATH
& $Executable
exit $LASTEXITCODE
