Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;
using System.Drawing;

public class DirectTimerV3 {
    public delegate bool EnumThreadDelegate(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumThreadWindows(int dwThreadId, EnumThreadDelegate lpfn, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "GetClassNameW", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

    [DllImport("user32.dll")]
    public static extern bool GetClientRect(IntPtr hWnd, out RECT lpRect);

    [DllImport("user32.dll")]
    public static extern IntPtr GetDC(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern int ReleaseDC(IntPtr hWnd, IntPtr hDC);

    [DllImport("gdi32.dll")]
    public static extern IntPtr CreateCompatibleDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    public static extern IntPtr CreateCompatibleBitmap(IntPtr hdc, int nWidth, int nHeight);

    [DllImport("gdi32.dll")]
    public static extern IntPtr SelectObject(IntPtr hdc, IntPtr hgdiobj);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteDC(IntPtr hdc);

    [DllImport("gdi32.dll")]
    public static extern bool DeleteObject(IntPtr hObject);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public static IntPtr FindLiliumHwnd(int processId) {
        IntPtr found = IntPtr.Zero;
        var proc = System.Diagnostics.Process.GetProcessById(processId);
        foreach (System.Diagnostics.ProcessThread t in proc.Threads) {
            EnumThreadWindows(t.Id, (hWnd, lParam) => {
                var sb = new StringBuilder(256);
                GetClassNameW(hWnd, sb, 256);
                if (sb.ToString() == "BrainCalcWinCEClass") {
                    found = hWnd;
                    return false;
                }
                return true;
            }, IntPtr.Zero);
            if (found != IntPtr.Zero) break;
        }
        return found;
    }

    public static Bitmap Capture(IntPtr hWnd) {
        RECT rect;
        GetClientRect(hWnd, out rect);
        int w = rect.Right - rect.Left;
        int h = rect.Bottom - rect.Top;
        if (w <= 0 || h <= 0) { w = 800; h = 480; }

        IntPtr hdcScreen = GetDC(hWnd);
        IntPtr hdcMem = CreateCompatibleDC(hdcScreen);
        IntPtr hBmp = CreateCompatibleBitmap(hdcScreen, w, h);
        IntPtr hOld = SelectObject(hdcMem, hBmp);

        PrintWindow(hWnd, hdcMem, 0);

        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
        ReleaseDC(hWnd, hdcScreen);

        Bitmap bmp = Image.FromHbitmap(hBmp);
        DeleteObject(hBmp);
        return bmp;
    }
}
"@

Add-Type -TypeDefinition $typeDef -ReferencedAssemblies System.Drawing, System.Windows.Forms

$hWnd = [DirectTimerV3]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium window handle: $hWnd"
[DirectTimerV3]::SetForegroundWindow($hWnd)
Start-Sleep -Milliseconds 600

function Key($vk) {
    [DirectTimerV3]::PostMessage($hWnd, 0x100, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 100
    [DirectTimerV3]::PostMessage($hWnd, 0x101, [IntPtr]$vk, [IntPtr]0xC0000001) | Out-Null
    Start-Sleep -Milliseconds 400
}

function Capture($filename) {
    $bmp = [DirectTimerV3]::Capture($hWnd)
    if ($bmp -ne $null) {
        $outPath = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346\$filename"
        $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host "Saved: $filename"
    }
}

# 1. ホーム画面のキャプチャ（左上タイトルバーの sym/deg/0% 表示を確認）
Start-Sleep -Seconds 1
Capture "v3_01_home_screen.png"

# 2. ホーム画面から Timer へ移動（Row 2, Col 1）
Key 0x28 # Down -> Statistics
Start-Sleep -Milliseconds 400
Key 0x28 # Down -> Atomic
Start-Sleep -Milliseconds 400
Key 0x27 # Right -> Timer
Start-Sleep -Milliseconds 600

# ホーム画面で Timer 選択中（右側 Lilium サイドバーの日本語解説表示を確認）
Capture "v3_02_home_timer_selected.png"

# 3. Enter で Timer アプリ起動
Key 0x0D # Enter
Start-Sleep -Seconds 1
Capture "v3_03_timer_initial.png"

# 4. Enter で Stopwatch 開始！
Key 0x0D # OK (Start)
Start-Sleep -Seconds 2
Capture "v3_04_timer_stopwatch_running.png"

# 5. メモ帳にキー入力 (1, 2, 3, 4, 5)
Key 0x31 # 1
Start-Sleep -Milliseconds 200
Key 0x32 # 2
Start-Sleep -Milliseconds 200
Key 0x33 # 3
Start-Sleep -Milliseconds 200
Key 0x34 # 4
Start-Sleep -Milliseconds 200
Key 0x35 # 5
Start-Sleep -Milliseconds 600
Capture "v3_05_timer_stopwatch_notes.png"

# 6. 一時停止 (OK)
Key 0x0D # OK (Pause)
Start-Sleep -Milliseconds 500

# 7. Right で Countdown Timer モードへ
Key 0x27 # Right
Start-Sleep -Milliseconds 600
Capture "v3_06_timer_countdown_mode.png"

# 8. Right で Pomodoro モードへ
Key 0x27 # Right
Start-Sleep -Milliseconds 600
Capture "v3_07_timer_pomodoro_mode.png"

# 9. ポモドーロ開始
Key 0x0D # OK (Start)
Start-Sleep -Seconds 2
Capture "v3_08_timer_pomodoro_running.png"

# 10. ホーム画面へ戻ってバックグラウンド稼働確認
Key 0x79 # F10 (HOME)
Start-Sleep -Seconds 1
Capture "v3_09_home_bg_running.png"

Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "V3 Verification completed successfully!"
