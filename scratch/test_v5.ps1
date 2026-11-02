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

public class Win32HelperV5 {
    public delegate bool EnumThreadDelegate(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumThreadWindows(int dwThreadId, EnumThreadDelegate lpfn, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "GetClassNameW", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll")]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);

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
}
"@

Add-Type -TypeDefinition $typeDef

$hWnd = [Win32HelperV5]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 600

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperV5]::PrintWindow($hWnd, $hdc, 0) | Out-Null
        $g.ReleaseHdc($hdc)
        $g.Dispose()
        $outPath = Join-Path $artifactDir $name
        $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host "Saved: $name"
    } catch {
        Write-Host "Capture error on $name : $_"
    }
}

function Send-Key($vk, $label) {
    [Win32HelperV5]::PostMessage($hWnd, 0x100, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 100
    [Win32HelperV5]::PostMessage($hWnd, 0x101, [IntPtr]$vk, [IntPtr]0xC0000001) | Out-Null
    Write-Host "Sent key: $label ($vk)"
    Start-Sleep -Milliseconds 500
}

# 1. Capture initial Home screen
Capture-Screen "v5_01_home_screen.png"

# 2. Navigate to Timer (Row 2, Col 1)
Send-Key 0x28 "Down 1 (Statistics)"
Send-Key 0x28 "Down 2 (Atomic)"
Send-Key 0x27 "Right 1 (Timer)"
Start-Sleep -Milliseconds 600

# Capture Home with Timer selected (Check sidebar Japanese help text)
Capture-Screen "v5_02_home_timer_selected.png"

# 3. Enter into Timer app
Send-Key 0x0D "Enter (Launch Timer)"
Start-Sleep -Seconds 1
Capture-Screen "v5_03_timer_initial.png"

# 4. Start Stopwatch (OK)
Send-Key 0x0D "OK (Start Stopwatch)"
Start-Sleep -Seconds 3
Capture-Screen "v5_04_stopwatch_running.png"

# 5. Type some notes using numbers (1, 2, 3, 4, 5)
Send-Key 0x31 "1"
Send-Key 0x32 "2"
Send-Key 0x33 "3"
Send-Key 0x34 "4"
Send-Key 0x35 "5"
Start-Sleep -Milliseconds 500
Capture-Screen "v5_05_timer_with_notes.png"

# 6. Pause Stopwatch
Send-Key 0x0D "OK (Pause Stopwatch)"
Start-Sleep -Milliseconds 500

# 7. Switch to Countdown Timer mode (Right)
Send-Key 0x27 "Right (Countdown Mode)"
Start-Sleep -Milliseconds 500
Capture-Screen "v5_06_timer_countdown_mode.png"

# 8. Switch to Pomodoro mode (Right)
Send-Key 0x27 "Right (Pomodoro Mode)"
Start-Sleep -Milliseconds 500
Capture-Screen "v5_07_timer_pomodoro_mode.png"

# 9. Start Pomodoro
Send-Key 0x0D "OK (Start Pomodoro)"
Start-Sleep -Seconds 3
Capture-Screen "v5_08_timer_pomodoro_running.png"

# 10. Press HOME (F10 = 0x79) to return to Home screen (test background timer)
Send-Key 0x79 "HOME (F10)"
Start-Sleep -Seconds 1
Capture-Screen "v5_09_home_bg_running.png"

Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "V5 Suite finished successfully!"
