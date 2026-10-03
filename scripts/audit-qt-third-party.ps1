param(
    [Parameter(Mandatory = $true)][string]$SbomPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
$packages = @{}
$dependencies = @{}
$current = $null

function Save-Package {
    param($Package)
    if ($null -eq $Package) { return }
    if (-not $Package.spdxId) {
        throw "Qt SBOM package has no SPDX ID: $($Package.name)"
    }
    $packages[$Package.spdxId] = $Package
}

foreach ($line in Get-Content -LiteralPath $SbomPath) {
    if ($line -match '^PackageName: (.+)$') {
        Save-Package $current
        $current = [ordered]@{
            name = $Matches[1]
            spdxId = ''
            version = ''
            licenseConcluded = ''
            licenseDeclared = ''
            downloadLocation = ''
        }
    } elseif ($line -match '^(?:FileName|LicenseID): ') {
        Save-Package $current
        $current = $null
    } elseif ($null -ne $current) {
        if ($line -match '^SPDXID: (\S+)$') { $current.spdxId = $Matches[1] }
        elseif ($line -match '^PackageVersion: (.+)$') { $current.version = $Matches[1] }
        elseif ($line -match '^PackageLicenseConcluded: (.+)$') {
            $current.licenseConcluded = $Matches[1]
        } elseif ($line -match '^PackageLicenseDeclared: (.+)$') {
            $current.licenseDeclared = $Matches[1]
        } elseif ($line -match '^PackageDownloadLocation: (.+)$') {
            $current.downloadLocation = $Matches[1]
        }
    }
    if ($line -match '^Relationship: (\S+) DEPENDS_ON (\S+)$') {
        $source = $Matches[1]
        $target = $Matches[2]
        if (-not $dependencies.ContainsKey($source)) {
            $dependencies[$source] = [System.Collections.Generic.List[string]]::new()
        }
        $dependencies[$source].Add($target)
    }
}
Save-Package $current

$usedBy = @{}
foreach ($root in @(
    @{ Label = 'QtCore'; Pattern = '^SPDXRef-Package-qtbase-qt-module-Core-[^-]+$' }
    @{ Label = 'QtGui'; Pattern = '^SPDXRef-Package-qtbase-qt-module-Gui-[^-]+$' }
    @{ Label = 'QtWidgets'; Pattern = '^SPDXRef-Package-qtbase-qt-module-Widgets-[^-]+$' }
    @{ Label = 'qwindows'; Pattern = '^SPDXRef-Package-qtbase-qt-plugin-QWindowsIntegrationPlugin-[^-]+$' }
)) {
    $ids = @($packages.Keys | Where-Object { $_ -match $root.Pattern })
    if ($ids.Count -ne 1) { throw "Could not identify one Qt SBOM root: $($root.Label)" }
    $pending = [System.Collections.Generic.Stack[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new()
    $pending.Push($ids[0])
    while ($pending.Count -gt 0) {
        $id = $pending.Pop()
        if (-not $seen.Add($id)) { continue }
        if ($id -match '3rdparty') {
            if (-not $packages.ContainsKey($id)) {
                throw "Qt third-party dependency has no package entry: $id"
            }
            if (-not $usedBy.ContainsKey($id)) {
                $usedBy[$id] = [System.Collections.Generic.List[string]]::new()
            }
            $usedBy[$id].Add($root.Label)
        }
        if ($dependencies.ContainsKey($id)) {
            foreach ($target in $dependencies[$id]) { $pending.Push($target) }
        }
    }
}

if ($usedBy.Count -lt 20) { throw 'Qt third-party inventory is unexpectedly small.' }
$inventory = @($usedBy.Keys | ForEach-Object {
    $package = $packages[$_]
    if (-not $package.licenseConcluded) {
        throw "Qt third-party package has no license conclusion: $_"
    }
    [pscustomobject]@{
        name = $package.name
        version = $package.version
        licenseConcluded = $package.licenseConcluded
        licenseDeclared = $package.licenseDeclared
        downloadLocation = $package.downloadLocation
        usedBy = @($usedBy[$_] | Sort-Object)
        spdxId = $_
    }
} | Sort-Object name, spdxId)

$report = [ordered]@{
    sbom = Split-Path -Leaf $SbomPath
    scope = 'SPDX DEPENDS_ON closure for the staged QtCore, QtGui, QtWidgets and qwindows packages; this is an audit inventory, not proof that every referenced component is linked into the staged DLLs.'
    packageCount = $inventory.Count
    packages = $inventory
}
$parent = Split-Path -Parent $OutputPath
if ($parent) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
$report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $OutputPath -Encoding utf8
Write-Host "Qt third-party SBOM inventory: $($inventory.Count) packages -> $OutputPath"
