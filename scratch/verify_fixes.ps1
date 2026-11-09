Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

# Kill previous instances
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Remove-Item "lilium_state.bin" -Force -ErrorAction SilentlyContinue

$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class Win32HelperFix {
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

$hWnd = [Win32HelperFix]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperFix]::PrintWindow($hWnd, $hdc, 0) | Out-Null
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
$VK_NEXT    = 0x22 # PageDown (KeyAction::RightParen)
$VK_PRIOR   = 0x21 # PageUp   (KeyAction::LeftParen)
$VK_4       = 0x34

function Send-Key($vk, $delay = 250) {
    [Win32HelperFix]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 50
    [Win32HelperFix]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds $delay
}

# 1. ホーム画面から Coding アプリを起動 (Down 4回, Enter)
Write-Host "Step 1: Open Coding app (Down 4, Enter)..."
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 800

# 2. 言語選択画面: 3. Logo を選択 (Down 2回, Enter)
Write-Host "Step 2: Select Logo..."
Send-Key $VK_DOWN 300
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600

# 3. Logo スクリプト一覧画面: 「スクリプトを追加」へ移動して Enter
Write-Host "Step 3: Add script in Logo..."
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "fix_01_logo_add_script"

# Enter を押してスクリプト名確定
Send-Key $VK_RETURN 400

# 4. 追加されたスクリプト（行1）の右側「...」ボタンへ移動 (Right)
Write-Host "Step 4: Go to parameter button of script..."
Send-Key $VK_RIGHT 300
# Enter で三点メニューを開く
Send-Key $VK_RETURN 600
Capture-Screen "fix_02_menu_row0_execute"

# 三点メニュー各行の日本語解説を確認
Write-Host "Step 5: Verify menu Japanese help..."
# Row 1: Rename
Send-Key $VK_DOWN 300
Capture-Screen "fix_03_menu_row1_rename"

# Row 2: Auto-import
Send-Key $VK_DOWN 300
Capture-Screen "fix_04_menu_row2_autoimport"

# Row 3: Size
Send-Key $VK_DOWN 300
Capture-Screen "fix_05_menu_row3_size"

# Row 4: Duplicate
Send-Key $VK_DOWN 300
Capture-Screen "fix_06_menu_row4_duplicate"

# Row 5: Delete
Send-Key $VK_DOWN 300
Capture-Screen "fix_07_menu_row5_delete"

# Row 1 (Rename) に戻る (Up 4回)
Send-Key $VK_UP 200
Send-Key $VK_UP 200
Send-Key $VK_UP 200
Send-Key $VK_UP 300

# Rename を選択実行 (Enter)
Write-Host "Step 6: Execute Rename from menu..."
Send-Key $VK_RETURN 600
Capture-Screen "fix_08_rename_target_verified"

# リネーム編集をキャンセル/確定 (Enter)
Send-Key $VK_RETURN 300

# 言語選択画面に戻る (Back 2回)
Write-Host "Step 7: Back to Language Select..."
Send-Key $VK_BACK 400
Send-Key $VK_BACK 600

# 4. BrainF*ck を選択 (4キー)
Write-Host "Step 8: Select Brainfuck..."
Send-Key $VK_4 400
Send-Key $VK_RETURN 600

# Brainfuck スクリプト一覧で「スクリプトを追加」へ移動して Enter
Write-Host "Step 9: Add script in Brainfuck..."
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "fix_09_bf_add_script"

# 終了処理
Start-Sleep -Seconds 1
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Host "Test completed!"
