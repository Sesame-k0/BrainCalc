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

public class Win32HelperAllLangs {
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

$hWnd = [Win32HelperAllLangs]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperAllLangs]::PrintWindow($hWnd, $hdc, 0) | Out-Null
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
$VK_BACK    = 0x08
$VK_UP      = 0x26
$VK_DOWN    = 0x28
$VK_NEXT    = 0x22 # PageDown (KeyAction::RightParen)
$VK_2       = 0x32
$VK_3       = 0x33
$VK_4       = 0x34

function Send-Key($vk, $delay = 250) {
    [Win32HelperAllLangs]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 50
    [Win32HelperAllLangs]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds $delay
}

# 1. Open Coding app (Down 4, Enter)
Write-Host "1. Open Coding app..."
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 800

# 2. Test BASIC (Down 1, Enter)
Write-Host "2. Test BASIC add script..."
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
# In BASIC list: Down to "Add a script", Enter
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "all_01_basic_add_script"
Send-Key $VK_RETURN 400 # confirm name (script1.bas)
Send-Key $VK_BACK 500 # Back to Language Select

# 3. Test Logo (Down 2, Enter)
Write-Host "3. Test Logo add script..."
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_RETURN 600
# In Logo list: Down to "Add a script", Enter
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "all_02_logo_add_script"
Send-Key $VK_RETURN 400 # confirm name (script1.lgo)
Send-Key $VK_BACK 500 # Back to Language Select

# 4. Test Brainfuck (PageDown or 4)
Write-Host "4. Test Brainfuck add script..."
Send-Key $VK_4 400
Send-Key $VK_RETURN 600
# In Brainfuck list: Down to "Add a script", Enter
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "all_03_bf_add_script"
Send-Key $VK_RETURN 400 # confirm name (script1.bf)
Capture-Screen "all_04_bf_list_with_script1"

# Done
Start-Sleep -Seconds 1
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Host "All languages add script test completed!"
