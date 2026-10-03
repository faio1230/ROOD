param([Parameter(Mandatory = $true)][string]$Path)

$ErrorActionPreference = 'Stop'
$file = (Resolve-Path -LiteralPath $Path).Path
$bytes = [IO.File]::ReadAllBytes($file)
if ($bytes.Length -lt 64) { throw "Not a PE image: $file" }

$peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
if ($peOffset -lt 64 -or $peOffset + 24 -gt $bytes.Length -or
    [Text.Encoding]::ASCII.GetString($bytes, $peOffset, 4) -ne "PE`0`0") {
    throw "Not a PE image: $file"
}
$optionalHeader = $peOffset + 24
$optionalSize = [int][BitConverter]::ToUInt16($bytes, $peOffset + 20)
if ($optionalHeader + $optionalSize -gt $bytes.Length) {
    throw "Truncated PE optional header: $file"
}
$magic = [BitConverter]::ToUInt16($bytes, $optionalHeader)
$dataDirectories = switch ($magic) {
    0x10b { $optionalHeader + 96 }
    0x20b { $optionalHeader + 112 }
    default { throw "Unsupported PE optional header: $file" }
}
$checksum = $optionalHeader + 64
$certificateDirectory = $dataDirectories + 8 * 4
if ($certificateDirectory + 8 -gt $optionalHeader + $optionalSize) {
    throw "PE certificate directory is missing: $file"
}

# The certificate directory uses a file offset, unlike most PE data directories.
# Reconstruct the bytes before signing; this is a flat SHA-1, not an Authenticode digest.
# https://learn.microsoft.com/en-us/windows/win32/debug/pe-format
$certificateOffset = [BitConverter]::ToUInt32($bytes, $certificateDirectory)
$certificateBytes = [BitConverter]::ToUInt32($bytes, $certificateDirectory + 4)
if ($certificateOffset -le $optionalHeader + $optionalSize -or
    $certificateBytes -lt 8 -or
    $certificateOffset % 8 -ne 0 -or
    ([long]$certificateOffset + [long]$certificateBytes) -ne $bytes.Length) {
    throw "PE certificate table is not an end-of-file table: $file"
}

$reconstructed = [byte[]]::new([int]$certificateOffset)
[Array]::Copy($bytes, 0, $reconstructed, 0, [int]$certificateOffset)
[Array]::Clear($reconstructed, $checksum, 4)
[Array]::Clear($reconstructed, $certificateDirectory, 8)
$sha1 = [Security.Cryptography.SHA1]::Create()
try {
    $hash = [BitConverter]::ToString($sha1.ComputeHash($reconstructed)).Replace('-', '').ToLowerInvariant()
} finally {
    $sha1.Dispose()
}
[pscustomobject]@{
    reconstructedSha1 = $hash
    certificateOffset = $certificateOffset
    certificateBytes = $certificateBytes
}
