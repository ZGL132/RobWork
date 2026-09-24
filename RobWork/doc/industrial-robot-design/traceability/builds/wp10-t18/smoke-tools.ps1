# UI-T18 宿主 GUI 冒烟取证工具（wp10-t18 返工 -r4 轮；沿 UI-T17 r3/r4 工具族形态）。
# 用法（pwsh -File smoke-tools.ps1 <command> ...）：
#   shot <输出png> [裁剪底边像素高度]   —— 全屏截图；第二参给"仅底部条带"高度（状态栏特写用）
#   click <x> <y>                       —— 真实鼠标左键单击（SetCursorPos+mouse_event）
#   key <文本|特殊键>                   —— SendKeys 发送（如 "{F1}" "^+p" "abc"）
#   activate <窗口标题片段>             —— 前台激活含指定标题片段的窗口
#   waitwindow <标题片段> <超时秒>      —— 轮询等待窗口出现（退出码 0=找到）
#   close <标题片段>                    —— 发送 WM_CLOSE（优雅退出——F-325 教训：禁用强杀）
# 全部动作用 Add-Type 内联 Win32，无外部依赖。
param([Parameter(Mandatory=$true)][string]$Command,
      [Parameter(Position=1)][string]$Arg1,
      [Parameter(Position=2)][string]$Arg2)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win32Smoke {
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, uint dx, uint dy, uint dwData, UIntPtr dwExtraInfo);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern IntPtr FindWindowW(string lpClassName, string lpWindowName);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr hWnd, [MarshalAs(UnmanagedType.LPWStr)] System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc lpEnumFunc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    // 枚举顶层可见窗口，返回"句柄|标题"列表（标题含给定片段者；片段为空=全部）。
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
        if ($PSBoundParameters.ContainsKey('Arg2') -and $Arg2 -ne '') {
            # 底部条带裁剪（状态栏/底 Dock 特写——arg2=条带高度像素）
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
        # crop <src.png> <dst.png> <x> <y> <w> <h> —— 矩形裁剪（Arg1=src dst x，Arg2=y w h 空格分字符串）
        $parts = $Arg1 -split '\s+'
        $src = [string]$parts[0]; $dst = [string]$parts[1]
        $x = [int]$parts[2]; $y = [int]$parts[3]; $w = [int]$parts[4]; $h = [int]$parts[5]
        $img = [System.Drawing.Image]::FromFile($src)
        $rect = New-Object System.Drawing.Rectangle $x, $y, $w, $h
        $crop = (New-Object System.Drawing.Bitmap $img).Clone($rect, $img.PixelFormat)
        $crop.Save($dst, [System.Drawing.Imaging.ImageFormat]::Png)
        $crop.Dispose(); $img.Dispose()
        Write-Output "CROP_OK $dst"
    }
    "click" {
        $x = [int]$Arg1; $y = [int]$Arg2
        [Win32Smoke]::SetCursorPos($x, $y) | Out-Null
        Start-Sleep -Milliseconds 120
        [Win32Smoke]::mouse_event([Win32Smoke]::LEFTDOWN, 0, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 60
        [Win32Smoke]::mouse_event([Win32Smoke]::LEFTUP, 0, 0, 0, [UIntPtr]::Zero)
        Write-Output "CLICK_OK $x $y"
    }
    "key" {
        [System.Windows.Forms.SendKeys]::SendWait($Arg1)
        Write-Output "KEY_OK $Arg1"
    }
    "activate" {
        $wins = [Win32Smoke]::FindByTitle($Arg1)
        if ($wins.Count -gt 0) {
            $h = [IntPtr]($wins[0].Split('|')[0])
            [Win32Smoke]::ShowWindow($h, 9) | Out-Null   # SW_RESTORE
            [Win32Smoke]::SetForegroundWindow($h) | Out-Null
            Write-Output "ACTIVATE_OK $($wins[0])"
        } else { Write-Output "ACTIVATE_FAIL no-window-matching:$Arg1"; exit 1 }
    }
    "waitwindow" {
        $deadline = (Get-Date).AddSeconds([int]$Arg2)
        while ((Get-Date) -lt $deadline) {
            $wins = [Win32Smoke]::FindByTitle($Arg1)
            if ($wins.Count -gt 0) { Write-Output "WINDOW_FOUND $($wins[0])"; exit 0 }
            Start-Sleep -Milliseconds 500
        }
        Write-Output "WINDOW_TIMEOUT $Arg1"; exit 1
    }
    "close" {
        $wins = [Win32Smoke]::FindByTitle($Arg1)
        if ($wins.Count -gt 0) {
            $h = [IntPtr]($wins[0].Split('|')[0])
            [Win32Smoke]::PostMessageW($h, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null  # WM_CLOSE
            Write-Output "CLOSE_SENT $($wins[0])"
        } else { Write-Output "CLOSE_FAIL no-window:$Arg1"; exit 1 }
    }
    "listwindows" {
        [Win32Smoke]::FindByTitle("") | ForEach-Object { Write-Output $_ }
    }
    default { Write-Output "UNKNOWN_COMMAND $Command"; exit 2 }
}
