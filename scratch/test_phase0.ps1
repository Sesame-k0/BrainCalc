Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

# Kill previous instances
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500

# Remove old debug log if exists
if (Test-Path "lilium_debug.log") {
    Remove-Item "lilium_debug.log" -Force
}

$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Milliseconds 800

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class Win32HelperPhase0 {
    public delegate bool EnumThreadDelegate(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumThreadWindows(int dwThreadId, EnumThreadDelegate lpfn, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "GetClassNameW", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

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

$hWnd = [Win32HelperPhase0]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperPhase0]::PrintWindow($hWnd, $hdc, 0)
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

Capture-Screen "phase0_3d_benchmark_preview.png"

# Close process cleanly
Get-Process -Id $proc.Id -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location

# Show lilium_debug.log if generated
if (Test-Path "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium\lilium_debug.log") {
    Write-Host "=== LILIUM_DEBUG.LOG OUTPUT ==="
    Get-Content "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium\lilium_debug.log" | Select-String -Pattern "LLCubic|Grid|Triangles|Mesh Gen|Transform|Rasterize"
}
