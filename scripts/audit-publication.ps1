param([string]$RepositoryPath = (Join-Path $PSScriptRoot '..'))

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path -LiteralPath $RepositoryPath).Path
Push-Location -LiteralPath $repo
try {
    if ((& git rev-parse --is-inside-work-tree) -ne 'true' -or $LASTEXITCODE -ne 0) {
        throw 'RepositoryPath must be a Git working tree.'
    }
    if (& git status --porcelain=v1) {
        throw 'Commit or discard working tree changes before publication.'
    }

    $expectedEmail = [string](& git config --local user.email)
    $expectedEmail = $expectedEmail.Trim()
    if ($LASTEXITCODE -ne 0 -or
        $expectedEmail -cnotmatch '^[0-9]+\+[A-Za-z0-9-]+@users\.noreply\.github\.com$') {
        throw 'Set this repository user.email to the GitHub ID-based noreply address.'
    }
    $commits = @(& git rev-list --all)
    if ($LASTEXITCODE -ne 0 -or $commits.Count -eq 0) {
        throw 'No Git commits were found.'
    }
    $emails = @(& git log --all --format='%ae%n%ce' |
        Where-Object { $_ -ne '' })
    if ($LASTEXITCODE -ne 0 -or
        @($emails | Where-Object { $_ -cne $expectedEmail }).Count -gt 0) {
        throw 'A commit author or committer email differs from the repository noreply address.'
    }

    $objects = @(& git rev-list --objects --all)
    if ($LASTEXITCODE -ne 0) { throw 'Could not list Git objects.' }
    $restrictedPaths = @($objects | Where-Object {
        $_ -match '^[0-9a-f]{40} (?:docs/environment-[^/]+|(?:build|out|vcpkg_installed)/|.*\.(?:exe|dll|pdb|zip|7z|bundle|pem|pfx|key))$'
    })
    if ($restrictedPaths.Count -gt 0) {
        $names = @($restrictedPaths | ForEach-Object {
            $_ -replace '^[0-9a-f]{40} ', ''
        } | Sort-Object -Unique)
        throw "Restricted files exist in Git history: $($names -join ', ')"
    }

    $patterns = [ordered]@{
        machinePath = '([A-Za-z]:[\\/]+Users[\\/]+|/Users/[^/[:space:]]+|/home/[^/[:space:]]+|\\\\[A-Za-z0-9._-]+\\[A-Za-z0-9._-]+)'
        email = '[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}'
        credential = '(gh[pousr]_[A-Za-z0-9_]{20,}|github_pat_[A-Za-z0-9_]{20,}|sk-[A-Za-z0-9_-]{20,}|-----BEGIN [A-Z ]+PRIVATE KEY-----|AKIA[0-9A-Z]{16}|xox[baprs]-[A-Za-z0-9-]{15,}|AIza[0-9A-Za-z_-]{30,})'
        urlCredential = 'https?://[^/[:space:]]+:[^@/[:space:]]+@'
    }
    foreach ($category in $patterns.Keys) {
        $paths = [System.Collections.Generic.HashSet[string]]::new()
        foreach ($commit in $commits) {
            $matches = @(& git grep -l -I -i -E $patterns[$category] $commit -- 2>$null)
            $result = $LASTEXITCODE
            if ($result -ne 0 -and $result -ne 1) {
                throw "History scan failed for $category at $commit."
            }
            foreach ($match in $matches) {
                [void]$paths.Add($match.Substring($commit.Length + 1))
            }
        }
        if ($paths.Count -gt 0) {
            throw "Possible $category in Git history: $((@($paths | Sort-Object)) -join ', ')"
        }
    }
    Write-Host "Publication audit passed: $($commits.Count) commits, noreply metadata, no restricted paths or matching content."
} finally {
    Pop-Location
}
