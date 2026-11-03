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

public class Win32HelperRange2 {
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

$hWnd = [Win32HelperRange2]::FindLiliumHwnd($proc.Id)
if ($hWnd -eq [IntPtr]::Zero) {
    Write-Host "Failed to find BrainCalc window!"
    Stop-Process -Id $proc.Id -Force
    Pop-Location
    exit 1
}

function Send-Key($vk, $delayMs = 250) {
    $WM_KEYDOWN = 0x0100
    $WM_KEYUP = 0x0101
    [Win32HelperRange2]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperRange2]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

function Capture-Screen($fileName) {
    $bmp = New-Object System.Drawing.Bitmap(480, 272)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [Win32HelperRange2]::PrintWindow($hWnd, $hdc, 0)
    $g.ReleaseHdc($hdc)
    $g.Dispose()

    $artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"
    $savePath = Join-Path $artifactDir $fileName
    $bmp.Save($savePath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "Saved screenshot: $savePath"
}

Write-Host "1. Wait for Home screen and navigate to LLCubic..."
Start-Sleep -Milliseconds 1500

# Home menu: Down 2, Right 2, Enter -> Opens LLCubic (Plot3D)
Send-Key 0x28 250 # Down
Send-Key 0x28 250 # Down
Send-Key 0x27 250 # Right
Send-Key 0x27 300 # Right
Send-Key 0x0D 800 # Enter

# 2. Select "+ Add an element": Cursor is on Row 0 (+ Add an element)
Write-Host "2. Open TypeMenu and select 7: z = f(x, y) [x^2 + y^2]..."
Send-Key 0x0D 600

# Press 7 (0x37) to select 7: z = f(x, y) [x^2 + y^2]
Send-Key 0x37 600
Capture-Screen "range_01_explicit_added_in_list.png"

# 3. Switch to 3D Plot tab (Right arrow)
Write-Host "3. Jump to 3D Plot tab to view z = x^2 + y^2..."
Send-Key 0x27 900
Capture-Screen "range_02_paraboloid_3d_standard_range.png"

# 4. Open Axes Range settings (press '5' 0x35)
Write-Host "4. Press 5 to open Axes Range Settings..."
Send-Key 0x35 600
Capture-Screen "range_03_axes_settings_opened.png"

# 5. Change preset to Paraboloid Z[0, 10]: on Row 0, press Right 3 times
Write-Host "5. Change preset to Paraboloid Z[0, 10]..."
Send-Key 0x27 300
Send-Key 0x27 300
Send-Key 0x27 500
Capture-Screen "range_04_preset_paraboloid_selected.png"

# 6. Back to 3D Plot with updated range (Back: 0x1B / 0x08)
Write-Host "6. Return to 3D Plot with updated range..."
Send-Key 0x1B 900
Capture-Screen "range_05_paraboloid_3d_tall_range.png"

# 7. Rotate view (Left 2, Up 1)
Send-Key 0x25 200
Send-Key 0x25 200
Send-Key 0x26 500
Capture-Screen "range_06_paraboloid_rotated.png"

# 8. Return to Expressions list (F1: 0x70)
Write-Host "7. Return to Expressions list..."
Send-Key 0x70 600

# 9. Open parameter editor for z = x^2 + y^2 (Row 0 is selected, press Enter)
Send-Key 0x0D 600
Capture-Screen "range_07_details_formula_editor.png"

# 10. Row 1 is Preset: move Down, then Right to switch to Saddle (x^2 - y^2)
Send-Key 0x28 300
Send-Key 0x27 500
Capture-Screen "range_08_formula_switched_to_saddle.png"

# 11. Back to Expressions, then jump to 3D Plot (Back: 0x1B, Right: 0x27)
Send-Key 0x1B 600
Send-Key 0x27 900
Capture-Screen "range_09_saddle_3d_view.png"

# 12. Start coordinate trace on Saddle (press '6' 0x36)
Write-Host "8. Start coordinate trace on Saddle surface..."
Send-Key 0x36 600
Capture-Screen "range_10_trace_on_saddle.png"

Write-Host "Verification test completed successfully!"

Stop-Process -Id $proc.Id -Force
Pop-Location
