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

public class Win32HelperRev {
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

$hWnd = [Win32HelperRev]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperRev]::PrintWindow($hWnd, $hdc, 0)
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
    [Win32HelperRev]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperRev]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

# 1. Navigate from Home to LLCubic (Down 2, Right 2)
Send-Key 0x28 200 # Down
Send-Key 0x28 200 # Down
Send-Key 0x27 200 # Right
Send-Key 0x27 300 # Right

# 2. Enter LLCubic
Send-Key 0x0D 700

# 3. Add Revolution: Vase: select "+ Add an element" (Down, Enter) -> key '5' (0x35)
Send-Key 0x28 350
Send-Key 0x0D 600
Send-Key 0x35 600
Capture-Screen "rev_01_vase_in_expressions.png"

# 4. Switch to 3D Plot tab (Right, Down)
Send-Key 0x27 400
Send-Key 0x28 600
Capture-Screen "rev_02_vase_3d_view.png"

# 5. Return to Expressions using F1 (Toolbox: 0x70)
Send-Key 0x70 600

# 6. Add Parametric: Torus: select "+ Add an element" (Down, Enter) -> key '6' (0x36)
Send-Key 0x28 350
Send-Key 0x0D 600
Send-Key 0x36 600
Capture-Screen "rev_03_torus_added.png"

# 7. Switch to 3D Plot tab (Right, Down)
Send-Key 0x27 400
Send-Key 0x28 600
Capture-Screen "rev_04_vase_and_torus_3d.png"

# 8. Slicer on Torus: Press '7' (0x37) for Z-axis slicer
Send-Key 0x37 600

# 9. Enter 2D Cross Section view (Enter: 0x0D) -> Torus cut in half (shows 2 rings!)
Send-Key 0x0D 700
Capture-Screen "rev_05_torus_2d_section.png"

# 10. Return to 3D View (Enter) and turn off Slicer (7 x 3)
Send-Key 0x0D 500
Send-Key 0x37 300
Send-Key 0x37 300
Send-Key 0x37 400

# 11. Return to Expressions using F1 (0x70)
Send-Key 0x70 600

# 12. Open Torus Details (Down to Row 1, Enter)
Send-Key 0x28 300
Send-Key 0x0D 600
# Change Color to Magenta (Right x 2)
Send-Key 0x27 200
Send-Key 0x27 300
# Down to Major R (Row 2), increase R by Right x 4 -> R = 1.90
Send-Key 0x28 200
Send-Key 0x27 150
Send-Key 0x27 150
Send-Key 0x27 150
Send-Key 0x27 300
Capture-Screen "rev_06_torus_details_edited.png"

# 13. Return to Expressions (Back), then switch to 3D Plot (Right, Down)
Send-Key 0x1B 500
Send-Key 0x27 400
Send-Key 0x28 600
Capture-Screen "rev_07_torus_magenta_3d.png"

# Clean close
Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "Revolution and Parametric verification completed successfully."
