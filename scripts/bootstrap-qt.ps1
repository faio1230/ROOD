$ErrorActionPreference = 'Stop'

$repository = Split-Path -Parent $PSScriptRoot
$deps = Join-Path $repository 'build/deps'
$qtRoot = Join-Path $deps 'qt'
$qtConfig = Join-Path $qtRoot '6.10.3/msvc2022_64/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake'
$venv = Join-Path $deps 'qt-installer-venv'
$archiveDir = Join-Path $deps 'qt-archives'
$archiveName = 'qtbase-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
$archive = Join-Path $archiveDir $archiveName
$archiveSha256 = '4db84dee7fe3c558f242bef0a88852613af76580dc6d2b24596479f47004dad7'
$archiveUrl = 'https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6103/qt6_6103/qt.qt6.6103.win64_msvc2022_64/6.10.3-0-202603310407' + $archiveName

if (Test-Path -LiteralPath $qtConfig) {
    if (-not (Test-Path -LiteralPath $archive)) {
        New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
        Invoke-WebRequest -Uri $archiveUrl -OutFile $archive
    }
    if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archiveSha256) {
        throw 'Qt 6.10.3 binary archive hash differs from the pinned official package.'
    }
    Write-Host "Qt Widgets 6.10.3 is already available at $qtRoot"
    exit 0
}

New-Item -ItemType Directory -Force -Path $archiveDir | Out-Null
if (-not (Test-Path -LiteralPath (Join-Path $venv 'Scripts/python.exe'))) {
    $pathPythonVersion = if (Get-Command python -ErrorAction SilentlyContinue) {
        & python -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")'
    } else { '' }
    if ($LASTEXITCODE -eq 0 -and $pathPythonVersion -eq '3.11') {
        & python -m venv $venv
    } else {
        & py -3.11 -m venv $venv
    }
    if ($LASTEXITCODE -ne 0) { throw 'Python 3.11 virtual environment creation failed' }
}

& (Join-Path $venv 'Scripts/python.exe') -m pip install --disable-pip-version-check 'aqtinstall==3.3.0'
if ($LASTEXITCODE -ne 0) { throw 'aqtinstall installation failed' }

& (Join-Path $venv 'Scripts/aqt.exe') install-qt windows desktop 6.10.3 win64_msvc2022_64 -O $qtRoot --archives qtbase -k -d $archiveDir
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $qtConfig)) {
    throw 'Qt 6.10.3 Widgets installation failed'
}
if (-not (Test-Path -LiteralPath $archive) -or
    (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $archiveSha256) {
    throw 'Qt 6.10.3 binary archive hash differs from the pinned official package.'
}

Write-Host "Qt Widgets 6.10.3 installed at $qtRoot"
