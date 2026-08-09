# Reliable window capture -- one shot, or a whole SEQUENCE collapsed into a
# single contact sheet.
# PCSX2's built-in F8 screenshot is flaky when triggered via SendKeys; this is not.
# Usage:
#   powershell -File screenshot-window.ps1 -ProcessName pcsx2-qt -OutFile C:\path\shot.png
#   powershell -File screenshot-window.ps1 -ProcessName tyrax-editor -OutFile shot.png
#   powershell -File screenshot-window.ps1 -OutFile shot.png -Auto        # render area only
#   powershell -File screenshot-window.ps1 -OutFile shot.png -Auto -PrintWindow   # occluded OK
#   powershell -File screenshot-window.ps1 -Watch C:\path\w -Auto -Every 1 -For 20 -Tile 224
#
# TWO capture back-ends, and the choice matters more than it looks:
#
#   default        GDI CopyFromScreen.  Reads the SCREEN, so it captures
#                  whatever is physically on those pixels -- an occluded window
#                  captures as whatever covers it, SILENTLY and plausibly.  The
#                  script raises the window first and warns when that failed,
#                  but raising steals focus, which is not always allowed.
#   -PrintWindow   PrintWindow(PW_RENDERFULLCONTENT).  Asks the window to draw
#                  ITSELF, so it works while the window is fully covered, needs
#                  no focus and moves nothing on the user's desktop.  It can
#                  come back blank on renderers that refuse to redraw on demand,
#                  so this script MEASURES the result and says so rather than
#                  handing back a black picture.
#
# Rule of thumb: `-PrintWindow` whenever a human is at the machine or anything
# might be in front of the window; the GDI default when the window is clear and
# you want the proven `-Watch` path.
#
# `-Watch DIR` samples the window on an interval, keeps the full-resolution
# frames on disk as DIR\frameNN.png, and reports ONE downscaled contact sheet
# plus a changed-pixel table -- so watching a game for a minute costs about as
# much context as a single screenshot instead of thirty.  It is the Windows twin
# of `wayland-control.py watch`; the flag names and the output lines match, and
# so does the diff metric, so the numbers are comparable across the two OSes.
#
# The one real difference is how the render area is found.  Windows HAS
# per-window capture, so `-Auto` takes the biggest visible CHILD window of the
# process (PCSX2's render surface is one) instead of guessing from motion --
# deterministic, and it works on a paused emulator or a parked camera, both of
# which defeat the Linux heuristic.  `-Area` is therefore WINDOW-relative here
# and is not cached: the window may have moved since the last run, while its
# own geometry is always available.
param(
    [string]$ProcessName = 'pcsx2-qt',
    # Parallel worktree sessions each run their own PCSX2, and -ProcessName
    # takes whichever it finds FIRST -- which silently captures somebody else's
    # game.  Select the pid off the -elf path when more than one is up:
    #   Get-CimInstance Win32_Process -Filter "name='pcsx2-qt.exe'" |
    #       Where-Object CommandLine -like '*<project>*'
    [int]$ProcessId = 0,
    [Parameter(ParameterSetName = 'Shot', Mandatory = $true, Position = 0)][string]$OutFile,
    [Parameter(ParameterSetName = 'Watch', Mandatory = $true)][string]$Watch,
    # Geometry (both modes).  -Area X,Y,W,H is relative to the window's
    # top-left -- the same coordinates you read off a capture.
    [string]$Area,
    [switch]$Auto,
    [switch]$Client,
    [switch]$Trim,
    [switch]$NoActivate,
    # Capture the window's OWN content instead of the screen pixels in front of
    # it.  Implies -NoActivate: PrintWindow raises nothing and steals no focus.
    [switch]$PrintWindow,
    # Sampling (-Watch only).
    [Parameter(ParameterSetName = 'Watch')][double]$Every = 1.0,
    [Parameter(ParameterSetName = 'Watch')][int]$Count = 8,
    [Parameter(ParameterSetName = 'Watch')][double]$For = 0,
    [Parameter(ParameterSetName = 'Watch')][int]$Tile = 256,
    [Parameter(ParameterSetName = 'Watch')][int]$Cols = 0,
    [Parameter(ParameterSetName = 'Watch')][string]$Sheet = 'sheet.png',
    [Parameter(ParameterSetName = 'Watch')][double]$OnlyChanged = 0,
    [Parameter(ParameterSetName = 'Watch')][int]$IdleStop = 0,
    [Parameter(ParameterSetName = 'Watch')][double]$IdleBelow = 0.05,
    [Parameter(ParameterSetName = 'Watch')][switch]$NoFrames
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

# Per-pixel luma delta that counts as "changed".  Same constant, same formula
# (ITU-R 601-2 over the absolute per-channel differences) as the PIL code in
# wayland-control.py, so a percentage means the same thing on both platforms.
$DIFF_THRESHOLD = 16

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Shot {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int cmd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, IntPtr pid);
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint from, uint to, bool attach);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, IntPtr extra);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hWnd, ref POINT pt);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr hWnd, EnumProc cb, IntPtr p);
    // The occlusion-proof back-end: the window renders itself into our DC, so
    // nothing has to be visible, in front, or focused.  Flag 2 is
    // PW_RENDERFULLCONTENT, which is what reaches DirectComposition/GPU
    // surfaces -- without it a hardware-accelerated child prints as black.
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);
    public delegate bool EnumProc(IntPtr hWnd, IntPtr param);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct POINT { public int X, Y; }

    // A GDI grab reads the SCREEN, so an occluded window captures as whatever
    // covers it -- silently, and for every frame of a watch.  A plain
    // SetForegroundWindow from a background shell does nothing at all, so use
    // the ALT-tap + AttachThreadInput trick the skill documents and then VERIFY.
    public static bool Raise(IntPtr hWnd) {
        ShowWindow(hWnd, 9);   // SW_RESTORE
        keybd_event(0x12, 0, 0, IntPtr.Zero);
        keybd_event(0x12, 0, 2, IntPtr.Zero);
        uint self = GetCurrentThreadId();
        uint target = GetWindowThreadProcessId(hWnd, IntPtr.Zero);
        AttachThreadInput(self, target, true);
        BringWindowToTop(hWnd);
        SetForegroundWindow(hWnd);
        AttachThreadInput(self, target, false);
        return GetForegroundWindow() == hWnd;
    }

    // The Windows answer to "which part of the window is the picture": the
    // render surface is a native child HWND (PCSX2's display widget is one),
    // so the biggest visible child IS the render area -- no motion needed.
    static RECT best;
    static long bestArea;
    // The HWND behind the winning rect, kept because PrintWindow addresses a
    // WINDOW, not a screen rectangle: printing the parent can miss a GPU child.
    public static IntPtr bestHwnd;
    public static int[] BiggestChild(IntPtr parent) {
        best = new RECT(); bestArea = 0; bestHwnd = IntPtr.Zero;
        EnumChildWindows(parent, new EnumProc(Consider), IntPtr.Zero);
        if (bestArea <= 0) return null;
        return new int[] { best.Left, best.Top, best.Right - best.Left, best.Bottom - best.Top };
    }
    static bool Consider(IntPtr h, IntPtr p) {
        if (!IsWindowVisible(h)) return true;
        RECT r = new RECT();
        GetWindowRect(h, out r);
        long a = (long)(r.Right - r.Left) * (long)(r.Bottom - r.Top);
        if (a > bestArea) { bestArea = a; best = r; bestHwnd = h; }
        return true;
    }

    // Pixel work in C#: a per-pixel loop in PowerShell costs seconds per frame,
    // which would eat the sampling interval.  Both take a BGRA byte array as
    // LockBits hands it over, so no System.Drawing reference is needed here.
    public static long CountDiff(byte[] a, byte[] b, int threshold) {
        long n = 0;
        int len = a.Length < b.Length ? a.Length : b.Length;
        for (int i = 0; i + 3 < len; i += 4) {
            int d0 = a[i] - b[i];         if (d0 < 0) d0 = -d0;
            int d1 = a[i + 1] - b[i + 1]; if (d1 < 0) d1 = -d1;
            int d2 = a[i + 2] - b[i + 2]; if (d2 < 0) d2 = -d2;
            if ((299 * d2 + 587 * d1 + 114 * d0) / 1000 > threshold) n++;
        }
        return n;
    }
    // Bounding box of everything brighter than floor -- PCSX2 pads the 4:3
    // picture with black bars, and those bars are exactly what -Trim drops.
    public static int[] ContentBox(byte[] a, int w, int h, int floor) {
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++) {
            int row = y * w * 4;
            for (int x = 0; x < w; x++) {
                int i = row + x * 4;
                if ((299 * a[i + 2] + 587 * a[i + 1] + 114 * a[i]) / 1000 <= floor) continue;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
        if (x1 < 0) return null;
        return new int[] { x0, y0, x1 - x0 + 1, y1 - y0 + 1 };
    }
}
"@

# Without DPI awareness, GetWindowRect/CopyFromScreen work in virtualized
# coordinates on displays scaled above 100% and the capture comes out as a
# scaled-up crop of the window's top-left corner.
[Win32Shot]::SetProcessDPIAware() | Out-Null

# Set when -PrintWindow selected a target; everything below keeps working in
# SCREEN coordinates either way, so -Auto / -Area / -Client / -Trim are shared.
$script:PwHwnd = [IntPtr]::Zero
$script:PwOrigin = @(0, 0)
$script:PwSize = @(0, 0)
$script:PwChecked = $false

function Grab([int]$x, [int]$y, [int]$w, [int]$h) {
    if ($script:PwHwnd -ne [IntPtr]::Zero) { return GrabPrint $x $y $w $h }
    $bmp = New-Object System.Drawing.Bitmap $w, $h
    $gfx = [System.Drawing.Graphics]::FromImage($bmp)
    $gfx.CopyFromScreen($x, $y, 0, 0, $bmp.Size)
    $gfx.Dispose()
    return $bmp
}

# Print the whole target window, then crop to the requested screen rect.  Two
# reasons not to print a sub-rect directly: PrintWindow has no such parameter,
# and going through the full bitmap keeps this path's coordinates identical to
# the GDI one, so a rect read off a GDI capture still means the same thing.
function GrabPrint([int]$x, [int]$y, [int]$w, [int]$h) {
    $fw = $script:PwSize[0]
    $fh = $script:PwSize[1]
    $full = New-Object System.Drawing.Bitmap $fw, $fh
    $gfx = [System.Drawing.Graphics]::FromImage($full)
    $hdc = $gfx.GetHdc()
    $ok = [Win32Shot]::PrintWindow($script:PwHwnd, $hdc, 2)   # PW_RENDERFULLCONTENT
    $gfx.ReleaseHdc($hdc)
    $gfx.Dispose()
    # A renderer that refuses to redraw on demand hands back a black rectangle,
    # and a black rectangle is a plausible-looking screenshot of a game that has
    # not booted yet.  Say it ONCE, loudly, instead of letting it read as a
    # finding -- this is the failure mode that pays for the GDI path's survival.
    if (-not $script:PwChecked) {
        $script:PwChecked = $true
        $box = [Win32Shot]::ContentBox((BitmapBytes $full), $fw, $fh, 10)
        if (-not $ok -or -not $box) {
            Write-Warning ("PrintWindow returned nothing but black for hwnd {0} (returned {1}). " -f $script:PwHwnd, $ok +
                           "This renderer will not draw on demand: drop -PrintWindow and let the script " +
                           "raise the window instead (which needs it uncovered, and steals focus).")
        }
    }
    $lx = $x - $script:PwOrigin[0]
    $ly = $y - $script:PwOrigin[1]
    if ($lx -eq 0 -and $ly -eq 0 -and $w -eq $fw -and $h -eq $fh) { return $full }
    $crop = New-Object System.Drawing.Bitmap $w, $h
    $cg = [System.Drawing.Graphics]::FromImage($crop)
    $cg.DrawImage($full, (New-Object System.Drawing.Rectangle 0, 0, $w, $h),
                  (New-Object System.Drawing.Rectangle $lx, $ly, $w, $h),
                  [System.Drawing.GraphicsUnit]::Pixel)
    $cg.Dispose()
    $full.Dispose()
    return $crop
}

function BitmapBytes($bmp) {
    $rect = New-Object System.Drawing.Rectangle 0, 0, $bmp.Width, $bmp.Height
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppRgb)
    $buf = New-Object byte[] ($data.Stride * $bmp.Height)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $buf, 0, $buf.Length)
    $bmp.UnlockBits($data)
    # `,` keeps the byte[] whole: a bare `return $buf` UNROLLS it into the
    # pipeline and hands back an Object[] of two million boxed bytes, which cost
    # ~2 s per frame here (and re-marshalling it on every CountDiff call again).
    return , $buf
}

function Thumb($bmp, [int]$tw) {
    $th = [Math]::Max(1, [int][Math]::Round($bmp.Height * $tw / $bmp.Width))
    $t = New-Object System.Drawing.Bitmap $tw, $th
    $gfx = [System.Drawing.Graphics]::FromImage($t)
    $gfx.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $gfx.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $gfx.DrawImage($bmp, 0, 0, $tw, $th)
    $gfx.Dispose()
    return $t
}

# Lay labelled thumbnails out in a grid -- ONE image for N moments.
function ContactSheet($tiles, [string]$path, [int]$cols) {
    $n = $tiles.Count
    if ($cols -le 0) {
        $cols = [Math]::Min($n, [Math]::Max(1, [int][Math]::Ceiling([Math]::Sqrt($n))))
    }
    $rows = [int][Math]::Ceiling($n / [double]$cols)
    $tw = $tiles[0].Img.Width
    $th = $tiles[0].Img.Height
    $pad = 6
    $lab = 16
    $canvas = New-Object System.Drawing.Bitmap (($cols * $tw + ($cols + 1) * $pad)),
                                              (($rows * ($th + $lab) + ($rows + 1) * $pad))
    $gfx = [System.Drawing.Graphics]::FromImage($canvas)
    $gfx.Clear([System.Drawing.Color]::FromArgb(24, 24, 24))
    try { $font = New-Object System.Drawing.Font 'Consolas', 8 }
    catch { $font = [System.Drawing.SystemFonts]::DefaultFont }
    $brush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(210, 210, 210))
    for ($i = 0; $i -lt $n; $i++) {
        $x = $pad + ($i % $cols) * ($tw + $pad)
        $y = $pad + [int][Math]::Floor($i / $cols) * ($th + $lab + $pad)
        $gfx.DrawImage($tiles[$i].Img, $x, $y)
        $gfx.DrawString($tiles[$i].Label, $font, $brush, $x, $y + $th + 1)
    }
    $gfx.Dispose()
    $brush.Dispose()
    $canvas.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    return $canvas
}

# Roughly what an image costs in a Claude context window.
function TokenEstimate([double]$w, [double]$h) {
    $m = [Math]::Max($w, $h)
    if ($m -gt 1568) { $s = 1568.0 / $m; $w = $w * $s; $h = $h * $s }
    return [int]($w * $h / 750)
}

# -- pick the window and the rect ------------------------------------------

if ($ProcessId -gt 0) {
    $proc = Get-Process -Id $ProcessId -ErrorAction Stop
    if ($proc.MainWindowHandle -eq 0) { throw "Process $ProcessId has no main window." }
} else {
    $all = @(Get-Process $ProcessName -ErrorAction SilentlyContinue |
             Where-Object { $_.MainWindowHandle -ne 0 })
    if ($all.Count -eq 0) { throw "No window found for process '$ProcessName'." }
    if ($all.Count -gt 1) {
        Write-Warning ("{0} '{1}' windows are open; capturing pid {2}. Pass -ProcessId to pick one " -f
                       $all.Count, $ProcessName, $all[0].Id +
                       "(parallel worktrees each run their own PCSX2, and the wrong one looks like a screenshot).")
    }
    $proc = $all[0]
}
$hwnd = $proc.MainWindowHandle

# Bring to front so the capture isn't occluded by other windows -- and say so
# when it did not work, because a silent capture of somebody else's window is
# the worst failure this script has (it looks like a screenshot).
if (-not $NoActivate -and -not $PrintWindow) {
    $raised = [Win32Shot]::Raise($hwnd)
    Start-Sleep -Milliseconds 400
    if (-not $raised -and [Win32Shot]::GetForegroundWindow() -ne $hwnd) {
        Write-Warning ("'{0}' did not come to the front: the capture WILL show whatever covers it, " -f $ProcessName +
                       "for every frame, without looking wrong.  Pass -PrintWindow to read the window's " +
                       "own content instead, move it out from under the other windows, or pass " +
                       "-NoActivate if it is already clear.")
    }
}

$wrect = New-Object Win32Shot+RECT
[Win32Shot]::GetWindowRect($hwnd, [ref]$wrect) | Out-Null
# Parenthesise every element: inside an array literal the comma binds tighter
# than the arithmetic, so a bare `a - b` term subtracts from the whole array.
$rect = @($wrect.Left, $wrect.Top, ($wrect.Right - $wrect.Left), ($wrect.Bottom - $wrect.Top))
$origin = 'window'
if ($rect[2] -le 0 -or $rect[3] -le 0) { throw "Window has zero size (minimized?)." }

if ($Auto) {
    $child = [Win32Shot]::BiggestChild($hwnd)
    # A child smaller than a fifth of the window is a toolbar or a status bar,
    # not the picture; fall back rather than hand back the wrong thing.
    if ($child -and ($child[2] * [double]$child[3]) -ge 0.2 * $rect[2] * $rect[3]) {
        $rect = $child
        $origin = 'render child'
    } else {
        $Client = $true
        $origin = 'client (no render child found)'
    }
}
if ($Client -and $origin -notlike 'render child*') {
    $crect = New-Object Win32Shot+RECT
    [Win32Shot]::GetClientRect($hwnd, [ref]$crect) | Out-Null
    $pt = New-Object Win32Shot+POINT
    [Win32Shot]::ClientToScreen($hwnd, [ref]$pt) | Out-Null
    $rect = @($pt.X, $pt.Y, ($crect.Right - $crect.Left), ($crect.Bottom - $crect.Top))
    if ($origin -eq 'window') { $origin = 'client' }
}
if ($Area) {
    $a = @($Area -split '[,x]' | ForEach-Object { [int]$_ })
    if ($a.Count -ne 4) { throw "-Area wants X,Y,W,H (relative to the window's top-left)." }
    $rect = @(($wrect.Left + $a[0]), ($wrect.Top + $a[1]), $a[2], $a[3])
    $origin = 'given'
}
# Pick what PrintWindow addresses, once the rect is final.  Prefer the render
# child -- PW_RENDERFULLCONTENT on a top-level window does not always reach a
# GPU-composited child -- but only while the rect actually lies inside it, so a
# main-window-relative -Area still prints the window that contains it.
if ($PrintWindow) {
    $target = $hwnd
    $torigin = @($wrect.Left, $wrect.Top)
    $tsize = @(($wrect.Right - $wrect.Left), ($wrect.Bottom - $wrect.Top))
    $childRect = [Win32Shot]::BiggestChild($hwnd)
    if ($childRect -and [Win32Shot]::bestHwnd -ne [IntPtr]::Zero -and
        $rect[0] -ge $childRect[0] -and $rect[1] -ge $childRect[1] -and
        ($rect[0] + $rect[2]) -le ($childRect[0] + $childRect[2]) -and
        ($rect[1] + $rect[3]) -le ($childRect[1] + $childRect[3])) {
        $target = [Win32Shot]::bestHwnd
        $torigin = @($childRect[0], $childRect[1])
        $tsize = @($childRect[2], $childRect[3])
    }
    $script:PwHwnd = $target
    $script:PwOrigin = $torigin
    $script:PwSize = $tsize
    $origin = "$origin, printwindow"
}

if ($Trim) {
    $probe = Grab $rect[0] $rect[1] $rect[2] $rect[3]
    $box = [Win32Shot]::ContentBox((BitmapBytes $probe), $probe.Width, $probe.Height, 10)
    $probe.Dispose()
    if ($box) {
        $rect = @(($rect[0] + $box[0]), ($rect[1] + $box[1]), $box[2], $box[3])
        $origin = "$origin, trimmed"
    }
}

# -- single shot -----------------------------------------------------------

if (-not $Watch) {
    $bmp = Grab $rect[0] $rect[1] $rect[2] $rect[3]
    $bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Output (Resolve-Path $OutFile).Path
    return
}

# -- watch: a downscaled contact sheet instead of N full screenshots -------

if (-not (Test-Path $Watch)) { New-Item -ItemType Directory -Path $Watch -Force | Out-Null }
if ($For -gt 0) { $Count = [Math]::Max(1, [int]($For / $Every) + 1) }
Write-Output ("area {0},{1},{2},{3} ({4})" -f ($rect[0] - $wrect.Left), ($rect[1] - $wrect.Top),
                                              $rect[2], $rect[3], $origin)

$total = [double]($rect[2] * $rect[3])
$tiles = New-Object System.Collections.ArrayList
$prev = $null       # bytes of the previous frame, kept or not -- drives -IdleStop
$keptPrev = $null   # bytes of the last KEPT frame -- what the table reports against
$idle = 0
$clock = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $Count; $i++) {
    $wait = $i * $Every * 1000 - $clock.Elapsed.TotalMilliseconds
    if ($wait -gt 0) { Start-Sleep -Milliseconds ([int]$wait) }
    $bmp = Grab $rect[0] $rect[1] $rect[2] $rect[3]
    $t = $clock.Elapsed.TotalSeconds
    $bytes = BitmapBytes $bmp

    # The reported delta is against the last KEPT frame, so a skipped frame's
    # number explains why it was skipped and the sheet's labels describe the
    # tiles next to each other.
    if ($null -eq $keptPrev) {
        $n = 0
        $share = 0.0
        $delta = '     -   '
    } else {
        $n = [Win32Shot]::CountDiff($keptPrev, $bytes, $DIFF_THRESHOLD)
        $share = $n / $total
        $delta = '{0,7:F3}%' -f ($share * 100)
    }
    $live = 0.0
    if ($null -ne $prev) { $live = [Win32Shot]::CountDiff($prev, $bytes, $DIFF_THRESHOLD) / $total }
    $prev = $bytes

    $keep = ($null -eq $keptPrev) -or ($share * 100 -ge $OnlyChanged)
    $name = 'frame{0:d2}.png' -f $i
    if ($keep) {
        if (-not $NoFrames) { $bmp.Save((Join-Path $Watch $name), [System.Drawing.Imaging.ImageFormat]::Png) }
        [void]$tiles.Add(@{
            Img   = (Thumb $bmp $Tile)
            Label = ('#{0:d2} t={1,5:F1}s d={2}' -f $i, $t, $delta.Trim())
        })
        $keptPrev = $bytes
    }
    $bmp.Dispose()
    $mark = ' '
    if (-not $keep) { $mark = '-' }
    $what = $name
    if (-not $keep) { $what = '(skipped)' }
    Write-Output ('{0}#{1:d2} t={2,6:F2}s d={3} {4,9:d}px {5}' -f $mark, $i, $t, $delta, $n, $what)

    if ($IdleStop -gt 0) {
        if ($i -gt 0 -and ($live * 100) -lt $IdleBelow) { $idle++ } else { $idle = 0 }
        if ($idle -ge $IdleStop) {
            Write-Output ("idle-stop: {0} frames under {1}% in a row" -f $idle, $IdleBelow)
            break
        }
    }
}
if ($tiles.Count -eq 0) {
    Write-Output 'no frames kept'
    return
}
$path = $Sheet
if (-not [System.IO.Path]::IsPathRooted($path)) { $path = Join-Path $Watch $Sheet }
$img = ContactSheet $tiles $path $Cols   # NOT $sheet: that is the [string] parameter
Write-Output ('{0} {1}x{2} -- {3} tile(s), ~{4} tokens (one full frame would be ~{5})' -f
              (Resolve-Path $path).Path, $img.Width, $img.Height, $tiles.Count,
              (TokenEstimate $img.Width $img.Height), (TokenEstimate $rect[2] $rect[3]))
$img.Dispose()
foreach ($item in $tiles) { $item.Img.Dispose() }   # NOT $tile: that is the [int] parameter
