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
$win32 = Add-Type -MemberDefinition $sig -Name "Win32TimerTest" -Namespace "Win32Timer" -PassThru

$artifactDir = "C:/Users/takan/.gemini/antigravity/brain/13890e2a-562c-4ed0-b208-da5a26778346"

function Capture-Screen($name) {
    Start-Sleep -Milliseconds 300
    $p = Get-Process -Id $proc.Id
    $bmp = New-Object System.Drawing.Bitmap(800, 480)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $hdc = $g.GetHdc()
    [Win32Timer.Win32TimerTest]::PrintWindow($p.MainWindowHandle, $hdc, 0)
    $g.ReleaseHdc($hdc)
    $g.Dispose()
    $outPath = Join-Path $artifactDir $name
    $bmp.Save($outPath, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host "Saved: $outPath"
}

function Send-Key($vk) {
    $p = Get-Process -Id $proc.Id
    [Win32Timer.Win32TimerTest]::PostMessage($p.MainWindowHandle, 0x0100, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 80
    [Win32Timer.Win32TimerTest]::PostMessage($p.MainWindowHandle, 0x0101, [IntPtr]$vk, [IntPtr]0)
    Start-Sleep -Milliseconds 250
}

# 1. Capture initial Home screen (Check title bar CPU usage on left side)
Capture-Screen "v2_01_home_screen.png"

# Navigate to Timer app on home screen
# Row 0: Calculation(0), Functions(1), Sequences(2)
# Row 1: Statistics(3), Regression(4), Probability(5)
# Row 2: Equations(6), Periodic(7), Code(8)
# Row 3: Settings(9), Reader(10), Timer(11)
Send-Key 0x28 # Down
Send-Key 0x28 # Down
Send-Key 0x28 # Down
Send-Key 0x27 # Right
Send-Key 0x27 # Right
Start-Sleep -Milliseconds 400

# 2. Capture Home screen with Timer selected (Check sidebar Japanese help!)
Capture-Screen "v2_02_home_timer_selected.png"

# 3. Enter Timer app (VK_RETURN = 0x0D)
Send-Key 0x0D
Start-Sleep -Milliseconds 600

# Capture initial Timer app view (Check big 7-segment digital time, layout, notes, sidebar help)
Capture-Screen "v2_03_timer_stopwatch_initial.png"

# 4. Start Stopwatch (VK_RETURN = 0x0D)
Send-Key 0x0D
Start-Sleep -Seconds 2

# Capture running Stopwatch
Capture-Screen "v2_04_timer_stopwatch_running.png"

# 5. Switch to Timer mode (VK_RIGHT = 0x27)
Send-Key 0x27
Start-Sleep -Milliseconds 400
Capture-Screen "v2_05_timer_countdown_mode.png"

# 6. Switch to Pomodoro mode (VK_RIGHT = 0x27)
Send-Key 0x27
Start-Sleep -Milliseconds 400
Capture-Screen "v2_06_timer_pomodoro_mode.png"

# 7. Type some notes using numbers (e.g. '1', '2', '3', '4', '5')
Send-Key 0x31
Send-Key 0x32
Send-Key 0x33
Send-Key 0x34
Send-Key 0x35
Start-Sleep -Milliseconds 400
Capture-Screen "v2_07_timer_with_notes.png"

# Stop process
Stop-Process -Id $proc.Id -Force
Write-Host "Verification complete!"
