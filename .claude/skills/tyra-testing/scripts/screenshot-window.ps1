# Reliable window screenshot via GDI CopyFromScreen.
# PCSX2's built-in F8 screenshot is flaky when triggered via SendKeys; this is not.
# Usage:
#   powershell -File screenshot-window.ps1 -ProcessName pcsx2-qt -OutFile C:\path\shot.png
#   powershell -File screenshot-window.ps1 -ProcessName tyra-editor -OutFile shot.png
param(
    [string]$ProcessName = 'pcsx2-qt',
    [Parameter(Mandatory = $true)][string]$OutFile
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Win32Shot {
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
}
"@

# Without DPI awareness, GetWindowRect/CopyFromScreen work in virtualized
# coordinates on displays scaled above 100% and the capture comes out as a
# scaled-up crop of the window's top-left corner.
[Win32Shot]::SetProcessDPIAware() | Out-Null

$proc = Get-Process $ProcessName -ErrorAction SilentlyContinue |
    Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { throw "No window found for process '$ProcessName'." }

# Bring to front so the capture isn't occluded by other windows.
[Win32Shot]::SetForegroundWindow($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 400

$rect = New-Object Win32Shot+RECT
[Win32Shot]::GetWindowRect($proc.MainWindowHandle, [ref]$rect) | Out-Null
$w = $rect.Right - $rect.Left
$h = $rect.Bottom - $rect.Top
if ($w -le 0 -or $h -le 0) { throw "Window has zero size (minimized?)." }

$bmp = New-Object System.Drawing.Bitmap $w, $h
$gfx = [System.Drawing.Graphics]::FromImage($bmp)
$gfx.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bmp.Size)
$bmp.Save($OutFile, [System.Drawing.Imaging.ImageFormat]::Png)
$gfx.Dispose()
$bmp.Dispose()
Write-Output (Resolve-Path $OutFile).Path
