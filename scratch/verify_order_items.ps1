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

public class Win32HelperVerify {
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

$hWnd = [Win32HelperVerify]::FindLiliumHwnd($proc.Id)
if ($hWnd -eq [IntPtr]::Zero) {
    Write-Host "Failed to find BrainCalc window!"
    Stop-Process -Id $proc.Id -Force
    Pop-Location
    exit 1
}

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"

function Capture-Screen($fileName) {
    $bmp = New-Object System.Drawing.Bitmap(800, 480)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [Win32HelperVerify]::PrintWindow($hWnd, $hdc, 0)
    $g.ReleaseHdc($hdc)
    $g.Dispose()

    $savePath = Join-Path $artifactDir $fileName
    $bmp.Save($savePath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "Saved screenshot: $savePath"
}

function Send-Key($vk, $delayMs = 250) {
    $WM_KEYDOWN = 0x0100
    $WM_KEYUP = 0x0101
    [Win32HelperVerify]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 60
    [Win32HelperVerify]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds $delayMs
}

Write-Host "=== TEST PHASE 1: Settings App & LLCubic Guide ==="
Start-Sleep -Milliseconds 1500
Capture-Screen "step01_home_screen.png"

# Move Down 4 times to Settings app
Send-Key 0x28 200 # Down (Row 1: statistics)
Send-Key 0x28 200 # Down (Row 2: atomic)
Send-Key 0x28 200 # Down (Row 3: sequence)
Send-Key 0x28 300 # Down (Row 4: settings)
Capture-Screen "step02_home_settings_selected.png"

# Enter Settings app
Send-Key 0x0D 800
Capture-Screen "step03_settings_menu_initial.png"

# Move down in Settings menu to LLCubic Guide
# In main_controller_prompt_none.cpp:
# s_modelMenu order: ExternalApps, Accessibility, LLCubicGuide, About
# ExternalApps is row 6/7, Accessibility is row 7/8, LLCubicGuide is row 8/9
# Let's send Down arrows to navigate through Settings
for ($i = 0; $i -lt 12; $i++) {
    Send-Key 0x28 150
}
Capture-Screen "step04_settings_llcubic_guide_focused.png"

# Enter LLCubic Guide
Send-Key 0x0D 800
Capture-Screen "step05_llcubic_guide_page1.png"

# Scroll down inside LLCubic Guide (Down arrow 6 times)
for ($i = 0; $i -lt 6; $i++) {
    Send-Key 0x28 150
}
Capture-Screen "step06_llcubic_guide_page2.png"

# Return back to Settings menu (ESC / Back)
Send-Key 0x1B 600
Capture-Screen "step07_settings_after_back.png"

# Return to Home screen (Home key 0x24)
Send-Key 0x24 800
Capture-Screen "step08_home_after_guide.png"

Write-Host "=== TEST PHASE 2: Plot3D (LLCubic) Parametric & Explicit Input ==="
# Navigate to LLCubic (Plot3D):
# From Home top: Down 2, Right 2
# Let's first move to (0,0) by Up 4, Left 2
Send-Key 0x26 200
Send-Key 0x26 200
Send-Key 0x26 200
Send-Key 0x26 200
Send-Key 0x25 200
Send-Key 0x25 200

# Now (0,0) -> Down 2, Right 2 to select plot3d
Send-Key 0x28 250
Send-Key 0x28 250
Send-Key 0x27 250
Send-Key 0x27 300
Capture-Screen "step09_home_plot3d_selected.png"

# Enter Plot3D
Send-Key 0x0D 800
Capture-Screen "step10_plot3d_clean_expressions.png"

# Open Type Menu (Enter on Row 0 "+ Add an element")
Send-Key 0x0D 600
Capture-Screen "step11_typemenu_row0_explicit.png"

# Down to Row 1: Parametric
Send-Key 0x28 500
Capture-Screen "step12_typemenu_row1_parametric.png"

# Down to Row 2: Sphere
Send-Key 0x28 300
Capture-Screen "step13_typemenu_row2_sphere.png"

# Back up to Row 1: Parametric and select it with Enter
Send-Key 0x26 400
Send-Key 0x0D 800
Capture-Screen "step14_parametric_added_in_list.png"

# Enter Details screen (Enter on Parametric item)
Send-Key 0x0D 800
Capture-Screen "step15_parametric_details_row0_color.png"

# Down to Row 1: X(u,v) -> Check _ cursor on Row 1!
Send-Key 0x28 400
Capture-Screen "step16_parametric_details_row1_X_cursor.png"

# Down to Row 2: Y(u,v) -> Check _ cursor moves to Row 2!
Send-Key 0x28 400
Capture-Screen "step17_parametric_details_row2_Y_cursor.png"

# Down to Row 3: Z(u,v) -> Check _ cursor moves to Row 3!
Send-Key 0x28 400
Capture-Screen "step18_parametric_details_row3_Z_cursor.png"

# Switch to 3D Plot view: Return to list with Back, then Right arrow to 3D Plot
Send-Key 0x1B 600
Send-Key 0x27 1000
Capture-Screen "step19_parametric_3d_rendered.png"

# Rotate view (Left 2, Up 1)
Send-Key 0x25 200
Send-Key 0x25 200
Send-Key 0x26 500
Capture-Screen "step20_parametric_3d_rotated.png"

# Return to Expressions tab (F1 / Left)
Send-Key 0x25 600
# Add Explicit function: Move to "+ Add an element" and press Enter
Send-Key 0x28 300
Send-Key 0x0D 600
# Row 0 is "1. z = f(x, y) [Formula]" -> Enter to add
Send-Key 0x0D 800
Capture-Screen "step21_explicit_added_in_list.png"

# Enter Details screen for Explicit function
Send-Key 0x0D 800
Capture-Screen "step22_explicit_details_row0.png"

# Down to Row 2: Formula editor (z = ...) -> Check _ cursor!
Send-Key 0x28 300
Send-Key 0x28 400
Capture-Screen "step23_explicit_formula_cursor.png"

# Return to Expressions (Back)
Send-Key 0x1B 600
# Move to 3D Plot (Right arrow)
Send-Key 0x27 1000
Capture-Screen "step24_dual_surfaces_3d_rendered.png"

Write-Host "=== ALL TEST VERIFICATIONS COMPLETED! ==="
Stop-Process -Id $proc.Id -Force
Pop-Location
