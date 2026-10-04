# Clones third-party dependencies into vendor/ and fetches the PS2 deploy tools.
# The lists themselves live in deps.ps1, which build.ps1 reads too - add a new
# dependency THERE and the build guard picks it up for free.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot
. ./deps.ps1

# git and tar report progress on stderr, and Windows PowerShell turns any
# native-command stderr into a terminating error while $ErrorActionPreference
# is 'Stop' - but only when the caller captured the stream (build.ps1 piped
# into a log, CI). Run native tools with that trap disabled and judge them by
# their exit code, which is what actually says whether they worked.
function Invoke-Native {
    param([scriptblock]$Command, [string]$What)
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { & $Command } finally { $ErrorActionPreference = $prev }
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit $LASTEXITCODE)" }
}

# Shallow-fetch one exact commit from the first remote that has it. `git clone
# --branch` only accepts refs, not SHAs, so a pinned checkout has to be spelled
# out: init, then fetch the SHA directly. GitHub serves arbitrary reachable
# SHAs this way, which is what lets the mirror be a plain fork rather than a
# repo carrying a tyrax-specific tag.
# Every git call is piped to Out-Null on purpose: in PowerShell a native
# command's stdout lands in the function's output stream, so a single chatty
# git would be returned alongside $true and turn the caller's `-not (...)` into
# a test on a 2-element array - i.e. a failed fetch that reads as success. The
# caller re-checks the probe file for the same reason.
function Get-PinnedCommit($Dir, $Commit, $Urls) {
    Invoke-Native { git init -q $Dir | Out-Null } "git init $Dir"
    foreach ($url in $Urls) {
        git -C $Dir fetch -q --depth 1 $url $Commit 2>$null | Out-Null
        if ($LASTEXITCODE -eq 0) {
            Invoke-Native { git -C $Dir checkout -q --detach FETCH_HEAD | Out-Null } "git checkout $Commit"
            return
        }
        Write-Host "  ...$url did not serve $Commit, trying the next remote" -ForegroundColor Yellow
    }
    # Leave nothing half-initialised: an empty .git with no checkout would trip
    # the "directory exists but the probe is missing" branch on the next run and
    # send the user off deleting directories for a network problem.
    Remove-Item -Recurse -Force $Dir -ErrorAction SilentlyContinue
}

foreach ($d in $VendorDeps) {
    if (Test-Path $d.Probe) {
        Write-Host "OK: $($d.Dir) already present"
        continue
    }
    if (Test-Path $d.Dir) {
        # A directory without its probe file: a stale/partial checkout, or
        # vendor/tyra whose engine sources are tracked in this repo. Fetching
        # into a non-empty directory just fails, so say what to do instead.
        Write-Host "NOTE: $($d.Dir) exists but $($d.Probe) is missing - delete the directory and re-run setup.ps1 if the build complains." -ForegroundColor Yellow
        continue
    }
    Write-Host "Fetching $($d.Url) @ $($d.Commit.Substring(0,12)) ($($d.Ref)) -> $($d.Dir)"
    Get-PinnedCommit $d.Dir $d.Commit @($d.Url, $d.Mirror)
    if (-not (Test-Path $d.Probe)) {
        throw "Could not fetch $($d.Commit) for $($d.Dir) from either $($d.Url) or $($d.Mirror)."
    }
}

# Ensure the stb single-headers we #include are present, even when vendor/stb
# is a stale/partial directory that predates the full clone above (no probe
# file, so the clone step skips it). Back-fill any missing header directly,
# from the SAME commit deps.ps1 pins - see the $StbHeaders comment there.
$stbCommit = ($VendorDeps | Where-Object { $_.Dir -eq 'vendor/stb' }).Commit
foreach ($h in $StbHeaders) {
    $path = "vendor/stb/$h"
    if (-not (Test-Path $path)) {
        Write-Host "Fetching $h @ $($stbCommit.Substring(0,12))"
        New-Item -ItemType Directory -Force 'vendor/stb' | Out-Null
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/$stbCommit/$h" -OutFile $path
    }
}

foreach ($t in $Ps2Tools) {
    if (Test-Path $t.Probe) {
        Write-Host "OK: $($t.Dir) already present"
        continue
    }
    Write-Host "Fetching $($t.Url)"
    New-Item -ItemType Directory -Force $t.Dir | Out-Null
    $tarball = Join-Path $t.Dir 'download.tar.gz'
    Invoke-WebRequest -Uri $t.Url -OutFile $tarball
    Invoke-Native { tar -xzf $tarball -C $t.Dir } "tar -xzf $tarball"
    Remove-Item $tarball
}
