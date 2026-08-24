# Emits the host mouse position over the QEMU window as lines of
#
#     <x> <y> <down> <clientWidth> <clientHeight>
#
# in client-area pixels, for scripts/qemu-touch.py to translate into panel
# coordinates and inject. Polling GetCursorPos beats installing a mouse hook:
# the window belongs to another process, the rate only has to match the panel,
# and nothing here needs to run in QEMU's message loop.
param(
    [int]$IntervalMs = 16,
    [int]$HeartbeatMs = 100
)
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public class MouseProbe {
  [DllImport("user32.dll")] public static extern bool GetCursorPos(out POINT p);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool ScreenToClient(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vKey);
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

$VK_LBUTTON = 0x01
$proc = $null
$lastEmit = [DateTime]::MinValue
$lastLine = ""
$warnedNoDesktop = $false

while ($true) {
    if ($null -eq $proc -or $proc.HasExited) {
        $proc = Get-Process -Name qemu-system-xtensa -ErrorAction SilentlyContinue |
                Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
        if ($null -eq $proc) { Start-Sleep -Milliseconds 250; continue }
    }
    $h = $proc.MainWindowHandle

    $rect = New-Object MouseProbe+RECT
    if (-not [MouseProbe]::GetClientRect($h, [ref]$rect)) {
        # Window went away between the lookup and here; re-acquire next round.
        $proc = $null
        Start-Sleep -Milliseconds 250
        continue
    }
    $pt = New-Object MouseProbe+POINT
    if (-not [MouseProbe]::GetCursorPos([ref]$pt)) {
        # No input desktop: the usual cause is a disconnected or locked RDP
        # session, where there is no pointer to read and injected clicks would
        # go nowhere either. Say so once instead of spinning silently.
        if (-not $warnedNoDesktop) {
            [Console]::Error.WriteLine("qemu-mouse: GetCursorPos failed; no input desktop (session disconnected or locked?). Idling.")
            $warnedNoDesktop = $true
        }
        Start-Sleep -Milliseconds 500
        continue
    }
    if ($warnedNoDesktop) {
        [Console]::Error.WriteLine("qemu-mouse: input desktop back, resuming.")
        $warnedNoDesktop = $false
    }
    [void][MouseProbe]::ScreenToClient($h, [ref]$pt)

    $w = $rect.R - $rect.L
    $h2 = $rect.B - $rect.T
    $inside = ($pt.X -ge 0 -and $pt.Y -ge 0 -and $pt.X -lt $w -and $pt.Y -lt $h2)
    # The high bit is "currently down". A press that started outside the window
    # still counts as a drag once the pointer enters it, which matches how a
    # touchscreen behaves when a finger slides in from the bezel.
    $down = $inside -and (([MouseProbe]::GetAsyncKeyState($VK_LBUTTON) -band 0x8000) -ne 0)

    $line = "$($pt.X) $($pt.Y) $([int]$down) $w $h2"
    $age = ([DateTime]::UtcNow - $lastEmit).TotalMilliseconds
    # Send on any change, and keep re-sending while held so the guest's press
    # expiry never fires mid-drag. Nothing goes out while idle and released.
    if ($line -ne $lastLine -or ($down -and $age -ge $HeartbeatMs)) {
        [Console]::Out.WriteLine($line)
        [Console]::Out.Flush()
        $lastLine = $line
        $lastEmit = [DateTime]::UtcNow
    }
    Start-Sleep -Milliseconds $IntervalMs
}
