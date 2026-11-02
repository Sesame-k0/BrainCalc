Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$proc = Start-Process -FilePath "output/release/simulator/brain-host/Lilium/AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$sig = @"
[DllImport("user32.dll")]
public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdcBlt, uint nFlags);
[DllImport("user32.dll")]
public static extern bool PostMessage(IntPtr hWnd, uint Msg, IntPtr wParam, IntPtr lParam);
"@
$win32 = Add-Type -MemberDefinition $sig -Name Win32Utils -Namespace Win32Modal -PassThru

$artifactDir = "C:/Users/takan/.gemini/antigravity/brain/13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    $bmp = New-Object System.Drawing.Bitmap(800, 480)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [Win32Modal.Win32Utils]::PrintWindow($proc.MainWindowHandle, $hdc, 2)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $outPath = Join-Path $artifactDir $name
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "Saved: $outPath"
}

# 1. 初期画面
Capture-Screen "modal_test_01_init.png"

# 2. 右側サイドバー最下部の「終了」ボタン (X=720, Y=450) をクリック
$lParam = [IntPtr]((450 -shl 16) -bor 720)
[Win32Modal.Win32Utils]::PostMessage($proc.MainWindowHandle, 0x0201, [IntPtr]1, $lParam)
Start-Sleep -Milliseconds 100
[Win32Modal.Win32Utils]::PostMessage($proc.MainWindowHandle, 0x0202, [IntPtr]0, $lParam)
Start-Sleep -Seconds 1

# 3. 終了確認ダイアログのキャプチャ
Capture-Screen "modal_test_02_exit_dialog.png"

# 4. 左矢印キー (VK_LEFT = 0x25) で「設定」ボタンを選択
[Win32Modal.Win32Utils]::PostMessage($proc.MainWindowHandle, 0x0100, [IntPtr]0x25, [IntPtr]0)
Start-Sleep -Milliseconds 100
[Win32Modal.Win32Utils]::PostMessage($proc.MainWindowHandle, 0x0101, [IntPtr]0x25, [IntPtr]0)
Start-Sleep -Seconds 1

Capture-Screen "modal_test_03_exit_settings_selected.png"

# 5. Enterキー (VK_RETURN = 0x0D) で設定画面を開く
[Win32Modal.Win32Utils]::PostMessage($proc.MainWindowHandle, 0x0100, [IntPtr]0x0D, [IntPtr]0)
Start-Sleep -Milliseconds 100
[Win32Modal.Win32Utils]::PostMessage($proc.MainWindowHandle, 0x0101, [IntPtr]0x0D, [IntPtr]0)
Start-Sleep -Seconds 1

# 6. 設定画面のキャプチャ
Capture-Screen "modal_test_04_settings_main.png"

Stop-Process -Id $proc.Id -Force
