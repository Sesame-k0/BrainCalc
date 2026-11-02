Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class TestKeyDebug {
    public delegate bool EnumThreadDelegate(IntPtr hWnd, IntPtr lParam);

    [DllImport("user32.dll")]
    public static extern bool EnumThreadWindows(int dwThreadId, EnumThreadDelegate lpfn, IntPtr lParam);

    [DllImport("user32.dll", EntryPoint = "GetClassNameW", CharSet = CharSet.Unicode)]
    public static extern int GetClassNameW(IntPtr hWnd, StringBuilder lpClassName, int nMaxCount);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);

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

$hWnd = [TestKeyDebug]::FindLiliumHwnd($proc.Id)
Write-Host "hWnd: $hWnd"

function SendTestKey($vk, $name) {
    $r1 = [TestKeyDebug]::PostMessage($hWnd, 0x100, [IntPtr]$vk, [IntPtr]0)
    $err1 = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Start-Sleep -Milliseconds 100
    $r2 = [TestKeyDebug]::PostMessage($hWnd, 0x101, [IntPtr]$vk, [IntPtr]0xC0000001)
    $err2 = [System.Runtime.InteropServices.Marshal]::GetLastWin32Error()
    Write-Host "Key $name ($vk) -> down:$r1 (err:$err1), up:$r2 (err:$err2)"
    Start-Sleep -Milliseconds 500
}

SendTestKey 0x28 "Down 1"
SendTestKey 0x28 "Down 2"
SendTestKey 0x27 "Right 1"
SendTestKey 0x0D "Enter 1"

Start-Sleep -Seconds 1
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Pop-Location
