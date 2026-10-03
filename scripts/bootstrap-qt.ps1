$ErrorActionPreference = 'Stop'

$repository = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $repository 'build/deps'
$qtRoot = Join-Path $deps 'qt'
$qtConfig = Join-Path $qtRoot '6.10.3/msvc2022_64/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake'
$venv = Join-Path $deps 'qt-installer-venv'

if (Test-Path -LiteralPath $qtConfig) {
    Write-Host "Qt Widgets 6.10.3 is already available at $qtRoot"
    exit 0
}

New-Item -ItemType Directory -Force -Path $deps | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $venv 'Scripts/python.exe'))) {
    py -3.11 -m venv $venv
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 virtual environment creation failed' }
}

& (Join-Path $venv 'Scripts/python.exe') -m pip install --disable-pip-version-check 'aqtinstall==3.3.0'
if ($LASTEXITCODE -ne 0) { throw 'aqtinstall installation failed' }

& (Join-Path $venv 'Scripts/aqt.exe') install-qt windows desktop 6.10.3 win64_msvc2022_64 -O $qtRoot --archives qtbase
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $qtConfig)) {
    throw 'Qt 6.10.3 Widgets installation failed'
}

Write-Host "Qt Widgets 6.10.3 installed at $qtRoot"
