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

foreach ($d in $VendorDeps) {
    if (Test-Path $d.Probe) {
        Write-Host "OK: $($d.Dir) already present"
        continue
    }
    if (Test-Path $d.Dir) {
        # A directory without its probe file: a stale/partial checkout, or
        # vendor/tyra whose engine sources are tracked in this repo. Cloning
        # into a non-empty directory just fails, so say what to do instead.
        Write-Host "NOTE: $($d.Dir) exists but $($d.Probe) is missing - delete the directory and re-run setup.ps1 if the build complains." -ForegroundColor Yellow
        continue
    }
    Invoke-Native { git clone --depth 1 --branch $d.Branch $d.Url $d.Dir } "git clone $($d.Url)"
}

# Ensure the stb single-headers we #include are present, even when vendor/stb
# is a stale/partial directory that predates the full clone above (no probe
# file, so the clone step skips it). Back-fill any missing header directly.
foreach ($h in $StbHeaders) {
    $path = "vendor/stb/$h"
    if (-not (Test-Path $path)) {
        Write-Host "Fetching $h"
        New-Item -ItemType Directory -Force 'vendor/stb' | Out-Null
        Invoke-WebRequest -Uri "https://raw.githubusercontent.com/nothings/stb/master/$h" -OutFile $path
    }
}

# MakeHuman CC0 data (Character Generator). ~110 small files plus six large
# skin textures, so this is the one fetch worth reporting progress on. Each
# file is checked individually: an interrupted run resumes where it stopped
# instead of re-downloading 40 MB.
# The list is checked file by file rather than gated on a probe: it GROWS as
# the generator gains assets, so an existing install has to pick up what is new
# instead of being declared complete because base.obj happens to be there.
$missing = @($MhAssets.Files | Where-Object { -not (Test-Path (Join-Path $MhAssets.Dir $_.Path)) })
if ($missing.Count -eq 0) {
    Write-Host "OK: $($MhAssets.Dir) already present ($($MhAssets.Files.Count) files)"
} else {
    Write-Host "Fetching MakeHuman CC0 data into $($MhAssets.Dir) ($($missing.Count) files, ~80 MB for a full set)"
    # Invoke-WebRequest's own progress bar costs more than the download on a
    # list this long - it re-renders per read on Windows PowerShell 5.1.
    $prevProgress = $ProgressPreference
    $ProgressPreference = 'SilentlyContinue'
    try {
        $done = 0
        foreach ($f in $missing) {
            $dest = Join-Path $MhAssets.Dir $f.Path
            $done++
            New-Item -ItemType Directory -Force (Split-Path $dest) | Out-Null
            if ($done % 10 -eq 0) { Write-Host "  $done/$($missing.Count) $($f.Path)" }
            # A partial file must not look like a finished one to the next run.
            $tmp = "$dest.part"
            Invoke-WebRequest -Uri $f.Url -OutFile $tmp
            Move-Item -Force $tmp $dest
        }
    } finally { $ProgressPreference = $prevProgress }
    Write-Host "  done ($($missing.Count) files)"
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
