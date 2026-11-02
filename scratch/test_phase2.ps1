Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

# Kill previous instances
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class Win32HelperP2 {
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

$hWnd = [Win32HelperP2]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperP2]::PrintWindow($hWnd, $hdc, 0)
        $g.ReleaseHdc($hdc)
        $g.Dispose()
        $outPath = Join-Path $artifactDir $name
        $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host "Captured: $outPath"
    } catch {
        Write-Host "Capture failed: $_"
    }
}

function Send-Key($vk, $delayMs = 250) {
    $WM_KEYDOWN = 0x0100
    $WM_KEYUP = 0x0101
    [Win32HelperP2]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperP2]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

# 1. Home screen initial
Capture-Screen "p2_01_home_screen.png"

# Navigate to LLCubic on Home screen (Down 2 times, Right 2 times)
Send-Key 0x28 200
Send-Key 0x28 200
Send-Key 0x27 200
Send-Key 0x27 300

# 2. Launch LLCubic by pressing Enter (0x0D)
Send-Key 0x0D 600

# Switch to Plot graph tab: Up arrow to tab header, Right arrow to select Plot tab, Enter to activate
Send-Key 0x26 200
Send-Key 0x27 200
Send-Key 0x0D 600
Capture-Screen "p2_02_surface_initial.png"

# 3. Rotate view using Right arrow (0x27) 4 times
for ($i = 0; $i -lt 4; $i++) {
    Send-Key 0x27 150
}
Capture-Screen "p2_03_surface_rotated_right.png"

# Rotate view using Up arrow (0x26) 3 times
for ($i = 0; $i -lt 3; $i++) {
    Send-Key 0x26 150
}
Capture-Screen "p2_04_surface_rotated_up.png"

# 4. Toggle render mode with Space (0x20)
Send-Key 0x20 400
Capture-Screen "p2_05_wireframe_mode.png"

# Toggle back to Surface mode with Space (0x20)
Send-Key 0x20 400
Capture-Screen "p2_06_surface_mode_back.png"

# 5. Preset view: Press 'Q' or '1' (0x31) for X-axis view
Send-Key 0x31 300
Capture-Screen "p2_07_surface_xaxis_view.png"

# Preset view: Press 'E' or '3' (0x33) for Z-axis top view
Send-Key 0x33 300
Capture-Screen "p2_08_surface_zaxis_topview.png"

# Reset view: Press Enter (0x0D)
Send-Key 0x0D 300
Capture-Screen "p2_09_surface_reset_view.png"

# Close process cleanly
Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "Test phase 2 completed."
