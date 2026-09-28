# WP-24-T03 验收段 GUI 冒烟驱动工具（acc/WP-24-T03/2）。
# 复刻 builds/wp10-t18/smoke-tools.ps1 先例的 Win32 调用族，改用命名参数
# （验收会话的 pwsh -File 通道对位置参数绑定不可靠）。
# 用法：pwsh -NoProfile -File acc-smoke-tools.ps1 -Command shot -Arg1 <png> [-Arg2 <条带高度>]
#       -Command click -Arg1 <x> -Arg2 <y> ／ key <文本> ／ activate <标题> ／
#       waitwindow <标题> ／ close <标题> ／ listwindows
param([Parameter(Mandatory=$true)][string]$Command,
      [string]$Arg1 = "",
      [string]$Arg2 = "")

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class AccWin32Smoke {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, [MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT lpRect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
    public struct RECT { public int Left, Top, Right, Bottom; }
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    public static System.Collections.Generic.List<string> FindByTitle(string fragment) {
        var result = new System.Collections.Generic.List<string>();
        EnumWindows((hWnd, lParam) => {
            if (!IsWindowVisible(hWnd)) return true;
            var sb = new System.Text.StringBuilder(512);
            GetWindowTextW(hWnd, sb, 512);
            string title = sb.ToString();
            if (title.Length > 0 && (fragment.Length == 0 || title.Contains(fragment)))
                result.Add(hWnd.ToString() + "|" + title);
            return true;
        }, IntPtr.Zero);
        return result;
    }
    public const uint LEFTDOWN = 0x0002, LEFTUP = 0x0004;
}
"@

switch ($Command) {
    "shot" {
        $bounds = [System.Windows.Forms.SystemInformation]::VirtualScreen
        $bmp = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.CopyFromScreen($bounds.X, $bounds.Y, 0, 0, $bmp.Size)
        $g.Dispose()
        if ($Arg2 -ne '') {
            $stripH = [int]$Arg2
            $cropY = $bmp.Height - $stripH
            $crop = $bmp.Clone((New-Object System.Drawing.Rectangle 0, $cropY, $bmp.Width, $stripH), $bmp.PixelFormat)
            $crop.Save($Arg1, [System.Drawing.Imaging.ImageFormat]::Png)
            $crop.Dispose()
        } else {
            $bmp.Save($Arg1, [System.Drawing.Imaging.ImageFormat]::Png)
        }
        $bmp.Dispose()
        Write-Output "SHOT_OK $Arg1"
    }
    "crop" {
        $parts = $Arg2 -split '\s+'
        $src = $Arg1; $dst = [string]$parts[0]
        $x = [int]$parts[1]; $y = [int]$parts[2]; $w = [int]$parts[3]; $h = [int]$parts[4]
        $img = [System.Drawing.Image]::FromFile($src)
        $rect = New-Object System.Drawing.Rectangle $x, $y, $w, $h
        $crop = (New-Object System.Drawing.Bitmap $img).Clone($rect, $img.PixelFormat)
        $crop.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
        $crop.Dispose(); $img.Dispose()
        Write-Output "CROP_OK $dst"
    }
    "click" {
        $x = [int]$Arg1; $y = [int]$Arg2
        [AccWin32Smoke]::SetCursorPos($x, $y) | Out-Null
        Start-Sleep -Milliseconds 120
        [AccWin32Smoke]::mouse_event([AccWin32Smoke]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
        [AccWin32Smoke]::mouse_event([AccWin32Smoke]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
        Write-Output "CLICK_OK $x $y"
    }
    "key" {
        [System.Windows.Forms.SendKeys]::SendWait($Arg1)
        Write-Output "KEY_OK $Arg1"
    }
    "activate" {
        $wins = [AccWin32Smoke]::FindByTitle($Arg1)
        if ($wins.Count -gt 0) {
            $h = [IntPtr]($wins[0].Split('|')[0])
            [AccWin32Smoke]::ShowWindow($h, 9) | Out-Null
            [AccWin32Smoke]::SetForegroundWindow($h) | Out-Null
            Write-Output "ACTIVATE_OK $($wins[0])"
        } else { Write-Output "ACTIVATE_FAIL no-window-matching:$Arg1"; exit 1 }
    }
    "waitwindow" {
        $deadline = (Get-Date).AddSeconds(30)
        while ((Get-Date) -lt $deadline) {
            $wins = [AccWin32Smoke]::FindByTitle($Arg1)
            if ($wins.Count -gt 0) { Write-Output "WINDOW_FOUND $($wins[0])"; exit 0 }
            Start-Sleep -Milliseconds 500
        }
        Write-Output "WINDOW_TIMEOUT $Arg1"; exit 1
    }
    "close" {
        $wins = [AccWin32Smoke]::FindByTitle($Arg1)
        if ($wins.Count -gt 0) {
            $h = [IntPtr]($wins[0].Split('|')[0])
            [AccWin32Smoke]::PostMessageW($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
            Write-Output "CLOSE_SENT $($wins[0])"
        } else { Write-Output "CLOSE_FAIL no-window:$Arg1"; exit 1 }
    }
    "move" {
        # move <标题片段> -Arg2 "x y w h"——窗口移到指定矩形（截图取证定位用）
        $wins = [AccWin32Smoke]::FindByTitle($Arg1)
        if ($wins.Count -gt 0) {
            $h = [IntPtr]($wins[0].Split('|')[0])
            if ([AccWin32Smoke]::IsIconic($h)) { [AccWin32Smoke]::ShowWindow($h, 9) | Out-Null; Start-Sleep -Milliseconds 300 }
            $parts = $Arg2 -split '\s+'
            $ok = [AccWin32Smoke]::MoveWindow($h, [int]$parts[0], [int]$parts[1], [int]$parts[2], [int]$parts[3], $true)
            [AccWin32Smoke]::SetForegroundWindow($h) | Out-Null
            Write-Output "MOVE_OK ok=$ok $($wins[0])"
        } else { Write-Output "MOVE_FAIL no-window:$Arg1"; exit 1 }
    }
    "capture" {
        # capture <标题片段> <输出png>——PrintWindow 直接从窗口句柄渲染取证
        # （PW_RENDERFULLCONTENT=2；不依赖窗口 Z 序/遮挡状态——验收桌面被
        # 用户其他窗口占用时的可靠取证通道）
        $wins = [AccWin32Smoke]::FindByTitle($Arg1)
        if ($wins.Count -eq 0) { Write-Output "CAPTURE_FAIL no-window:$Arg1"; exit 1 }
        $h = [IntPtr]($wins[0].Split('|')[0])
        if ([AccWin32Smoke]::IsIconic($h)) { [AccWin32Smoke]::ShowWindow($h, 9) | Out-Null; Start-Sleep -Milliseconds 500 }
        $r = New-Object AccWin32Smoke+RECT
        [AccWin32Smoke]::GetWindowRect($h, [ref]$r) | Out-Null
        $w = $r.Right - $r.Left; $ht = $r.Bottom - $r.Top
        if ($w -le 0 -or $ht -le 0) { Write-Output "CAPTURE_FAIL bad-rect ${w}x${ht}"; exit 1 }
        $bmp = New-Object System.Drawing.Bitmap $w, $ht
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        $ok = [AccWin32Smoke]::PrintWindow($h, $hdc, 2)
        $g.ReleaseHdc($hdc); $g.Dispose()
        $bmp.Save($Arg2, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Output "CAPTURE_OK ok=$ok rect=$($r.Left),$($r.Top),$($r.Right),$($r.Bottom) -> $Arg2"
    }
    "listwindows" {
        [AccWin32Smoke]::FindByTitle("") | ForEach-Object { Write-Output $_ }
    }
    default { Write-Output "UNKNOWN_COMMAND $Command"; exit 2 }
}
