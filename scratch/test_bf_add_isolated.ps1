Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Remove-Item "lilium_state.bin" -Force -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class Win32HelperBFAdd {
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

$hWnd = [Win32HelperBFAdd]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperBFAdd]::PrintWindow($hWnd, $hdc, 0) | Out-Null
        $g.ReleaseHdc($hdc)
        $g.Dispose()
        $path = Join-Path $artifactDir "$name.png"
        $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
        $bmp.Dispose()
        Write-Host "Saved: $path"
    } catch {
        Write-Host "Capture failed: $_"
    }
}

$WM_KEYDOWN = 0x0100
$WM_KEYUP   = 0x0101
$VK_RETURN  = 0x0D
$VK_DOWN    = 0x28
$VK_4       = 0x34

function Send-Key($vk, $delay = 250) {
    [Win32HelperBFAdd]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 50
    [Win32HelperBFAdd]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds $delay
}

# 1. Open Coding app (Down 4, Enter)
Write-Host "1. Open Coding app..."
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 800

# 2. Select 4. Brainfuck
Write-Host "2. Select 4. Brainfuck..."
Send-Key $VK_4 400
Send-Key $VK_RETURN 600

# 3. Brainfuck script list: Move Down to "Add a script"
Write-Host "3. Down to Add a script..."
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "bf_add_01_script_added"

# 4. Confirm default name
Send-Key $VK_RETURN 400
Capture-Screen "bf_add_02_script_confirmed"

# Done
Start-Sleep -Seconds 1
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Host "Brainfuck add script test completed!"
