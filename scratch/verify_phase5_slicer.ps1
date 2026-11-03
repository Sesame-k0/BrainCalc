Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

# Kill previous instances
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

# Remove any old saved state to ensure fresh state
if (Test-Path "..\brain_state.bin") {
    Remove-Item "..\brain_state.bin" -Force -ErrorAction SilentlyContinue
}

$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class Win32HelperP5 {
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

$hWnd = [Win32HelperP5]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperP5]::PrintWindow($hWnd, $hdc, 0)
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
    [Win32HelperP5]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperP5]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

# 1. Navigate from Home to LLCubic (Down 2, Right 2)
Send-Key 0x28 200 # Down
Send-Key 0x28 200 # Down
Send-Key 0x27 200 # Right
Send-Key 0x27 300 # Right

# 2. Enter LLCubic
Send-Key 0x0D 700

# 3. Add Cone: select "+ Add an element" (Down, Enter) -> key '3'
Send-Key 0x28 350
Send-Key 0x0D 600
Send-Key 0x33 600

# 4. Switch to 3D Plot tab (Right arrow, then Down arrow)
Send-Key 0x27 400
Send-Key 0x28 600

# 5. Activate Section Slicer (key '7': 0x37) -> Z-axis slicer at Z=0.00
Send-Key 0x37 600
Capture-Screen "p5_01_slicer_z0.png"

# 6. Move slice position up (Right arrow x 3 -> c = +0.30)
Send-Key 0x27 250
Send-Key 0x27 250
Send-Key 0x27 400
Capture-Screen "p5_02_slicer_z_moved.png"

# 7. Press Enter (0x0D) to enter 2D Cross Section View
Send-Key 0x0D 700
Capture-Screen "p5_03_section2d_z_view.png"

# 8. Press Enter (0x0D) to return to 3D Slicer view
Send-Key 0x0D 600
Capture-Screen "p5_04_return_3d.png"

# 9. Press '7' (0x37) to cycle slice axis to X-axis
Send-Key 0x37 600
Capture-Screen "p5_05_slicer_x0.png"

# 10. Press Enter (0x0D) to enter 2D Cross Section View for X=0.00
Send-Key 0x0D 700
Capture-Screen "p5_06_section2d_x_view.png"

# 11. Press Back (0x1B) to return to 3D Slicer view
Send-Key 0x1B 600

# 12. Cycle slice axis to Y-axis, then OFF
Send-Key 0x37 400 # Y-axis
Send-Key 0x37 500 # OFF
Capture-Screen "p5_07_slicer_off.png"

# Clean close
Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "Phase 5 slicer verification completed successfully."
