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

public class Win32HelperP3C {
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

$hWnd = [Win32HelperP3C]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperP3C]::PrintWindow($hWnd, $hdc, 0)
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
    [Win32HelperP3C]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperP3C]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

# 1. Home screen -> navigate to LLCubic (Down 2, Right 2)
Send-Key 0x28 200
Send-Key 0x28 200
Send-Key 0x27 200
Send-Key 0x27 300

# 2. Enter LLCubic (default tab is Functions)
Send-Key 0x0D 700
Capture-Screen "p3_01_functions_initial.png"

# 3. Down arrow to select row 1: "+ Add an element"
Send-Key 0x28 350

# 4. Open Type Selection Menu with Enter
Send-Key 0x0D 600
Capture-Screen "p3_02_type_menu.png"

# 5. Press '3' (0x33) to add Cone (Double Sheet) via numeric shortcut!
Send-Key 0x33 600
Capture-Screen "p3_03_cone_added.png"

# 6. Select "+ Add an element" again: cursor is at row 1 (Cone), press Down to reach row 2 ("+ Add an element")
Send-Key 0x28 350
Send-Key 0x0D 600

# Press '4' (0x34) to add Plane (z = 0.6x - 0.4y) via numeric shortcut!
Send-Key 0x34 600
Capture-Screen "p3_04_cone_and_plane_added.png"

# 7. Uncheck/disable the 1st element (z1 = sin(x)*cos(y))
# Go to top row (Up twice from row 2)
Send-Key 0x26 250
Send-Key 0x26 250
# Press Space (0x20) to toggle checkbox off
Send-Key 0x20 400
Capture-Screen "p3_05_list_configured.png"

# 8. Switch to 3D Plot tab using Right arrow (0x27) at row 0 (or F1: 0x70)
Send-Key 0x27 800
Capture-Screen "p3_06_multi_plot_cone_plane.png"

# 9. Rotate view: Right 4 times, Up 3 times
for ($i = 0; $i -lt 4; $i++) {
    Send-Key 0x27 150
}
for ($i = 0; $i -lt 3; $i++) {
    Send-Key 0x26 150
}
Capture-Screen "p3_07_multi_plot_rotated.png"

# 10. Switch to wireframe mode (Space)
Send-Key 0x20 400
Capture-Screen "p3_08_multi_plot_wireframe.png"

# 11. Switch back to surface mode (Space) and reset view (Enter)
Send-Key 0x20 400
Send-Key 0x0D 400
Capture-Screen "p3_09_multi_plot_reset.png"

# 12. Switch back to Functions tab using F1 (0x70)
Send-Key 0x70 600
Capture-Screen "p3_10_back_to_list.png"

# Clean close
Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "Test phase 3 completed successfully."
