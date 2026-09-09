# A/B a dynamic-shadow switch on the console, unattended: one command per run,
# one report.md out.
#
#   powershell -File .claude\skills\tyra-testing\scripts\shadow-ab.ps1 `
#       -Editor build-dev\tyrax-editor.exe `
#       -Project $env:TEMP\tyra-editor-test\spotab `
#       -Vantages $env:TEMP\tyra-editor-test\spotab\vantages.json `
#       -Toggle spotShadowVolumes -Values true,false `
#       -OutDir <scratch>\spotab
#
# For every (value x vantage) it: writes the value into the `.tyra`, freezes the
# Player at the vantage's pose, builds and boots the project, waits for the game
# to settle, screenshots the emulator that is running THIS project by pid,
# greps the game's own bin/log.txt for assert banners, and measures three
# regions of the picture.  Then it writes a table.
#
# The measurement is the point.  "Look at these two screenshots" is not an A/B;
# mean brightness of the WHOLE FRAME, of the centre region and of the LOWER
# THIRD is, because a shadow is darkness in a known place, and a reviewer can
# read "0.412 -> 0.287" without opening an image.  The whole-frame column is
# there because the other two assume the subject is centred and will report a
# confident 0.0000 when it is not.  The FPS strip is reported the same way and
# for a different reason: it is the cheapest proof that the frame is a running
# game at all rather than a black window that would otherwise measure as a
# beautiful shadow everywhere.  It is INK COVERAGE, not OCR - this reads "the
# counter is drawn", never "the counter says 50".
#
# Everything that can reach Docker is bounded and killed with its children on
# timeout: on this machine a docker call does not fail, it blocks forever.
#
# What it does NOT do: it never kills PCSX2 by name.  The instance it closes is
# the one whose command line names this project - the same discriminator
# Runner::killEmulatorsFor uses in src/runner.cpp - so a parallel worktree's
# emulator is neither captured nor reaped.

param(
    # Your build.  The main checkout's build\tyrax-editor.exe is usually locked
    # by the user's running editor; build-dev\ is the one to point at.
    [Parameter(Mandatory = $true)][string]$Editor,
    [Parameter(Mandatory = $true)][string]$Project,
    # JSON array of { "name", "position": [x,y,z], "rotation": [pitch,yaw,roll] }.
    # make-shadow-fixture.ps1 writes one into the project.
    [Parameter(Mandatory = $true)][string]$Vantages,
    # A key in the `.tyra`'s top-level "settings" block: spotShadowVolumes,
    # flashShadowVolumes, blobShadows, ...  Inserted if the file lacks it.
    [Parameter(Mandatory = $true)][string]$Toggle,
    # JSON literals, in the order they should be run.
    [string[]]$Values = @('true', 'false'),
    [Parameter(Mandatory = $true)][string]$OutDir,
    # Docker's ceiling.  A first-ever build pulls the image and compiles the
    # engine, so give it room; a warm incremental build is under a minute.
    [int]$BuildTimeout = 900,
    # The skill's number: logo + splash + scene load.
    [double]$Settle = 14,
    # Leave the last emulator up (to look at it by hand).
    [switch]$KeepRunning
)

$ErrorActionPreference = 'Stop'
. "$PSScriptRoot\shadow-ab.lib.ps1"
Add-Type -AssemblyName System.Drawing

$Editor = (Resolve-Path $Editor).Path
$Project = (Resolve-Path $Project).Path
$Vantages = (Resolve-Path $Vantages).Path
$shotScript = Join-Path $PSScriptRoot 'screenshot-window.ps1'
if (-not (Test-Path $shotScript)) { throw "screenshot-window.ps1 is missing next to this script." }
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Force $OutDir | Out-Null }
$OutDir = (Resolve-Path $OutDir).Path

$manifest = Get-TyraManifest -Project $Project
$playerFile = Get-PlayerObjectFile -Project $Project
$binDir = Join-Path $Project 'bin'
$gameLog = Join-Path $binDir 'log.txt'

# PowerShell 5.1's ConvertFrom-Json writes a JSON ARRAY to the pipeline as ONE
# object, so `@(... | ConvertFrom-Json)` is an array of one Object[] and not an
# array of vantages.  A foreach over it then runs ONCE with $v bound to the
# whole array, and `$v.name` member-enumerates into "lamp1-8u between lamp2-8u"
# - a single run under a three-part name, which looks like the loop working.
# Enumerating explicitly is what flattens it, on either edition.
$vantageList = @()
foreach ($x in (Get-Content -Raw $Vantages | ConvertFrom-Json)) { $vantageList += $x }
if ($vantageList.Count -eq 0) { throw "No vantages in $Vantages." }
foreach ($x in $vantageList) {
    if (-not $x.name -or -not $x.position -or -not $x.rotation) {
        throw "Every vantage needs name, position[3] and rotation[3]; got: $($x | ConvertTo-Json -Compress)"
    }
}

# `-Values true,false` is an ARRAY to PowerShell but one string to
# `powershell -File`, which does not split arguments - and an unsplit
# "true,false" written into the manifest produces `"key": true,false,`, i.e. a
# malformed .tyra and a build that fails for a reason nothing points at. Accept
# both spellings by splitting here.
$Values = @($Values | ForEach-Object { $_ -split ',' } |
            ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($Values.Count -eq 0) { throw "-Values is empty." }

# --- measuring a picture ----------------------------------------------------
#
# System.Drawing only: no PIL, no extra dependency, and the same LockBits shape
# screenshot-window.ps1 already uses.  Luma is ITU-R 601-2 over 0..1, so the
# numbers here mean the same thing as the diff percentages that script prints.
function Measure-Region {
    param(
        [Parameter(Mandatory = $true)]$Bitmap,
        # The picture inside the capture, as x,y,w,h - see Get-ContentBox.
        [Parameter(Mandatory = $true)][int[]]$Box,
        [double]$X0, [double]$Y0, [double]$X1, [double]$Y1
    )
    $x = $Box[0] + [int][Math]::Floor($X0 * $Box[2])
    $y = $Box[1] + [int][Math]::Floor($Y0 * $Box[3])
    $rw = [Math]::Max(1, [int][Math]::Floor(($X1 - $X0) * $Box[2]))
    $rh = [Math]::Max(1, [int][Math]::Floor(($Y1 - $Y0) * $Box[3]))
    if ($x + $rw -gt $Bitmap.Width) { $rw = $Bitmap.Width - $x }
    if ($y + $rh -gt $Bitmap.Height) { $rh = $Bitmap.Height - $y }
    $rect = New-Object System.Drawing.Rectangle $x, $y, $rw, $rh
    $data = $Bitmap.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                             [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
    $stride = $data.Stride
    $buf = New-Object byte[] ($stride * $rh)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $Bitmap.UnlockBits($data)
    [double]$sum = 0; [long]$n = 0; [long]$ink = 0
    for ($row = 0; $row -lt $rh; $row++) {
        $base = $row * $stride
        for ($col = 0; $col -lt $rw; $col++) {
            $i = $base + $col * 4
            $l = (299 * $buf[$i + 2] + 587 * $buf[$i + 1] + 114 * $buf[$i]) / 1000.0
            $sum += $l; $n++
            if ($l -gt 128) { $ink++ }
        }
    }
    $mean = 0.0; $inkFrac = 0.0
    if ($n -gt 0) { $mean = $sum / $n / 255.0; $inkFrac = $ink / [double]$n }
    return @{ Mean = $mean; Ink = $inkFrac; Rect = "$x,$y,$rw,$rh" }
}

# WHERE THE PS2 PICTURE IS, decided ONCE and reused for every capture.
#
# This is deliberately not screenshot-window.ps1's -Trim.  Trimming re-finds the
# content box on every frame, so a variant that changed the lit pixels gets a
# different crop and a brightness comparison silently compares two different
# rectangles - the same trap the motion gate records ("take the crop ONCE and
# never -Trim").  -Auto already narrows the capture to the render child; this
# only drops the black pillar bars that child pads the 4:3 picture with.
function Get-ContentBox {
    param([Parameter(Mandatory = $true)][string]$Path)
    $bmp = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        $w = $bmp.Width; $h = $bmp.Height
        $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                              [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
        $stride = $data.Stride
        $buf = New-Object byte[] ($stride * $h)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
        $bmp.UnlockBits($data)
        # COVERAGE, not a bounding box.  PCSX2 draws its own "FPS 58.35 [P]"
        # overlay inside the black letterbox above the picture, and one line of
        # thin white text drags a plain bounding box right back over the bars -
        # measured here as a 959x829 "picture" where the real one is 959x712,
        # which put the FPS-strip region on pure black and read 0.000 ink.  A
        # row/column belongs to the picture when a QUARTER of it is lit.
        $rowLit = New-Object int[] $h
        $colLit = New-Object int[] $w
        for ($y = 0; $y -lt $h; $y++) {
            $base = $y * $stride
            for ($x = 0; $x -lt $w; $x++) {
                $i = $base + $x * 4
                if ((299 * $buf[$i + 2] + 587 * $buf[$i + 1] + 114 * $buf[$i]) / 1000 -le 10) { continue }
                $rowLit[$y]++
                $colLit[$x]++
            }
        }
        $y0 = $h; $y1 = -1; $x0 = $w; $x1 = -1
        for ($y = 0; $y -lt $h; $y++) {
            if ($rowLit[$y] -lt ($w / 4)) { continue }
            if ($y -lt $y0) { $y0 = $y }
            if ($y -gt $y1) { $y1 = $y }
        }
        for ($x = 0; $x -lt $w; $x++) {
            if ($colLit[$x] -lt ($h / 4)) { continue }
            if ($x -lt $x0) { $x0 = $x }
            if ($x -gt $x1) { $x1 = $x }
        }
        if ($x1 -lt 0 -or $y1 -lt 0 -or ($x1 - $x0 + 1) -lt ($w / 2) -or ($y1 - $y0 + 1) -lt ($h / 2)) {
            Write-Warning (("The first capture has no plausible picture in it (found {0}x{1} of {2}x{3}) - " +
                            "measuring the whole capture instead. A black or tiny box means the game had " +
                            "not drawn yet: raise -Settle.") -f
                           ($x1 - $x0 + 1), ($y1 - $y0 + 1), $w, $h)
            return @(0, 0, $w, $h)
        }
        return @($x0, $y0, ($x1 - $x0 + 1), ($y1 - $y0 + 1))
    } finally { $bmp.Dispose() }
}

function Measure-Shot {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int[]]$Box
    )
    $bmp = [System.Drawing.Bitmap]::FromFile($Path)
    try {
        return @{
            Width  = $bmp.Width
            Height = $bmp.Height
            # The metric that cannot miss the subject.  The two regions below
            # assume the thing under test is roughly centred, and a vantage
            # that frames two lamp groups puts BOTH at the edges: measured on
            # this fixture's `between` shot, the centre region reported exactly
            # 0.0000 for a switch the whole frame scored 0.00005 on (against a
            # 0.00000 control).  Small either way - but "no change" and "the
            # region was empty" are different answers and this column is what
            # tells them apart.
            Frame  = (Measure-Region -Bitmap $bmp -Box $Box -X0 0.00 -Y0 0.00 -X1 1.00 -Y1 1.00)
            # The middle of the frame: the caster and the wall behind it.
            Centre = (Measure-Region -Bitmap $bmp -Box $Box -X0 0.30 -Y0 0.30 -X1 0.70 -Y1 0.70)
            # Where a shadow on the GROUND lands with the camera pitched down.
            Lower  = (Measure-Region -Bitmap $bmp -Box $Box -X0 0.00 -Y0 0.667 -X1 1.00 -Y1 1.00)
            # The debug HUD writes "FPS x/y" at raster (16, 16); this box holds
            # it at any raster size.  Ink coverage, not OCR.
            Fps    = (Measure-Region -Bitmap $bmp -Box $Box -X0 0.02 -Y0 0.015 -X1 0.42 -Y1 0.095)
        }
    } finally { $bmp.Dispose() }
}

function Format-Num { param([double]$v, [int]$d = 3)
    return $v.ToString('F' + $d, [Globalization.CultureInfo]::InvariantCulture) }

# --- one variant x one vantage ---------------------------------------------

$rows = @()
$contentBox = $null

# Indexed, not keyed by the value: passing the SAME value twice is the control
# run - it measures what two boots of one configuration differ by, which is the
# only number that says whether a delta below it means anything.
for ($vi = 0; $vi -lt $Values.Count; $vi++) {
    $value = $Values[$vi]
    Write-Host "=== [$vi] $Toggle = $value ==="
    Set-TyraSetting -Path $manifest -Key $Toggle -Value $value

    foreach ($v in $vantageList) {
        $tag = "$vi-$value-$($v.name)"
        Write-Host "--- $tag"

        # Frozen-camera fixture: the pose is authored, the speeds are zero, so
        # the same frame comes back every boot with no pad script involved.
        Edit-ObjectFile -Path $playerFile -Fields ([ordered]@{
            position  = (Format-JsonVec3 @([double]$v.position[0], [double]$v.position[1], [double]$v.position[2]))
            rotation  = (Format-JsonVec3 @([double]$v.rotation[0], [double]$v.rotation[1], [double]$v.rotation[2]))
            walkSpeed = '0'
            lookSpeed = '0'
        })

        # The log is the game's own, written through host: - remove it so the
        # grep below can only be about THIS boot.  Same for a leftover devkit
        # command, which is applied at boot and eats the first capture.
        Remove-Item -Force -ErrorAction SilentlyContinue $gameLog
        Remove-Item -Force -ErrorAction SilentlyContinue (Join-Path $binDir 'livedbg.cmd')

        $stdout = Join-Path $OutDir "$tag.build.log"
        $stderr = Join-Path $OutDir "$tag.build.err"
        $run = Invoke-Bounded -FilePath $Editor -Arguments @('--build', $Project, '--run') `
                              -TimeoutSec $BuildTimeout -StdOut $stdout -StdErr $stderr
        $buildStatus = if ($run.TimedOut) { "TIMED OUT after $($run.Seconds)s" }
                       elseif ($run.ExitCode -ne 0) { "exit $($run.ExitCode)" }
                       else { "ok ($($run.Seconds)s)" }
        Write-Host "    build: $buildStatus"

        $shot = ''
        $emuPid = 0
        $measure = $null
        if (-not $run.TimedOut -and $run.ExitCode -eq 0) {
            Start-Sleep -Seconds $Settle
            $emus = @(Get-ProjectEmulators -Project $Project)
            if ($emus.Count -eq 0) {
                Write-Warning "No PCSX2 whose command line names '$Project' - nothing to capture."
            } else {
                if ($emus.Count -gt 1) {
                    Write-Warning "$($emus.Count) emulators name this project; capturing pid $($emus[0].ProcessId)."
                }
                $emuPid = [int]$emus[0].ProcessId
                $shot = Join-Path $OutDir "$tag.png"
                # -PrintWindow reads the window's OWN content: nothing is raised,
                # no focus is stolen and an occluded emulator cannot be captured
                # as whatever is in front of it.  -Auto takes the render child,
                # so the PS2 picture is what gets measured, not the Qt chrome.
                & powershell -NoProfile -File $shotScript -ProcessId $emuPid -PrintWindow -Auto -OutFile $shot
                if (Test-Path $shot) {
                    if (-not $contentBox) {
                        $contentBox = Get-ContentBox -Path $shot
                        Write-Host ("    picture: {0},{1} {2}x{3} (found once, reused for every capture)" -f
                                    $contentBox[0], $contentBox[1], $contentBox[2], $contentBox[3])
                    }
                    $measure = Measure-Shot -Path $shot -Box $contentBox
                } else { Write-Warning "screenshot-window.ps1 wrote nothing for pid $emuPid." }
            }
        }

        # The reliable failure signal is the game's own log, not emulog: a
        # failed assert halts quietly and leaves the last frame on screen, so a
        # screenshot of a dead game looks like a screenshot of a live one.
        $logStatus = 'no log.txt'
        if (Test-Path $gameLog) {
            $banner = @(Select-String -Path $gameLog -Pattern 'Assertion|^=+$|=======' -ErrorAction SilentlyContinue)
            $logStatus = if ($banner.Count -gt 0) { "ASSERT ($($banner.Count) line(s))" } else { 'clean' }
            Copy-Item -Force $gameLog (Join-Path $OutDir "$tag.log.txt")
        }
        Write-Host "    log:   $logStatus"

        $leaf = '-'
        if ($shot) { $leaf = Split-Path -Leaf $shot }
        $rows += [pscustomobject]@{
            Index = $vi; Value = $value; Vantage = $v.name; Build = $buildStatus; Pid = $emuPid
            Shot = $leaf; Log = $logStatus; M = $measure
        }
    }
}

# --- close only what we started --------------------------------------------

if (-not $KeepRunning) {
    foreach ($p in @(Get-ProjectEmulators -Project $Project)) {
        Write-Host "closing PCSX2 pid $($p.ProcessId) (its command line names this project)"
        try { Stop-Process -Id ([int]$p.ProcessId) -Force -ErrorAction Stop } catch { }
    }
}

# --- report ----------------------------------------------------------------

$sb = New-Object Text.StringBuilder
function Line($s) { [void]$sb.AppendLine($s) }

$tick = [char]0x60
$valuesText = (($Values | ForEach-Object { "$tick$_$tick" }) -join ', ')
$stamp = (Get-Date -Format 'yyyy-MM-dd HH:mm:ss')

Line "# Dynamic-shadow A/B: ``$Toggle``"
Line ""
Line "- project: ``$Project``"
Line "- editor: ``$Editor``"
Line "- vantages: ``$Vantages``"
Line "- values: $valuesText"
Line "- settle: $Settle s after ``--build --run``; capture ``-PrintWindow -Auto`` by pid"
Line "- run: $stamp"
Line ""
Line "| # | $Toggle | vantage | build | pid | screenshot | log | frame mean | centre mean | lower-third mean | FPS-strip ink |"
Line "|---|---|---|---|---|---|---|---|---|---|---|"
foreach ($r in $rows) {
    $w = '-'; $c = '-'; $l = '-'; $f = '-'
    if ($r.M) {
        $w = Format-Num $r.M.Frame.Mean 5
        $c = Format-Num $r.M.Centre.Mean
        $l = Format-Num $r.M.Lower.Mean
        $f = Format-Num $r.M.Fps.Ink
    }
    $img = '-'
    if ($r.Shot -ne '-') { $img = "![$($r.Shot)]($($r.Shot))" }
    Line "| $($r.Index) | ``$($r.Value)`` | $($r.Vantage) | $($r.Build) | $($r.Pid) | $img | $($r.Log) | $w | $c | $l | $f |"
}
Line ""

# The delta table is the answer to "did the switch change any pixels, and
# where".  Written against the FIRST value, so the sign is "value N minus
# value 1" and the reader does not have to remember which column was which.
if ($Values.Count -ge 2) {
    Line "## Change against ``$($Values[0])`` (variant 0)"
    Line ""
    Line "| vantage | variant | d frame | d centre | d lower third |"
    Line "|---|---|---|---|---|"
    foreach ($v in $vantageList) {
        $base = $rows | Where-Object { $_.Vantage -eq $v.name -and $_.Index -eq 0 } | Select-Object -First 1
        for ($k = 1; $k -lt $Values.Count; $k++) {
            $cur = $rows | Where-Object { $_.Vantage -eq $v.name -and $_.Index -eq $k } | Select-Object -First 1
            if ($base -and $cur -and $base.M -and $cur.M) {
                $dw = Format-Num ($cur.M.Frame.Mean - $base.M.Frame.Mean) 5
                $dc = Format-Num ($cur.M.Centre.Mean - $base.M.Centre.Mean) 4
                $dl = Format-Num ($cur.M.Lower.Mean - $base.M.Lower.Mean) 4
                Line "| $($v.name) | $k = ``$($Values[$k])`` | $dw | $dc | $dl |"
            } else {
                Line "| $($v.name) | $k = ``$($Values[$k])`` | - | - | - |"
            }
        }
    }
    Line ""
    Line "Run the SAME value twice (``-Values true,true``) to get the noise floor:"
    Line "two boots of one configuration, frozen camera, progressive display. A"
    Line "delta smaller than that is not a finding."
    Line ""
}

$first = $rows | Where-Object { $_.M } | Select-Object -First 1
if ($first) {
    if ($contentBox) {
        Line ("Capture $($first.M.Width)x$($first.M.Height); the PS2 picture inside it is " +
              "``$($contentBox[0]),$($contentBox[1]) $($contentBox[2])x$($contentBox[3])``, found on the " +
              "FIRST capture and reused for every one after it.")
    }
    Line ("Regions, as x,y,w,h in the capture: frame ``$($first.M.Frame.Rect)``, " +
          "centre ``$($first.M.Centre.Rect)``, lower third ``$($first.M.Lower.Rect)``, " +
          "FPS strip ``$($first.M.Fps.Rect)``.")
    Line ""
}
Line "The FRAME column is the whole picture and is the one that cannot miss its"
Line "subject: a vantage that frames two lamp groups puts both at the EDGES, where"
Line "the centre region reads a perfect 0.0000 for a switch that did change pixels."
Line "Read it first, then the two regions to say WHERE."
Line ""
Line "Means are ITU-R 601-2 luma over 0..1. The FPS column is INK COVERAGE of the"
Line "debug HUD's top-left strip - the fraction of pixels above half brightness."
Line "It is a liveness check (a black window reads 0.000 and would otherwise look"
Line "like a perfect shadow), never a reading of the number."
Line ""
Line "Build logs are ``<variant>.build.log`` / ``.build.err``; the game's own"
Line "``bin/log.txt`` is copied beside each screenshot as ``<variant>.log.txt``."

$report = Join-Path $OutDir 'report.md'
[IO.File]::WriteAllText($report, $sb.ToString())
Write-Host ""
Write-Host "report: $report"
