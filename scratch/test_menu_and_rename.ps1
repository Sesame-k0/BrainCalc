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

public class Win32HelperMenu {
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

$hWnd = [Win32HelperMenu]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperMenu]::PrintWindow($hWnd, $hdc, 0) | Out-Null
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
$VK_LEFT    = 0x25
$VK_RIGHT   = 0x27

function Send-Key($vk, $delay = 250) {
    [Win32HelperMenu]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 50
    [Win32HelperMenu]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds $delay
}

# 1. ホーム画面から Coding アプリを開く (Down 4, Enter)
Write-Host "1. Open Coding app..."
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 800

# 2. 1. PocketPy を選択 (Enter)
Write-Host "2. Select Python..."
Send-Key $VK_RETURN 600

# 現在 sample.py (row 0, col 0) にいる
# 新規スクリプトを追加して 2 つのスクリプトがある状態を作る
Write-Host "3. Add new script..."
Send-Key $VK_DOWN 300 # Down to "Add a script"
Send-Key $VK_RETURN 600 # Add script -> enters rename mode
Send-Key $VK_RETURN 500 # Confirm name (script1.py)

# 現在 script1.py (row 1, col 0) にいる
# 右側の三点メニューボタンに移動 (Right)
Write-Host "4. Move to '...' of script1.py..."
Send-Key $VK_RIGHT 400

# Enter で三点メニューを開く
Write-Host "5. Open script parameter menu..."
Send-Key $VK_RETURN 600
Capture-Screen "menu_01_execute_help"

# Row 1: 名前変更
Send-Key $VK_DOWN 400
Capture-Screen "menu_02_rename_help"

# Row 2: 自動インポート
Send-Key $VK_DOWN 400
Capture-Screen "menu_03_autoimport_help"

# Row 3: サイズ
Send-Key $VK_DOWN 400
Capture-Screen "menu_04_size_help"

# Row 4: 複製
Send-Key $VK_DOWN 400
Capture-Screen "menu_05_duplicate_help"

# Row 5: 削除
Send-Key $VK_DOWN 400
Capture-Screen "menu_06_delete_help"

# 確実に Row 1 (名前変更) に移動: Up 5回で一番上(Row 0)へ、その後 Down 1回で Row 1 へ
Write-Host "6. Return to Row 1 (Rename) and execute..."
Send-Key $VK_UP 150
Send-Key $VK_UP 150
Send-Key $VK_UP 150
Send-Key $VK_UP 150
Send-Key $VK_UP 250
Send-Key $VK_DOWN 300

# Rename を実行 (Enter)
Send-Key $VK_RETURN 800
Capture-Screen "menu_07_rename_script1_verified"

# 終了処理
Start-Sleep -Seconds 1
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Host "Menu and rename test completed!"
