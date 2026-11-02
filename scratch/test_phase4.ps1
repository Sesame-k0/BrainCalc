Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

# Kill previous instances
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

# Remove any old saved state to ensure clean test
if (Test-Path "..\brain_state.bin") {
    Remove-Item "..\brain_state.bin" -Force -ErrorAction SilentlyContinue
}

$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class Win32HelperP4 {
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

Add-Type -TypeDefinition $typeDef -ErrorAction SilentlyContinue

$hWnd = [Win32HelperP4]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperP4]::PrintWindow($hWnd, $hdc, 0)
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
    [Win32HelperP4]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperP4]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

# 1. Home screen -> navigate to LLCubic (Down 2, Right 2)
Send-Key 0x28 200
Send-Key 0x28 200
Send-Key 0x27 200
Send-Key 0x27 300

# 2. Enter LLCubic (default tab is Functions)
Send-Key 0x0D 700

# 3. Down arrow to select row 1: "+ Add an element"
Send-Key 0x28 350

# 4. Open Type Selection Menu with Enter
Send-Key 0x0D 600

# 5. Press '3' (0x33) to add Cone (Double Sheet)
Send-Key 0x33 600

# 6. Select "+ Add an element" again and add Plane ('4')
Send-Key 0x28 350
Send-Key 0x0D 600
Send-Key 0x34 600

# 7. Disable element 0 (z1 = sin(x)*cos(y)) so we focus on Cone + Plane
Send-Key 0x26 250
Send-Key 0x26 250
Send-Key 0x20 400

# 8. Switch to 3D Plot tab using Right arrow (0x27) at top row
Send-Key 0x27 800
Capture-Screen "p4_01_plot_initial.png"

# 9. Press '6' (0x36) to enter Trace Mode!
Send-Key 0x36 600
Capture-Screen "p4_02_trace_initial.png"

# 10. Move cursor along u-direction (Right arrow 4 times)
for ($i = 0; $i -lt 4; $i++) {
    Send-Key 0x27 200
}
Capture-Screen "p4_03_trace_move_u.png"

# 11. Move cursor along v-direction (Up arrow 4 times)
for ($i = 0; $i -lt 4; $i++) {
    Send-Key 0x26 200
}
Capture-Screen "p4_04_trace_move_v.png"

# 12. Press Enter (0x0D) to switch trace target to next object (Plane)
Send-Key 0x0D 600
Capture-Screen "p4_05_trace_switch_to_plane.png"

# 13. Move cursor on the plane (Right arrow 3 times)
for ($i = 0; $i -lt 3; $i++) {
    Send-Key 0x27 200
}
Capture-Screen "p4_06_trace_plane_move.png"

# 14. Press Enter (0x0D) to switch back to Cone
Send-Key 0x0D 600
Capture-Screen "p4_07_trace_switch_next.png"

# 15. Press Back (VK_ESCAPE: 0x1B) to exit Trace Mode
Send-Key 0x1B 600
Capture-Screen "p4_08_trace_exit.png"

# 16. Verify rotation is restored: Right 4 times, Up 3 times
for ($i = 0; $i -lt 4; $i++) {
    Send-Key 0x27 150
}
for ($i = 0; $i -lt 3; $i++) {
    Send-Key 0x26 150
}
Capture-Screen "p4_09_rotate_after_trace.png"

# Clean close
Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "Test phase 4 completed successfully."
