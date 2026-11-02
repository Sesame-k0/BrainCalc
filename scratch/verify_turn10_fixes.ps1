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

public class Win32HelperT10Final {
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

$hWnd = [Win32HelperT10Final]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperT10Final]::PrintWindow($hWnd, $hdc, 0)
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
    [Win32HelperT10Final]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperT10Final]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

# 1. Navigate from Home to LLCubic (Down 2, Right 2)
Send-Key 0x28 200 # Down
Send-Key 0x28 200 # Down
Send-Key 0x27 200 # Right
Send-Key 0x27 300 # Right

# 2. Enter LLCubic
Send-Key 0x0D 700

# Capture 1: Expressions tab in initial state (should show "Expressions" and "3D式・立体一覧" help)
Capture-Screen "final_01_expressions_empty.png"

# 3. Switch to 3D Plot tab (Right arrow, then Down arrow to focus PlotView)
Send-Key 0x27 400
Send-Key 0x28 600
Capture-Screen "final_02_empty_plot3d_view.png"

# 4. Return to Expressions tab using F1 (0x70)
Send-Key 0x70 600

# 5. Add Cone: select "+ Add an element" (Down, Enter) -> key '3'
Send-Key 0x28 350
Send-Key 0x0D 600
Send-Key 0x33 600
Capture-Screen "final_03_cone_added.png"

# 6. Switch to 3D Plot tab (Right arrow, then Down arrow)
Send-Key 0x27 400
Send-Key 0x28 600
Capture-Screen "final_04_cone_plot_with_help.png"

# 7. Press '6' (0x36) to activate Trace Mode
Send-Key 0x36 600
Capture-Screen "final_05_trace_active_hud.png"

# 8. Move trace cursor along surface
Send-Key 0x27 200 # Right
Send-Key 0x27 200 # Right
Send-Key 0x26 200 # Up
Send-Key 0x26 200 # Up
Capture-Screen "final_06_trace_moved.png"

# 9. Press Back (ESC: 0x1B) to exit trace mode
Send-Key 0x1B 600
Capture-Screen "final_07_trace_exited.png"

# Clean close
Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
Write-Host "Final verification completed successfully."
