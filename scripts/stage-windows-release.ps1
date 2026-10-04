param([switch]$SkipSmokeTest)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$build = Join-Path $repo 'build/msvc-media-release'
$dependencies = Join-Path $repo 'build/deps'
$qt = Join-Path $dependencies 'qt/6.10.3/msvc2022_64'
$qtArchiveName = 'qtbase-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
$qtArchive = Join-Path $dependencies "qt-archives/$qtArchiveName"
$qtArchiveSha256 = '4db84dee7fe3c558f242bef0a88852613af76580dc6d2b24596479f47004dad7'
$vcpkg = Join-Path $dependencies 'vcpkg-installed/x64-windows'
$pa = Join-Path $dependencies 'portaudio-msvc-install'
$spout = Join-Path $dependencies 'spout2-msvc-install'
$omt = Join-Path $dependencies 'omt-v1.0.0.16'
$qtSource = Join-Path $dependencies 'qt-source'
$qtSourceArchive = Join-Path $qtSource 'qtbase-everywhere-src-6.10.3.tar.xz'
$qtSourceSha256 = '383dc907816338f0cba72088a524c07458dfc69ce684ca9132fcc4fe91c24b0b'
$qtSourceUrl = 'https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz'
$qtSourceCommit = '7ddbc87d8e14ce51d2957ea72d0a6077593d5ff4'
$ffmpegArchive = Join-Path $dependencies 'vcpkg-src/downloads/ffmpeg-ffmpeg-n8.1.2.tar.gz'
$ffmpegArchiveSha512 = 'c72f4062aecc16d8b2b1e8678d5efe3af4cfaa0cc7c0997052248f9e499e60c2463acf07877cf3b78b246ce3e8078cb043e8d97e90a6b50d06af32ff7369a788'
$ffmpegPort = Join-Path $dependencies 'vcpkg-src/ports/ffmpeg'
$ffmpegBuild = Join-Path $dependencies 'vcpkg-src/buildtrees/ffmpeg/x64-windows-rel'
$srtArchive = Join-Path $dependencies 'vcpkg-src/downloads/Haivision-srt-v1.5.6.tar.gz'
$srtArchiveSha512 = '57641b35644b6bfa5998648fb808b615d11d8eab52fecb628a58414dbc87b1d781ea281c8ceef42a92f0c3796a05b41b8411bea95188ce751544f8195b7dbb66'
$srtPort = Join-Path $dependencies 'vcpkg-src/ports/libsrt'
$srtBuild = Join-Path $dependencies 'vcpkg-src/buildtrees/libsrt/x64-windows-rel'
$vcpkgRevision = '9e593bb18ea69cc5095e012465dcd675a822ed0d'
$cache = Join-Path $build 'CMakeCache.txt'
$paCache = Join-Path $dependencies 'portaudio-msvc-build/CMakeCache.txt'
if (-not (Test-Path -LiteralPath $cache) -or
    -not (Select-String -LiteralPath $cache -Pattern '^CMAKE_BUILD_TYPE:STRING=Release$' -Quiet)) {
    throw 'Release media build is missing. Run scripts/build-media-release-msvc.cmd first.'
}
if (-not (Test-Path -LiteralPath $paCache) -or
    -not (Select-String -LiteralPath $paCache -Pattern '^PA_USE_ASIO:BOOL=OFF$' -Quiet) -or
    -not (Select-String -LiteralPath $paCache -Pattern '^PA_USE_WASAPI:BOOL=ON$' -Quiet)) {
    throw 'The PortAudio dependency is not the verified WASAPI-only build.'
}
if (-not (Test-Path -LiteralPath $qtSourceArchive) -or
    (Get-FileHash -LiteralPath $qtSourceArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qtSourceSha256) {
    throw 'Qt source archive is missing or has the wrong hash. Run scripts/bootstrap-qt-source.ps1.'
}
$qtLicenses = Join-Path $qtSource 'LICENSES'
if (-not (Test-Path -LiteralPath (Join-Path $qtLicenses 'LGPL-3.0-only.txt')) -or
    -not (Test-Path -LiteralPath (Join-Path $qtLicenses 'GPL-3.0-only.txt')) -or
    (Get-ChildItem -LiteralPath $qtLicenses -File).Count -ne 38) {
    throw 'Qt license texts are missing. Run scripts/bootstrap-qt-source.ps1.'
}
$qtSbom = Join-Path $qt 'sbom/qtbase-6.10.3.spdx'
if (-not (Test-Path -LiteralPath $qtSbom) -or
    -not (Select-String -LiteralPath $qtSbom -Pattern $qtSourceCommit -SimpleMatch -Quiet)) {
    throw 'Qt binary SBOM does not match the verified source commit.'
}
if (-not (Test-Path -LiteralPath $qtArchive) -or
    (Get-FileHash -LiteralPath $qtArchive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $qtArchiveSha256) {
    throw 'Qt binary archive is missing or differs from the pinned official package. Run scripts/bootstrap-qt.ps1.'
}
$actualVcpkgRevision = (& git -C (Join-Path $dependencies 'vcpkg-src') rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $actualVcpkgRevision -ne $vcpkgRevision -or
    (Get-Content -LiteralPath (Join-Path $repo 'vcpkg.json') -Raw | ConvertFrom-Json).'builtin-baseline' -ne $vcpkgRevision) {
    throw 'vcpkg checkout and manifest do not match the pinned revision.'
}
if (-not (Test-Path -LiteralPath $ffmpegArchive) -or
    (Get-FileHash -LiteralPath $ffmpegArchive -Algorithm SHA512).Hash.ToLowerInvariant() -ne $ffmpegArchiveSha512) {
    throw 'FFmpeg source archive is missing or does not match the pinned vcpkg port.'
}
$ffmpegPortfile = Join-Path $ffmpegPort 'portfile.cmake'
$portText = Get-Content -LiteralPath $ffmpegPortfile -Raw
if ($portText -notmatch [regex]::Escape($ffmpegArchiveSha512)) {
    throw 'FFmpeg portfile has a different source archive hash.'
}
$patchBlock = [regex]::Match($portText, '(?ms)^\s*PATCHES\s*\r?\n(?<names>.*?)^\)')
if (-not $patchBlock.Success) { throw 'Could not read the FFmpeg port patch list.' }
$ffmpegPatches = @([regex]::Matches($patchBlock.Groups['names'].Value, '(?m)^\s*(\S+\.patch)(?:\s+#.*)?$') |
    ForEach-Object { $_.Groups[1].Value })
if ($ffmpegPatches.Count -ne 14) { throw 'The FFmpeg port patch list has changed.' }
if (-not (Test-Path -LiteralPath $srtArchive) -or
    (Get-FileHash -LiteralPath $srtArchive -Algorithm SHA512).Hash.ToLowerInvariant() -ne $srtArchiveSha512) {
    throw 'libsrt source archive is missing or does not match the pinned vcpkg port.'
}
$srtPortfile = Join-Path $srtPort 'portfile.cmake'
$srtPortText = Get-Content -LiteralPath $srtPortfile -Raw
if ($srtPortText -notmatch [regex]::Escape($srtArchiveSha512)) {
    throw 'libsrt portfile has a different source archive hash.'
}
$srtPatchBlock = [regex]::Match($srtPortText, '(?ms)^\s*PATCHES\s*\r?\n(?<names>.*?)^\)')
if (-not $srtPatchBlock.Success) { throw 'Could not read the libsrt port patch list.' }
$srtPatches = @([regex]::Matches($srtPatchBlock.Groups['names'].Value,
    '(?m)^\s*(\S+\.(?:patch|diff))(?:\s+#.*)?$') | ForEach-Object { $_.Groups[1].Value })
if ($srtPatches.Count -ne 3) { throw 'The libsrt port patch list has changed.' }

$stageRoot = Join-Path $repo 'build/stage'
New-Item -ItemType Directory -Path $stageRoot -Force | Out-Null
$stage = Join-Path $stageRoot ('ROOD-Windows-x64-' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
if (Test-Path -LiteralPath $stage) { throw "Stage directory already exists: $stage" }
New-Item -ItemType Directory -Path $stage | Out-Null

$files = @(
    @{ Source = (Join-Path $repo 'LICENSE'); Target = 'LICENSE'; Component = 'ROOD' }
    @{ Source = (Join-Path $repo 'docs/third-party-notices.md'); Target = 'THIRD-PARTY-NOTICES.md'; Component = 'Third-party notices' }
    @{ Source = (Join-Path $build 'rood_gui.exe'); Target = 'rood_gui.exe'; Component = 'ROOD' }
    @{ Source = (Join-Path $build 'rood_ingest.exe'); Target = 'rood_ingest.exe'; Component = 'ROOD' }
    @{ Source = (Join-Path $build 'rood_deps_probe.exe'); Target = 'rood_deps_probe.exe'; Component = 'ROOD' }
    @{ Source = (Join-Path $build 'rood_pa_probe.exe'); Target = 'rood_pa_probe.exe'; Component = 'ROOD' }
    @{ Source = (Join-Path $qt 'bin/Qt6Core.dll'); Target = 'Qt6Core.dll'; Component = 'Qt 6.10.3' }
    @{ Source = (Join-Path $qt 'bin/Qt6Gui.dll'); Target = 'Qt6Gui.dll'; Component = 'Qt 6.10.3' }
    @{ Source = (Join-Path $qt 'bin/Qt6Widgets.dll'); Target = 'Qt6Widgets.dll'; Component = 'Qt 6.10.3' }
    @{ Source = (Join-Path $qt 'plugins/platforms/qwindows.dll'); Target = 'platforms/qwindows.dll'; Component = 'Qt 6.10.3' }
    @{ Source = (Join-Path $vcpkg 'bin/avcodec-62.dll'); Target = 'avcodec-62.dll'; Component = 'FFmpeg 8.1.2' }
    @{ Source = (Join-Path $vcpkg 'bin/avformat-62.dll'); Target = 'avformat-62.dll'; Component = 'FFmpeg 8.1.2' }
    @{ Source = (Join-Path $vcpkg 'bin/avutil-60.dll'); Target = 'avutil-60.dll'; Component = 'FFmpeg 8.1.2' }
    @{ Source = (Join-Path $vcpkg 'bin/swresample-6.dll'); Target = 'swresample-6.dll'; Component = 'FFmpeg 8.1.2' }
    @{ Source = (Join-Path $vcpkg 'bin/swscale-9.dll'); Target = 'swscale-9.dll'; Component = 'FFmpeg 8.1.2' }
    @{ Source = (Join-Path $vcpkg 'bin/srt.dll'); Target = 'srt.dll'; Component = 'libsrt 1.5.6' }
    @{ Source = (Join-Path $vcpkg 'bin/libcrypto-3-x64.dll'); Target = 'libcrypto-3-x64.dll'; Component = 'OpenSSL 3.6.3' }
    @{ Source = (Join-Path $pa 'bin/portaudio_x64.dll'); Target = 'portaudio_x64.dll'; Component = 'PortAudio v19.7.0 WASAPI' }
    @{ Source = (Join-Path $spout 'bin/SpoutLibrary.dll'); Target = 'SpoutLibrary.dll'; Component = 'Spout2 2.007.017' }
    @{ Source = (Join-Path $spout 'bin/Spout.dll'); Target = 'Spout.dll'; Component = 'Spout2 2.007.017' }
    @{ Source = (Join-Path $omt 'Libraries/Winx64/libomt.dll'); Target = 'libomt.dll'; Component = 'OMT v1.0.0.16' }
    @{ Source = (Join-Path $omt 'Libraries/Winx64/libomtnet.dll'); Target = 'libomtnet.dll'; Component = 'OMT v1.0.0.16' }
    @{ Source = (Join-Path $omt 'Libraries/Winx64/libvmx.dll'); Target = 'libvmx.dll'; Component = 'OMT v1.0.0.16' }
    @{ Source = (Join-Path $vcpkg 'share/ffmpeg/copyright'); Target = 'licenses/FFmpeg-copyright.txt'; Component = 'FFmpeg 8.1.2' }
    @{ Source = (Join-Path $vcpkg 'share/libsrt/copyright'); Target = 'licenses/libsrt-copyright.txt'; Component = 'libsrt 1.5.6' }
    @{ Source = (Join-Path $vcpkg 'share/openssl/copyright'); Target = 'licenses/OpenSSL-copyright.txt'; Component = 'OpenSSL 3.6.3' }
    @{ Source = (Join-Path $dependencies 'portaudio-src/LICENSE.txt'); Target = 'licenses/PortAudio-LICENSE.txt'; Component = 'PortAudio v19.7.0 WASAPI' }
    @{ Source = (Join-Path $dependencies 'spout2-src/LICENSE'); Target = 'licenses/Spout2-LICENSE.txt'; Component = 'Spout2 2.007.017' }
    @{ Source = (Join-Path $omt 'LICENSE.txt'); Target = 'licenses/OMT-LICENSE.txt'; Component = 'OMT v1.0.0.16' }
    @{ Source = (Join-Path $qt 'sbom/qtbase-6.10.3.spdx'); Target = 'licenses/Qt-qtbase-6.10.3.spdx'; Component = 'Qt 6.10.3' }
    @{ Source = (Join-Path $qt 'sbom/qtbase-6.10.3.source.spdx'); Target = 'licenses/Qt-qtbase-6.10.3.source.spdx'; Component = 'Qt 6.10.3' }
    @{ Source = (Join-Path $qtSource 'SOURCE-REFERENCE.txt'); Target = 'licenses/Qt-SOURCE-REFERENCE.txt'; Component = 'Qt 6.10.3' }
    @{ Source = $qtArchive; Target = "source/Qt/$qtArchiveName"; Component = 'Qt 6.10.3 binary provenance' }
    @{ Source = $qtSourceArchive; Target = 'source/Qt/qtbase-everywhere-src-6.10.3.tar.xz'; Component = 'Qt 6.10.3 source' }
    @{ Source = $ffmpegArchive; Target = 'source/FFmpeg/ffmpeg-n8.1.2.tar.gz'; Component = 'FFmpeg 8.1.2 source' }
    @{ Source = $ffmpegPortfile; Target = 'source/FFmpeg/vcpkg-portfile.cmake'; Component = 'FFmpeg 8.1.2 source' }
    @{ Source = (Join-Path $repo 'vcpkg.json'); Target = 'source/FFmpeg/rood-vcpkg.json'; Component = 'FFmpeg 8.1.2 source' }
    @{ Source = (Join-Path $ffmpegBuild 'ffbuild/config.sh'); Target = 'source/FFmpeg/ffbuild-config.sh'; Component = 'FFmpeg 8.1.2 build' }
    @{ Source = (Join-Path $ffmpegBuild 'ffbuild/config.mak'); Target = 'source/FFmpeg/ffbuild-config.mak'; Component = 'FFmpeg 8.1.2 build' }
    @{ Source = (Join-Path $ffmpegBuild 'config.h'); Target = 'source/FFmpeg/config.h'; Component = 'FFmpeg 8.1.2 build' }
    @{ Source = $srtArchive; Target = 'source/libsrt/srt-v1.5.6.tar.gz'; Component = 'libsrt 1.5.6 source' }
    @{ Source = $srtPortfile; Target = 'source/libsrt/vcpkg-portfile.cmake'; Component = 'libsrt 1.5.6 source' }
    @{ Source = (Join-Path $repo 'vcpkg.json'); Target = 'source/libsrt/rood-vcpkg.json'; Component = 'libsrt 1.5.6 source' }
    @{ Source = (Join-Path $srtBuild 'CMakeCache.txt'); Target = 'source/libsrt/CMakeCache.txt'; Component = 'libsrt 1.5.6 build' }
)
foreach ($patch in $ffmpegPatches) {
    $files += @{
        Source = (Join-Path $ffmpegPort $patch)
        Target = "source/FFmpeg/patches/$patch"
        Component = 'FFmpeg 8.1.2 source'
    }
}
foreach ($patch in $srtPatches) {
    $files += @{
        Source = (Join-Path $srtPort $patch)
        Target = "source/libsrt/patches/$patch"
        Component = 'libsrt 1.5.6 source'
    }
}
foreach ($license in (Get-ChildItem -LiteralPath $qtLicenses -File | Sort-Object Name)) {
    $files += @{
        Source = $license.FullName
        Target = "licenses/Qt-LICENSES/$($license.Name)"
        Component = 'Qt 6.10.3'
    }
}

$entries = foreach ($file in $files) {
    if (-not (Test-Path -LiteralPath $file.Source -PathType Leaf)) {
        throw "A required staging file is missing: $($file.Source)"
    }
    $destination = Join-Path $stage $file.Target
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
    Copy-Item -LiteralPath $file.Source -Destination $destination
    [pscustomobject]@{
        path = ($file.Target -replace '\\', '/')
        component = $file.Component
        bytes = (Get-Item -LiteralPath $destination).Length
        sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$qtArchiveFiles = @(
    'bin/Qt6Core.dll'
    'bin/Qt6Gui.dll'
    'bin/Qt6Widgets.dll'
    'plugins/platforms/qwindows.dll'
    'sbom/qtbase-6.10.3.spdx'
)
$qtRawDir = Join-Path $stage '.qt-archive-verification'
New-Item -ItemType Directory -Path $qtRawDir | Out-Null
& tar -xf $qtArchive -C $qtRawDir -- $qtArchiveFiles
if ($LASTEXITCODE -ne 0) { throw 'Could not extract the Qt files from the pinned official archive.' }
foreach ($pair in @(
    @{ Archive = 'bin/Qt6Core.dll'; Staged = 'Qt6Core.dll' }
    @{ Archive = 'bin/Qt6Gui.dll'; Staged = 'Qt6Gui.dll' }
    @{ Archive = 'bin/Qt6Widgets.dll'; Staged = 'Qt6Widgets.dll' }
    @{ Archive = 'plugins/platforms/qwindows.dll'; Staged = 'platforms/qwindows.dll' }
    @{ Archive = 'sbom/qtbase-6.10.3.spdx'; Staged = 'licenses/Qt-qtbase-6.10.3.spdx' }
)) {
    $archiveFile = Join-Path $qtRawDir $pair.Archive
    $stagedFile = Join-Path $stage $pair.Staged
    if (-not (Test-Path -LiteralPath $archiveFile -PathType Leaf) -or
        (Get-FileHash -LiteralPath $archiveFile -Algorithm SHA256).Hash -ne
        (Get-FileHash -LiteralPath $stagedFile -Algorithm SHA256).Hash) {
        throw "A staged Qt file differs from the pinned official archive: $($pair.Staged)"
    }
}
$resolvedStage = (Resolve-Path -LiteralPath $stage).Path.TrimEnd([IO.Path]::DirectorySeparatorChar)
$resolvedQtRawDir = (Resolve-Path -LiteralPath $qtRawDir).Path
if (-not $resolvedQtRawDir.StartsWith($resolvedStage + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Qt verification cleanup path escaped the stage directory.'
}
Remove-Item -LiteralPath $resolvedQtRawDir -Recurse -Force

$qtSbomText = Get-Content -LiteralPath $qtSbom -Raw
$qtBinaryAudit = foreach ($binary in @(
    @{ SbomPath = 'bin/Qt6Core.dll'; StagePath = 'Qt6Core.dll' }
    @{ SbomPath = 'bin/Qt6Gui.dll'; StagePath = 'Qt6Gui.dll' }
    @{ SbomPath = 'bin/Qt6Widgets.dll'; StagePath = 'Qt6Widgets.dll' }
    @{ SbomPath = 'plugins/platforms/qwindows.dll'; StagePath = 'platforms/qwindows.dll' }
)) {
    $filePattern = '(?ms)^FileName: \./' + [regex]::Escape($binary.SbomPath) +
        '\r?\n(?<attributes>.*?)(?=^\s*$)'
    $sbomFile = [regex]::Match($qtSbomText, $filePattern)
    if (-not $sbomFile.Success) { throw "Qt SBOM entry is missing: $($binary.SbomPath)" }
    $checksum = [regex]::Match($sbomFile.Groups['attributes'].Value,
        '(?m)^FileChecksum: SHA1: (?<hash>[0-9a-fA-F]{40})\r?$')
    if (-not $checksum.Success) { throw "Qt SBOM SHA1 is missing: $($binary.SbomPath)" }
    $stagedPath = Join-Path $stage $binary.StagePath
    $signature = Get-AuthenticodeSignature -LiteralPath $stagedPath
    $reconstruction = & (Join-Path $PSScriptRoot 'measure-pe-without-certificate.ps1') -Path $stagedPath
    $expected = $checksum.Groups['hash'].Value.ToLowerInvariant()
    $actual = (Get-FileHash -LiteralPath $stagedPath -Algorithm SHA1).Hash.ToLowerInvariant()
    [pscustomobject]@{
        file = $binary.StagePath
        sbomSha1 = $expected
        stagedSha1 = $actual
        sha1Matches = ($expected -eq $actual)
        reconstructedSha1 = $reconstruction.reconstructedSha1
        reconstructedSha1Matches = ($expected -eq $reconstruction.reconstructedSha1)
        certificateOffset = $reconstruction.certificateOffset
        certificateBytes = $reconstruction.certificateBytes
        signatureStatus = [string]$signature.Status
        signer = if ($signature.SignerCertificate) { $signature.SignerCertificate.Subject } else { $null }
    }
}
$qtAuditPath = Join-Path $stage 'licenses/Qt-SBOM-CHECKSUM-AUDIT.json'
$qtBinaryAudit | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $qtAuditPath -Encoding utf8
$entries += [pscustomobject]@{
    path = 'licenses/Qt-SBOM-CHECKSUM-AUDIT.json'
    component = 'Qt 6.10.3 audit'
    bytes = (Get-Item -LiteralPath $qtAuditPath).Length
    sha256 = (Get-FileHash -LiteralPath $qtAuditPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
$qtInventoryPath = Join-Path $stage 'licenses/Qt-THIRD-PARTY-SBOM-INVENTORY.json'
& (Join-Path $PSScriptRoot 'audit-qt-third-party.ps1') `
    -SbomPath $qtSbom -OutputPath $qtInventoryPath
$qtInventory = Get-Content -LiteralPath $qtInventoryPath -Raw | ConvertFrom-Json
$qtUnassertedCount = @($qtInventory.packages | Where-Object licenseConcluded -eq 'NOASSERTION').Count
$entries += [pscustomobject]@{
    path = 'licenses/Qt-THIRD-PARTY-SBOM-INVENTORY.json'
    component = 'Qt 6.10.3 audit'
    bytes = (Get-Item -LiteralPath $qtInventoryPath).Length
    sha256 = (Get-FileHash -LiteralPath $qtInventoryPath -Algorithm SHA256).Hash.ToLowerInvariant()
}
$qtMismatchCount = @($qtBinaryAudit | Where-Object { -not $_.sha1Matches }).Count
$qtReconstructionMismatchCount = @($qtBinaryAudit | Where-Object { -not $_.reconstructedSha1Matches }).Count
$qtChecksumStatus = if ($qtMismatchCount -eq 0) {
    'All four staged Qt binary SHA-1 values match their SBOM entries.'
} elseif ($qtReconstructionMismatchCount -eq 0) {
    "The $qtMismatchCount raw Qt DLL hashes differ from the SBOM. All four SHA-1 values match after removing the end-of-file PE certificate table and zeroing the certificate directory and checksum."
} else {
    "The Qt SBOM audit found $qtMismatchCount raw DLL mismatches; $qtReconstructionMismatchCount remain after removing the PE signing fields."
}

$commit = (& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Could not read the ROOD Git revision.' }
$gitDirty = [bool](@(& git -C $repo status --porcelain).Count)
if ($LASTEXITCODE -ne 0) { throw 'Could not read the ROOD working tree status.' }
$manifest = [ordered]@{
    project = 'ROOD'
    profile = 'windows-msvc-media-release'
    gitCommit = $commit
    gitDirty = $gitDirty
    qtSourceUrl = $qtSourceUrl
    qtSourceSha256 = $qtSourceSha256
    qtSourceCommit = $qtSourceCommit
    qtOfficialArchiveSha256 = $qtArchiveSha256
    qtStagedFilesMatchOfficialArchive = $true
    qtSbomBinaryChecksumsMatch = ($qtMismatchCount -eq 0)
    qtSbomReconstructedChecksumsMatch = ($qtReconstructionMismatchCount -eq 0)
    qtThirdPartySbomPackageCount = $qtInventory.packageCount
    qtThirdPartySbomUnassertedCount = $qtUnassertedCount
    ffmpegSourceArchiveSha512 = $ffmpegArchiveSha512
    libsrtSourceArchiveSha512 = $srtArchiveSha512
    vcpkgRevision = $vcpkgRevision
    publishable = $false
    files = $entries
}
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $stage 'manifest.json') -Encoding utf8
@"
ROOD Windows x64 staging audit — NOT READY FOR PUBLIC DISTRIBUTION

This directory is a local dependency and startup check. It uses the WASAPI-only
PortAudio build and contains no Steinberg ASIO SDK or ASIO-enabled binary.
The ROOD-owned code license is MIT and is included as LICENSE.

Before public distribution:
- Review the bundled Qt license texts and binary/source SBOMs against the DLLs.
  The verified qtbase 6.10.3 source archive and matching SBOM commit are
  recorded in licenses/Qt-SOURCE-REFERENCE.txt; confirm the public source
  access method and third-party notices before distribution.
  The bundled Qt DLLs and SBOM were compared byte-for-byte with the pinned
  official binary archive under source/Qt; all five files match that archive.
  $qtChecksumStatus
  The per-file comparison is in licenses/Qt-SBOM-CHECKSUM-AUDIT.json.
  The reconstruction is a flat SHA-1 comparison, not an Authenticode digest.
  The Qt SBOM dependency inventory lists $($qtInventory.packageCount) third-party packages,
  including $qtUnassertedCount with no license conclusion. See licenses/Qt-THIRD-PARTY-SBOM-INVENTORY.json;
  the unasserted WrapAtomic entry is an INTERFACE IMPORTED CMake target in
  cmake/FindWrapAtomic.cmake, with no separate file staged. Review actual
  inclusion and the full notices in the bundled SPDX and source.
- Review all bundled notices and matching FFmpeg/libsrt/Qt source and build data.
  THIRD-PARTY-NOTICES.md lists the staged components and local source paths,
  but its contents still need a final review against the release files.
  FFmpeg and libsrt source archives, vcpkg patches and Release build settings
  are retained under source/ for review. Confirm their public source access.
- Verify the required Microsoft Visual C++ runtime on a clean Windows machine.
- Complete hardware ASIO, physical AV timing and active device-removal testing.

manifest.json lists the staged files and SHA-256 hashes. Do not publish this
directory merely because its local smoke test succeeds.
"@ | Set-Content -LiteralPath (Join-Path $stage 'STAGING-STATUS.txt') -Encoding utf8

if (-not $SkipSmokeTest) {
    $auditDir = Join-Path $repo 'build/tests/staged-release'
    New-Item -ItemType Directory -Path $auditDir -Force | Out-Null
    $previousPath = $env:PATH
    try {
        $env:PATH = "$env:SystemRoot\System32;$env:SystemRoot"
        $probeStdout = Join-Path $auditDir 'deps.stdout.txt'
        $probeStderr = Join-Path $auditDir 'deps.stderr.txt'
        $probeProcess = Start-Process -FilePath (Join-Path $stage 'rood_deps_probe.exe') `
            -WorkingDirectory $stage -RedirectStandardOutput $probeStdout `
            -RedirectStandardError $probeStderr -WindowStyle Hidden -PassThru
        try {
            $probeProcess | Wait-Process -Timeout 15 -ErrorAction Stop
        } catch {
            $probeProcess.Refresh()
            if (-not $probeProcess.HasExited) { Stop-Process -Id $probeProcess.Id -Force }
            throw "Staged dependency probe timed out. See $auditDir"
        }
        $probeProcess.Refresh()
        if ($probeProcess.ExitCode -ne 0 -or
            (Get-Content -LiteralPath $probeStdout -Raw) -notmatch 'FFmpeg license: LGPL') {
            throw "Staged dependency probe failed. See $auditDir"
        }
        $paStdout = Join-Path $auditDir 'portaudio.stdout.txt'
        $paStderr = Join-Path $auditDir 'portaudio.stderr.txt'
        $paProcess = Start-Process -FilePath (Join-Path $stage 'rood_pa_probe.exe') `
            -ArgumentList '--list' -WorkingDirectory $stage `
            -RedirectStandardOutput $paStdout -RedirectStandardError $paStderr `
            -WindowStyle Hidden -PassThru
        try {
            $paProcess | Wait-Process -Timeout 15 -ErrorAction Stop
        } catch {
            $paProcess.Refresh()
            if (-not $paProcess.HasExited) { Stop-Process -Id $paProcess.Id -Force }
            throw "Staged PortAudio probe timed out. See $auditDir"
        }
        $paProcess.Refresh()
        if ($paProcess.ExitCode -ne 0 -or
            (Get-Content -LiteralPath $paStdout -Raw) -match '(?m)^\d+ \| ASIO \|') {
            throw "Staged PortAudio binary includes ASIO or failed to enumerate. See $auditDir"
        }
        $ingestStdout = Join-Path $auditDir 'ingest.stdout.txt'
        $ingestStderr = Join-Path $auditDir 'ingest.stderr.txt'
        $ingestPort = Get-Random -Minimum 20000 -Maximum 50000
        $ingestProcess = Start-Process -FilePath (Join-Path $stage 'rood_ingest.exe') `
            -ArgumentList @('--port', "$ingestPort", '--seconds', '1') `
            -WorkingDirectory $stage -RedirectStandardOutput $ingestStdout `
            -RedirectStandardError $ingestStderr -WindowStyle Hidden -PassThru
        try {
            $ingestProcess | Wait-Process -Timeout 10 -ErrorAction Stop
        } catch {
            $ingestProcess.Refresh()
            if (-not $ingestProcess.HasExited) { Stop-Process -Id $ingestProcess.Id -Force }
            throw "Staged SRT listener timed out. See $auditDir"
        }
        $ingestProcess.Refresh()
        $ingestOutput = Get-Content -LiteralPath $ingestStdout -Raw
        if ($ingestProcess.ExitCode -ne 0 -or
            $ingestOutput -notmatch 'state listening' -or
            $ingestOutput -notmatch 'state stopped') {
            throw "Staged SRT listener failed. See $auditDir"
        }
        $guiStdout = Join-Path $auditDir 'gui.stdout.txt'
        $guiStderr = Join-Path $auditDir 'gui.stderr.txt'
        $guiProcess = Start-Process -FilePath (Join-Path $stage 'rood_gui.exe') `
            -WorkingDirectory $stage -RedirectStandardOutput $guiStdout `
            -RedirectStandardError $guiStderr -WindowStyle Hidden -PassThru
        try {
            Start-Sleep -Seconds 2
            $guiProcess.Refresh()
            if ($guiProcess.HasExited) {
                throw "Staged GUI exited during startup (code $($guiProcess.ExitCode)). See $auditDir"
            }
        } finally {
            $guiProcess.Refresh()
            if (-not $guiProcess.HasExited) { Stop-Process -Id $guiProcess.Id -Force }
        }
    } finally {
        $env:PATH = $previousPath
    }
}

Write-Host "Local release staging passed: $stage"
Write-Host 'Public distribution is still blocked by STAGING-STATUS.txt items.'
