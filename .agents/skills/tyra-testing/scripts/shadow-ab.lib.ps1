# Shared helpers for the shadow A/B rig (shadow-ab.ps1 + make-shadow-fixture.ps1).
#
# Dot-source it; it defines functions and does nothing else:
#
#     . "$PSScriptRoot\shadow-ab.lib.ps1"
#
# Two jobs: editing a project's files as TEXT, and running something that talks
# to Docker without being able to hang forever.
#
# Why text and not ConvertTo-Json: a `.tyra` (and an `objects/<id>.json`) is
# written by the editor's own serializer, and round-tripping it through
# PowerShell 5.1's JSON would reformat the whole file, drop the one-key-per-line
# layout every other recipe in the testing skill greps, and silently retype
# numbers.  These helpers replace ONE key's value and leave every byte around it
# alone, so a fixture stays diffable and `--resave` has nothing to complain
# about.  Both files are machine-written with a predictable shape - the manifest
# is one settings key per line, an object file is a single line - which is what
# makes that safe here and NOT a pattern to copy onto hand-edited JSON.

$ErrorActionPreference = 'Stop'

# --- project files ---------------------------------------------------------

# JSON has one decimal separator and this machine's culture may not agree with
# it: "$x" for 0.9 under pl-PL is "0,9", which turns a position array into four
# numbers.  Every number this rig writes into a project goes through here.
function Format-JsonNumber {
    param([Parameter(Mandatory = $true)][double]$Value)
    return $Value.ToString('0.######', [Globalization.CultureInfo]::InvariantCulture)
}

# A JSON [x, y, z] from three numbers, culture-proof.
function Format-JsonVec3 {
    param([Parameter(Mandatory = $true)][double[]]$Value)
    if ($Value.Count -ne 3) { throw "Format-JsonVec3 wants exactly 3 numbers." }
    return '[' + (($Value | ForEach-Object { Format-JsonNumber $_ }) -join ', ') + ']'
}

# Replace the value of "<Key>" in a single-line JSON object (an
# objects/<id>.json).  $Value is a JSON LITERAL - '0', 'true', '[1, 2, 3]',
# '"text"' - not a PowerShell object, so the caller decides the formatting.
# Only the FIRST occurrence is replaced, which is what makes "position" mean the
# object's own position and not one buried in a nested block: the editor writes
# the object's own fields first.
function Set-JsonField {
    param(
        [Parameter(Mandatory = $true)][string]$Text,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$Value
    )
    # array | string | bare scalar, in that order - a bare scalar must not be
    # allowed to eat a '[' or a '"'.
    $rx = [regex]('"' + [regex]::Escape($Key) + '"\s*:\s*(\[[^\]]*\]|"(?:[^"\\]|\\.)*"|[^,}\s]+)')
    $m = $rx.Match($Text)
    if (-not $m.Success) { throw "Set-JsonField: key '$Key' not found." }
    return $Text.Substring(0, $m.Index) + '"' + $Key + '": ' + $Value +
           $Text.Substring($m.Index + $m.Length)
}

# Read/patch/write one object file.  -Fields is an ordered pairing of key ->
# JSON literal.
function Edit-ObjectFile {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        # An ordered dictionary works too; [hashtable] would refuse one.
        [Parameter(Mandatory = $true)][System.Collections.IDictionary]$Fields
    )
    $t = [IO.File]::ReadAllText($Path)
    foreach ($k in $Fields.Keys) { $t = Set-JsonField -Text $t -Key $k -Value $Fields[$k] }
    [IO.File]::WriteAllText($Path, $t)
}

# Set one key that the `.tyra` manifest writes ON A LINE OF ITS OWN - every
# entry of the top-level "settings" block, and the top-level scalars beside it
# (`defaultAmbience`, `defaultGrading`, ...).  The line anchor is what makes
# that safe: the same key spelled inside a scene's own single-line settings blob
# is mid-line and cannot match, which is the whole reason this is a regex and
# not a JSON walk.  A key the file does not carry yet (a setting is written only
# when it is not its default) is INSERTED into the settings block, so a toggle
# works on a project that never touched it.
function Set-TyraSetting {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Key,
        [Parameter(Mandatory = $true)][string]$Value
    )
    $t = [IO.File]::ReadAllText($Path)
    $rx = [regex]('(?m)^(?<i>[ \t]*)"' + [regex]::Escape($Key) + '"\s*:\s*(?<v>\[[^\]]*\]|"(?:[^"\\]|\\.)*"|[^,\r\n]+?)(?<c>,?)[ \t]*$')
    $m = $rx.Match($t)
    if ($m.Success) {
        $t = $t.Substring(0, $m.Index) + $m.Groups['i'].Value + '"' + $Key + '": ' + $Value +
             $m.Groups['c'].Value + $t.Substring($m.Index + $m.Length)
    } else {
        $open = [regex]::Match($t, '(?m)^[ \t]*"settings"[ \t]*:[ \t]*\{[ \t]*$')
        if (-not $open.Success) { throw "Set-TyraSetting: no top-level settings block in $Path." }
        $at = $open.Index + $open.Length
        $t = $t.Substring(0, $at) + "`n    `"$Key`": $Value," + $t.Substring($at)
    }
    [IO.File]::WriteAllText($Path, $t)
}

# The manifest of a project directory (there is exactly one).
function Get-TyraManifest {
    param([Parameter(Mandatory = $true)][string]$Project)
    $f = @(Get-ChildItem -Path $Project -Filter '*.tyra' -File)
    if ($f.Count -ne 1) { throw "Expected one .tyra in '$Project', found $($f.Count)." }
    return $f[0].FullName
}

# The project's Player object file, found by type rather than by id.
function Get-PlayerObjectFile {
    param([Parameter(Mandatory = $true)][string]$Project)
    $dir = Join-Path $Project 'objects'
    foreach ($f in Get-ChildItem -Path $dir -Filter '*.json' -File) {
        $t = [IO.File]::ReadAllText($f.FullName)
        if ($t -match '"type"\s*:\s*"player"') { return $f.FullName }
    }
    throw "No object of type 'player' under '$dir'."
}

# --- bounded child processes -----------------------------------------------
#
# Docker on this machine does not error when it is unhappy, it BLOCKS - forever
# (see the memory note "Docker backend hangs").  Every step that can reach it
# therefore runs with a wall-clock ceiling and is killed WITH ITS CHILDREN when
# it expires: the editor spawns `docker compose`, and killing only the editor
# leaves that child holding the terminal and the build.

function Stop-ProcessTree {
    param([Parameter(Mandatory = $true)][int]$ProcessId)
    # Children first, so nothing is re-parented out of reach mid-walk.
    foreach ($c in @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$ProcessId" -ErrorAction SilentlyContinue)) {
        Stop-ProcessTree -ProcessId ([int]$c.ProcessId)
    }
    try { Stop-Process -Id $ProcessId -Force -ErrorAction Stop } catch { }
}

# Run a program with a hard timeout.  Returns a hashtable:
#   ExitCode, TimedOut, StdOut, StdErr (paths), Seconds
# Arguments are quoted HERE rather than handed to -ArgumentList raw: that array
# is pasted together unquoted, which is the trap the testing skill records for
# `--pad` scripts and applies just as well to a project path with a space in it.
function Invoke-Bounded {
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][int]$TimeoutSec,
        [Parameter(Mandatory = $true)][string]$StdOut,
        [Parameter(Mandatory = $true)][string]$StdErr
    )
    $quoted = @()
    foreach ($a in $Arguments) {
        if ($a -match '[\s"]') { $quoted += '"' + ($a -replace '"', '\"') + '"' } else { $quoted += $a }
    }
    $t0 = Get-Date
    $p = Start-Process -FilePath $FilePath -ArgumentList $quoted -PassThru -NoNewWindow `
                       -RedirectStandardOutput $StdOut -RedirectStandardError $StdErr
    # Touching .Handle caches the native handle in the object. Without it
    # PowerShell 5.1 hands back a Process whose ExitCode reads $null after the
    # wait - so a failed build reports as "exit " and looks like a script bug
    # rather than the malformed .tyra it actually was.
    $null = $p.Handle
    $done = $p.WaitForExit($TimeoutSec * 1000)
    if (-not $done) {
        Write-Warning (("'{0}' did not finish within {1}s - killing it and its children. " +
                        "A docker step that blocks does not fail on its own.") -f
                       ([IO.Path]::GetFileName($FilePath)), $TimeoutSec)
        Stop-ProcessTree -ProcessId $p.Id
        return @{ ExitCode = -1; TimedOut = $true; StdOut = $StdOut; StdErr = $StdErr
                  Seconds = [int]((Get-Date) - $t0).TotalSeconds }
    }
    return @{ ExitCode = $p.ExitCode; TimedOut = $false; StdOut = $StdOut; StdErr = $StdErr
              Seconds = [int]((Get-Date) - $t0).TotalSeconds }
}

# --- the emulator that is running THIS project ------------------------------
#
# Never by name: parallel worktrees each run their own PCSX2, and the editor
# itself only ever reaps the instance whose `-elf` names the project (see
# Runner::killEmulatorsFor in src/runner.cpp).  This is the same discriminator,
# read from the outside - so what the script closes and what a build would have
# closed are the same set.
function Get-ProjectEmulators {
    param([Parameter(Mandatory = $true)][string]$Project)
    $needle = ((Resolve-Path $Project).Path.TrimEnd('\') -replace '/', '\').ToLowerInvariant()
    $out = @()
    foreach ($name in @('pcsx2-qt.exe', 'pcsx2.exe')) {
        foreach ($p in @(Get-CimInstance Win32_Process -Filter "name='$name'" -ErrorAction SilentlyContinue)) {
            if (-not $p.CommandLine) { continue }
            $cl = ($p.CommandLine -replace '/', '\').ToLowerInvariant()
            if ($cl.Contains($needle)) { $out += $p }
        }
    }
    return $out
}
