# The MOTION gate, capture half (Windows).  Analysis half: motion-gate.py.
#
# Drives the running game along a fixed route and captures a BURST of frames
# back to back, then hands the burst to motion-gate.py, which decides whether
# what changed between the frames is the route or a broken picture.
#
#   powershell -File motion-gate.ps1 -Project C:\...\mgate -Out C:\...\burstB
#   powershell -File motion-gate.ps1 -Project C:\...\mgate -Out C:\...\burstB -NoAnalyse
#
# WHY IT IS BUILT THE WAY IT IS - four rules, each paid for on this branch:
#
#   1. CONSECUTIVE CAPTURES, NEVER A STRIDE.  The jitter shake survived a whole
#      harness because the sampler used an EVEN frame stride and therefore
#      landed on the same jitter phase every time, reporting a perfectly still
#      picture.  This loop sleeps for nothing; it reports the interval it
#      achieved and WARNS when that interval is close to a whole number of game
#      frames, which is the condition that recreates the bug.
#   2. -PrintWindow BY DEFAULT.  A GDI CopyFromScreen reads the SCREEN, so an
#      occluded window captures whatever is physically in front of it - that
#      once grabbed the owner's browser instead of the emulator and produced a
#      perfectly plausible table.  PrintWindow reads the window's own content,
#      raises nothing and steals no focus, which is what a machine with a human
#      sitting at it requires.  -Gdi is there for the case where the window is
#      demonstrably clear.
#   3. PICK THE INSTANCE BY THE PROJECT ON ITS COMMAND LINE.  Parallel worktree
#      sessions each run their own PCSX2 and -ProcessName takes the first one.
#   4. INSTRUMENT OUTSIDE THE LOOP YOU ARE PERTURBING.  The game's frame
#      counter is read from bin/livedbg.bin ONCE before the burst and once
#      after, never per capture: 24 reads a second of a file the game is
#      writing through HostFs is exactly the kind of poking that moved the
#      black-frame defect's trigger when somebody added logging inside the
#      loop that produced it.  The per-capture frame index is interpolated from
#      those two reads and labelled as an estimate, because it is one.
param(
    # The project directory - used to find the right PCSX2 and to drive its pad.
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$Out,
    # Overrides for the PCSX2 selection; -Project normally covers it.
    [int]$ProcessId = 0,
    [string]$ProcessName = 'pcsx2-qt',
    # How many captures per leg.  The frames are held in memory until the leg's
    # burst ends (a capture is w*h*4 bytes, ~2.8 MB here), which is what bounds
    # this - 110 at ~45 Hz is 2.4 s, comfortably inside a 3.3 s leg.
    [int]$Count = 110,
    # Which legs of the route to capture, in order.  Each becomes <Out>\<name>.
    [string]$Legs = 'hold,pan,dolly,return',
    # The editor binary that drives the pad.  NEVER build/ - that is the user's.
    [string]$Editor = '',
    # The pad script that resyncs the route.  A ONE-SHOT event on purpose: its
    # arrival jitter shifts the whole route equally instead of perturbing every
    # frame, which is the only way a 25 Hz host-clock pad driver can take part
    # in a deterministic measurement at all.
    [string]$Pad = 'press cross',
    [switch]$NoPad,
    # The route in src/scripts/routecam.cpp: 200 frames per leg, and the burst
    # is placed `LegLead` seconds into its leg so that the pad's arrival jitter
    # and an emulator running at less than 100 % both have room.
    [int]$LegFrames = 200,
    [double]$GameFps = 0,          # 0 = measure it from the Live Debugger
    [double]$LegLead = 0.45,
    [switch]$Gdi,
    [switch]$NoAnalyse,
    [string]$Baseline = '',
    [string]$Python = 'python',
    [string]$Note = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Gate {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hWnd, EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    public delegate bool EnumProc(IntPtr hWnd, IntPtr param);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    static RECT best; static long bestArea;
    public static IntPtr bestHwnd;
    public static int[] BiggestChild(IntPtr parent) {
        best = new RECT(); bestArea = 0; bestHwnd = IntPtr.Zero;
        EnumChildWindows(parent, new EnumProc(Consider), IntPtr.Zero);
        if (bestArea <= 0) return null;
        return new int[] { best.Left, best.Top, best.Right - best.Left, best.Bottom - best.Top };
    }
    static bool Consider(IntPtr h, IntPtr p) {
        if (!IsWindowVisible(h)) return true;
        RECT r = new RECT(); GetWindowRect(h, out r);
        long a = (long)(r.Right - r.Left) * (long)(r.Bottom - r.Top);
        if (a > bestArea) { bestArea = a; best = r; bestHwnd = h; }
        return true;
    }
    // Mean luma of a BGRA buffer - the cheapest "is this the game or a black
    // rectangle" check there is, and it runs in C# because a per-pixel loop in
    // PowerShell costs seconds per frame.
    public static double MeanLuma(byte[] a) {
        long s = 0; int n = 0;
        for (int i = 0; i + 3 < a.Length; i += 4) { s += (299 * a[i+2] + 587 * a[i+1] + 114 * a[i]) / 1000; n++; }
        return n == 0 ? 0 : (double)s / n;
    }
}
"@
[Win32Gate]::SetProcessDPIAware() | Out-Null

function Fail([string]$m) { Write-Error $m; exit 2 }

# -- the game ---------------------------------------------------------------

$Project = (Resolve-Path $Project).Path
$projLeaf = Split-Path $Project -Leaf
if ($ProcessId -le 0) {
    $cands = @(Get-CimInstance Win32_Process -Filter "name='$ProcessName.exe'" |
               Where-Object { $_.CommandLine -like "*$projLeaf*" })
    if ($cands.Count -eq 0) {
        Fail ("No $ProcessName is running on '$projLeaf'. Launch it yourself on " +
              "$Project\bin\*.elf (--build --run kills every OTHER agent's emulator too).")
    }
    if ($cands.Count -gt 1) { Write-Warning "$($cands.Count) emulators match '$projLeaf'; taking pid $($cands[0].ProcessId)." }
    $ProcessId = [int]$cands[0].ProcessId
}
$proc = Get-Process -Id $ProcessId -ErrorAction Stop
if ($proc.MainWindowHandle -eq 0) { Fail "pid $ProcessId has no main window." }
$hwnd = $proc.MainWindowHandle

$wr = New-Object Win32Gate+RECT
[Win32Gate]::GetWindowRect($hwnd, [ref]$wr) | Out-Null
$rect = @($wr.Left, $wr.Top, ($wr.Right - $wr.Left), ($wr.Bottom - $wr.Top))
$pwHwnd = $hwnd; $pwOrigin = @($rect[0], $rect[1]); $pwSize = @($rect[2], $rect[3])
$child = [Win32Gate]::BiggestChild($hwnd)
if ($child -and ($child[2] * [double]$child[3]) -ge 0.2 * $rect[2] * $rect[3]) {
    $rect = $child
    $pwHwnd = [Win32Gate]::bestHwnd; $pwOrigin = @($child[0], $child[1]); $pwSize = @($child[2], $child[3])
}
$W = $rect[2]; $H = $rect[3]
Write-Output ("pid {0}, render area {1}x{2} at {3},{4} ({5})" -f $ProcessId, $W, $H, $rect[0], $rect[1],
              $(if ($Gdi) { 'GDI - reads the SCREEN, so nothing may cover the window' } else { 'PrintWindow' }))

# -- the game's own frame counter, read OUTSIDE the burst --------------------

function GameFrame() {
    # bin/livedbg.bin: magic, version, seq, frame - the Live Debugger's
    # snapshot header (src/livedbg.hpp), flushed every 6 frames.  Opened
    # share-everything and swallowed on any failure: this is a cross-check, and
    # a cross-check that can break the run is worse than no cross-check.
    $p = Join-Path $Project 'bin\livedbg.bin'
    try {
        $fs = [System.IO.File]::Open($p, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read,
                                     [System.IO.FileShare]::ReadWrite -bor [System.IO.FileShare]::Delete)
        $b = New-Object byte[] 16
        $n = $fs.Read($b, 0, 16); $fs.Close()
        if ($n -lt 16) { return $null }
        return [BitConverter]::ToUInt32($b, 12)
    } catch { return $null }
}

# The leg length in SECONDS needs the game's own rate: PAL and NTSC differ, and
# an emulator is not obliged to run at 100 %.  Measured, not assumed.
if ($GameFps -le 0) {
    $a = GameFrame; Start-Sleep -Milliseconds 2500; $b = GameFrame
    if ($null -ne $a -and $null -ne $b -and $b -gt $a) {
        $GameFps = ($b - $a) / 2.5
    } else {
        $GameFps = 50.0
        Write-Warning "No frame counter in bin\livedbg.bin - assuming 50 fps for the leg timing, which is a GUESS."
    }
}
$legSec = $LegFrames / $GameFps
Write-Output ("game {0:F1} fps, leg = {1:F2} s" -f $GameFps, $legSec)

# -- the pad ----------------------------------------------------------------

if (-not $Editor) {
    # scripts -> tyra-testing -> skills -> .claude -> the repo root.
    $root = Split-Path (Split-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) -Parent) -Parent
    foreach ($c in @('build-gate\tyrax-editor.exe', 'build-dev\tyrax-editor.exe')) {
        $p = Join-Path $root $c
        if (Test-Path $p) { $Editor = $p; break }
    }
}
if (-not $NoPad) {
    if (-not $Editor -or -not (Test-Path $Editor)) {
        Write-Warning "No editor binary found for --pad; the route will not be resynced (its PHASE is then unknown, its shape is not)."
        $NoPad = $true
    }
}

$routeZero = $null
if (-not $NoPad) {
    # Start-Process -ArgumentList does NOT quote its array elements, so a
    # ';'-separated pad script arrives as loose argv, the driver dies on
    # stderr nobody reads, and the game looks like it has no pad channel.  This
    # call is a direct native invocation from a .ps1 (which quotes correctly)
    # and its stderr AND exit code are read, out loud.
    $err = Join-Path $env:TEMP ("motion-gate-pad-{0}.txt" -f $PID)
    $routeZero = GameFrame
    $padOut = & $Editor --pad $Project $Pad 2>$err
    $padCode = $LASTEXITCODE
    $padErr = if (Test-Path $err) { (Get-Content $err -Raw) } else { '' }
    Remove-Item $err -ErrorAction SilentlyContinue
    if ($padCode -ne 0) {
        Write-Warning ("--pad exited {0}: {1}" -f $padCode, ($padErr + $padOut))
    } elseif ($padErr -and $padErr.Trim()) {
        Write-Warning ("--pad said: {0}" -f $padErr.Trim())
    }
    Write-Output ("route resynced at game frame {0} ({1})" -f $routeZero, $Pad)
}
# Zero of the route, as near as the host can know it.  The pad press is a
# ONE-SHOT event on purpose: its few frames of arrival jitter shift the whole
# route equally instead of perturbing each capture, which is the only way a
# 25 Hz host-clock pad driver can take part in a deterministic measurement.
$routeClock = [Diagnostics.Stopwatch]::StartNew()
# -- the burst --------------------------------------------------------------

$full = New-Object System.Drawing.Bitmap $pwSize[0], $pwSize[1]
$crop = New-Object System.Drawing.Bitmap $W, $H
$srcRect = New-Object System.Drawing.Rectangle ($rect[0] - $pwOrigin[0]), ($rect[1] - $pwOrigin[1]), $W, $H
$dstRect = New-Object System.Drawing.Rectangle 0, 0, $W, $H
$lockRect = New-Object System.Drawing.Rectangle 0, 0, $W, $H

# One leg's worth of captures, taken BACK TO BACK - this loop sleeps for
# nothing.  A fixed interval is the failure that started all of this: an even
# frame stride lands on the same jitter phase forever and reports a perfectly
# still picture.  The achieved interval is measured and reported instead, and
# the caller is warned when it happens to land on a whole number of frames.
function Burst([string]$dir, [string]$legName) {
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Get-ChildItem $dir -Filter 'frame*.bin' -ErrorAction SilentlyContinue | Remove-Item -Force
    $buffers = New-Object System.Collections.ArrayList
    $times = New-Object System.Collections.ArrayList
    $gf0 = GameFrame
    $clock = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt $Count; $i++) {
        $g = [System.Drawing.Graphics]::FromImage($full)
        if ($Gdi) {
            $g.CopyFromScreen($pwOrigin[0], $pwOrigin[1], 0, 0, $full.Size)
        } else {
            $hdc = $g.GetHdc()
            [void][Win32Gate]::PrintWindow($pwHwnd, $hdc, 2)   # PW_RENDERFULLCONTENT
            $g.ReleaseHdc($hdc)
        }
        $g.Dispose()
        $cg = [System.Drawing.Graphics]::FromImage($crop)
        $cg.DrawImage($full, $dstRect, $srcRect, [System.Drawing.GraphicsUnit]::Pixel)
        $cg.Dispose()
        $data = $crop.LockBits($lockRect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                               [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
        $buf = New-Object byte[] ($W * $H * 4)
        for ($row = 0; $row -lt $H; $row++) {
            [System.Runtime.InteropServices.Marshal]::Copy(
                [IntPtr]::Add($data.Scan0, $row * $data.Stride), $buf, $row * $W * 4, $W * 4)
        }
        $crop.UnlockBits($data)
        [void]$buffers.Add($buf)
        [void]$times.Add($clock.Elapsed.TotalSeconds)
    }
    $clock.Stop()
    $gf1 = GameFrame
    $span = $times[$times.Count - 1]

    $dts = @()
    for ($i = 1; $i -lt $times.Count; $i++) { $dts += ($times[$i] - $times[$i - 1]) }
    $meanDt = ($dts | Measure-Object -Average).Average
    $fps = $null
    if ($null -ne $gf0 -and $null -ne $gf1 -and $gf1 -gt $gf0 -and $span -gt 0.1) {
        $fps = ($gf1 - $gf0) / $span
    }
    $lum0 = [Win32Gate]::MeanLuma($buffers[0])
    $msg = ("  {0,-7} {1} captures in {2:F2} s = {3:F1} Hz (min {4:F3}, max {5:F3} s), luma {6:F1}" -f
            $legName, $buffers.Count, $span, (1.0 / $meanDt),
            ($dts | Measure-Object -Minimum).Minimum, ($dts | Measure-Object -Maximum).Maximum, $lum0)
    if ($null -ne $fps) {
        $stride = $meanDt * $fps
        $msg += (", game {0:F1} fps, {1:F2} game frames per capture" -f $fps, $stride)
    }
    Write-Output $msg
    if ($null -ne $fps) {
        # THE trap this whole file exists for.
        $fr = [Math]::Abs($stride - [Math]::Round($stride))
        if ($fr -lt 0.04 -and $stride -ge 1.5) {
            Write-Warning ("the capture interval is {0:F2} game frames - within {1:F2} of a WHOLE number. " -f $stride, $fr +
                           "That is an even stride, and an even stride samples one phase of a period-2 " +
                           "artefact forever: it is exactly how the jitter shake survived its first harness. " +
                           "Re-run - PrintWindow's own timing jitter normally prevents it.")
        }
    } else {
        Write-Warning ("bin\livedbg.bin gave no frame counter, so the capture stride is UNKNOWN " +
                       "(the Live Debugger preference is off, or the game is not running). The analysis " +
                       "still works, but nothing cross-checks how many game frames a capture pair spans.")
    }
    if ($lum0 -lt 4.0) {
        Write-Warning ("the first capture of '{0}' is essentially black. Either the game has not booted, " -f $legName +
                       "or this renderer will not draw on demand and -PrintWindow is handing back nothing - " +
                       "take one screenshot-window.ps1 shot before believing any of these numbers.")
    }

    $frames = @()
    for ($i = 0; $i -lt $buffers.Count; $i++) {
        $name = 'frame{0:d3}.bin' -f $i
        [System.IO.File]::WriteAllBytes((Join-Path $dir $name), $buffers[$i])
        $gfEst = $null
        # Interpolated between the two reads that bracket the burst, NOT
        # sampled per capture: 45 reads a second of a file the game is writing
        # through HostFs is exactly the sort of poking that moved the
        # black-frame defect's trigger when somebody instrumented its loop.
        if ($null -ne $gf0 -and $null -ne $fps) { $gfEst = [int]($gf0 + $fps * $times[$i]) }
        $frames += @{ i = $i; t = [Math]::Round($times[$i], 4); file = $name; gameFrame = $gfEst }
    }
    $meta = @{
        w = $W; h = $H; count = $buffers.Count; format = 'bgra'
        project = $Project; pid = $ProcessId; note = $Note; legName = $legName
        backend = $(if ($Gdi) { 'gdi' } else { 'printwindow' })
        capturedAt = (Get-Date).ToString('s')
        gameFrame0 = $gf0; gameFrame1 = $gf1; fps = $fps
        routeZeroFrame = $routeZero; legFrames = $LegFrames
        frames = $frames
    }
    $meta | ConvertTo-Json -Depth 5 | Set-Content -Path (Join-Path $dir 'burst.json') -Encoding utf8
}


$names = @($Legs -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$canon = @('hold', 'pan', 'dolly', 'return')
foreach ($n in $names) {
    $k = [Array]::IndexOf($canon, $n)
    if ($k -lt 0) { Fail "unknown leg '$n' (want one of: $($canon -join ', '))" }
    # Wait in GAME FRAMES, not in seconds.  The route is indexed by the frame
    # counter, and the emulator's rate is neither constant nor equal to the
    # video mode's: this fixture measured 52-61 fps across its own four legs,
    # which over four legs of wall-clock scheduling walks the burst clean out
    # of the leg it is supposed to be in.  Falls back to the clock, loudly,
    # when the Live Debugger channel is not there to ask.
    $due = $routeZero + $k * $LegFrames + [int]($LegLead * $GameFps)
    if ($null -ne $routeZero) {
        $guard = [Diagnostics.Stopwatch]::StartNew()
        while ($true) {
            $now = GameFrame
            if ($null -eq $now -or $now -ge $due) { break }
            $left = ($due - $now) / $GameFps
            if ($guard.Elapsed.TotalSeconds -gt ($LegFrames * 4 / $GameFps + 5)) {
                Write-Warning "waited past a whole route loop for leg '$n' - the game may be stalled."
                break
            }
            Start-Sleep -Milliseconds ([Math]::Max(10, [int]([Math]::Min($left, 0.25) * 1000)))
        }
    } else {
        $wait = $k * $legSec + $LegLead - $routeClock.Elapsed.TotalSeconds
        if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]($wait * 1000)) }
    }
    Burst (Join-Path $Out $n) $n
}
$full.Dispose(); $crop.Dispose()
Write-Output ("wrote {0} ({1:F0} MB)" -f $Out, (($W * $H * 4.0 * $Count * $names.Count) / 1MB))

if (-not $NoAnalyse) {
    $py = Join-Path $PSScriptRoot 'motion-gate.py'
    $argv = @($py, $Out)
    if ($Baseline) { $argv += @('--baseline', $Baseline) }
    & $Python @argv
    exit $LASTEXITCODE
}
