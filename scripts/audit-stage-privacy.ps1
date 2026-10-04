param(
    [Parameter(Mandatory = $true)][string]$StagePath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$stage = (Resolve-Path -LiteralPath $StagePath).Path.TrimEnd([IO.Path]::DirectorySeparatorChar)
$output = [IO.Path]::GetFullPath($OutputPath)
if (-not $output.StartsWith($stage + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'The privacy report must be written inside the stage directory.'
}

$archiveExtensions = @('.7z', '.gz', '.xz', '.zip', '.tar')
$pathPattern = '(?i)(?:[A-Z]:[\\/]+(?:Users|Documents and Settings)[\\/]+[^\\/\x00\s"'';]+|/Users/[^/\x00\s"'';]+|/home/[^/\x00\s"'';]+)'
$profile = [Environment]::GetFolderPath('UserProfile')
$localPathPattern = '(?i)(?:' + ((@(
    $profile
    $profile.Replace('\', '/')
    $profile.Replace('\', '\\')
) | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')'
$credentialPattern = '(?<![A-Za-z0-9_-])(?:gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9_-]{20,}|-----BEGIN [A-Z ]+PRIVATE KEY-----|AKIA[0-9A-Z]{16}|xox[baprs]-[A-Za-z0-9-]{15,}|AIza[0-9A-Za-z_-]{30,})'
$singleByte = [Text.Encoding]::GetEncoding(28591)
$paths = [System.Collections.Generic.List[string]]::new()
$localPaths = [System.Collections.Generic.List[string]]::new()
$credentials = [System.Collections.Generic.List[string]]::new()
$scanned = 0
$skippedArchives = 0
foreach ($file in (Get-ChildItem -LiteralPath $stage -Recurse -File)) {
    if ($file.FullName.Equals($output, [StringComparison]::OrdinalIgnoreCase)) { continue }
    if ($archiveExtensions -contains $file.Extension.ToLowerInvariant()) {
        ++$skippedArchives
        continue
    }
    ++$scanned
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $ascii = $singleByte.GetString($bytes)
    $wide = [Text.Encoding]::Unicode.GetString($bytes)
    $relative = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
    if ([regex]::IsMatch($ascii, $pathPattern) -or
        [regex]::IsMatch($wide, $pathPattern)) {
        $paths.Add($relative)
    }
    if ([regex]::IsMatch($ascii, $localPathPattern) -or
        [regex]::IsMatch($wide, $localPathPattern)) {
        $localPaths.Add($relative)
    }
    if ([regex]::IsMatch($ascii, $credentialPattern) -or
        [regex]::IsMatch($wide, $credentialPattern)) {
        $credentials.Add($relative)
    }
}

$report = [pscustomobject]@{
    scope = 'Uncompressed staged files; filenames only, no matched content'
    scannedFiles = $scanned
    skippedArchives = $skippedArchives
    userProfilePathFiles = @($paths | Sort-Object -Unique)
    localUserProfilePathFiles = @($localPaths | Sort-Object -Unique)
    credentialPatternFiles = @($credentials | Sort-Object -Unique)
}
$report | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $output -Encoding utf8
$report
