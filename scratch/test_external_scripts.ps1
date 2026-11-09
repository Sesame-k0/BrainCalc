Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

Push-Location "$PSScriptRoot\..\output\release\simulator\brain-host\Lilium"

# Kill previous instances
Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
Remove-Item "lilium_state.bin" -Force -ErrorAction SilentlyContinue

# Setup scripts directory and test files
$scriptsDir = ".\scripts"
if (-not (Test-Path $scriptsDir)) {
    New-Item -ItemType Directory -Path $scriptsDir -Force | Out-Null
}

# 1. Python (scripts/calc_fib.py)
@"
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a
print('FIB(10) =', fib(10))
"@ | Set-Content -Path "$scriptsDir\calc_fib.py" -Encoding utf8

# 2. BASIC (scripts/circle.bas) - ごっちゃに直下に配置
@"
10 CLS
20 PRINT "CIRCLE DEMO"
30 FOR A = 0 TO 360 STEP 10
40 PSET(160+50*COS(A), 120+50*SIN(A), 1)
50 NEXT A
"@ | Set-Content -Path "$scriptsDir\circle.bas" -Encoding utf8

# 3. Logo (scripts/spiral.lgo) - ごっちゃに直下に配置
@"
CS
REPEAT 50 [
  FD 100
  RT 145
]
"@ | Set-Content -Path "$scriptsDir\spiral.lgo" -Encoding utf8

# 4. Brainfuck (scripts/nested_subfolder/add_test.bf) - サブフォルダ内に配置
$subDir = "$scriptsDir\nested_subfolder"
if (-not (Test-Path $subDir)) {
    New-Item -ItemType Directory -Path $subDir -Force | Out-Null
}
@"
++>+++<[->+<]>
"@ | Set-Content -Path "$subDir\add_test.bf" -Encoding utf8

Write-Host "Test files created in scripts/ directory."

# Start AppMain.exe
$proc = Start-Process -FilePath ".\AppMain.exe" -PassThru
Start-Sleep -Seconds 2

$typeDef = @"
using System;
using System.Text;
using System.Runtime.InteropServices;

public class Win32HelperScriptsTest {
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

$hWnd = [Win32HelperScriptsTest]::FindLiliumHwnd($proc.Id)
Write-Host "Found Lilium HWND: $hWnd"
Start-Sleep -Milliseconds 500

$artifactDir = "C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"

function Capture-Screen($name) {
    try {
        $bmp = New-Object System.Drawing.Bitmap(800, 480)
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $hdc = $g.GetHdc()
        [Win32HelperScriptsTest]::PrintWindow($hWnd, $hdc, 0) | Out-Null
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
$VK_ESCAPE  = 0x1B
$VK_UP      = 0x26
$VK_DOWN    = 0x28
$VK_4       = 0x34

function Send-Key($vk, $delay = 250) {
    [Win32HelperScriptsTest]::PostMessage($hWnd, $WM_KEYDOWN, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 50
    [Win32HelperScriptsTest]::PostMessage($hWnd, $WM_KEYUP, [IntPtr]$vk, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds $delay
}

# 1. ホーム画面から Coding アプリを開く (Down 4, Enter)
Write-Host "1. Open Coding app..."
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 200
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 800

# 2. 1. PocketPy (Python) の一覧確認 (初期選択行が0なので Enter で入る)
Write-Host "2. Check Python scripts list..."
Send-Key $VK_RETURN 600
Capture-Screen "scripts_test_01_python"
# 言語選択画面に戻る (Escape)
Send-Key $VK_ESCAPE 600

# 3. 2. TinyBasic (BASIC) の一覧確認 (Down, Enter)
Write-Host "3. Check BASIC scripts list..."
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "scripts_test_02_basic"
# 言語選択画面に戻る (Escape)
Send-Key $VK_ESCAPE 600

# 4. 3. Logo の一覧確認 (Down, Enter)
Write-Host "4. Check Logo scripts list..."
Send-Key $VK_DOWN 300
Send-Key $VK_RETURN 600
Capture-Screen "scripts_test_03_logo"
# 言語選択画面に戻る (Escape)
Send-Key $VK_ESCAPE 600

# 5. 4. Brainfuck の一覧確認 (4キーでシークレットモード, Enter)
Write-Host "5. Check Brainfuck scripts list..."
Send-Key $VK_4 400
Send-Key $VK_RETURN 600
Capture-Screen "scripts_test_04_brainfuck"

# 終了処理
Start-Sleep -Seconds 1
Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
Pop-Location
Write-Host "Scripts test completed!"
