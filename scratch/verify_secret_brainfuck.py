import os
import sys
import time
import ctypes
from ctypes import wintypes
import subprocess

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_KEYDOWN = 0x0100
WM_KEYUP   = 0x0101
VK_RETURN  = 0x0D
VK_ESCAPE  = 0x1B
VK_PRIOR   = 0x21  # PageUp
VK_NEXT    = 0x22  # PageDown
VK_LEFT    = 0x25
VK_UP      = 0x26
VK_RIGHT   = 0x27
VK_DOWN    = 0x28
VK_F12     = 0x7B
VK_4       = 0x34

artifact_dir = r"C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"
lilium_dir = r"G:\マイドライブ\BackUp-K0Project\Program\BrainCalc\BrainCalc\output\release\simulator\brain-host\Lilium"

# Kill old instances
subprocess.run(["taskkill", "/F", "/IM", "AppMain.exe"], capture_output=True)
time.sleep(0.5)

# Launch Lilium
proc = subprocess.Popen([os.path.join(lilium_dir, "AppMain.exe")], cwd=lilium_dir)
print(f"Launched Lilium PID={proc.pid}, waiting for window...")
time.sleep(8.0) # NumWorks home initialization delay

# Find window
found_hwnd = None
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

def enum_windows_cb(hwnd, lparam):
    global found_hwnd
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    if buf.value == "BrainCalcWinCEClass":
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == proc.pid:
            found_hwnd = hwnd
            return False
    return True

user32.EnumWindows(WNDENUMPROC(enum_windows_cb), 0)
if not found_hwnd:
    print("Failed to find Lilium window!")
    proc.kill()
    sys.exit(1)

print(f"Found HWND: {hex(found_hwnd)}")

def send_key(vk, delay_ms=250):
    user32.PostMessageW(found_hwnd, WM_KEYDOWN, vk, 0)
    time.sleep(0.05)
    user32.PostMessageW(found_hwnd, WM_KEYUP, vk, 0)
    time.sleep(delay_ms / 1000.0)

def capture(name):
    # Create DC and DIB
    hwnd_dc = user32.GetDC(found_hwnd)
    mem_dc = gdi32.CreateCompatibleDC(hwnd_dc)
    hbitmap = gdi32.CreateCompatibleBitmap(hwnd_dc, 800, 480)
    old_bmp = gdi32.SelectObject(mem_dc, hbitmap)
    user32.PrintWindow(found_hwnd, mem_dc, 0)

    # Use PIL / save to file
    from PIL import Image
    import ctypes.wintypes

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [
            ('biSize', wintypes.DWORD),
            ('biWidth', wintypes.LONG),
            ('biHeight', wintypes.LONG),
            ('biPlanes', wintypes.WORD),
            ('biBitCount', wintypes.WORD),
            ('biCompression', wintypes.DWORD),
            ('biSizeImage', wintypes.DWORD),
            ('biXPelsPerMeter', wintypes.LONG),
            ('biYPelsPerMeter', wintypes.LONG),
            ('biClrUsed', wintypes.DWORD),
            ('biClrImportant', wintypes.DWORD)
        ]

    bih = BITMAPINFOHEADER()
    bih.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bih.biWidth = 800
    bih.biHeight = -480
    bih.biPlanes = 1
    bih.biBitCount = 32
    bih.biCompression = 0

    buf = ctypes.create_string_buffer(800 * 480 * 4)
    gdi32.GetDIBits(mem_dc, hbitmap, 0, 480, buf, ctypes.byref(bih), 0)

    img = Image.frombuffer('RGBA', (800, 480), buf, 'raw', 'BGRA', 0, 1)
    img_rgb = img.convert('RGB')
    out_path = os.path.join(artifact_dir, f"{name}.png")
    img_rgb.save(out_path)
    print(f"Saved: {out_path}")

    # Cleanup GDI
    gdi32.SelectObject(mem_dc, old_bmp)
    gdi32.DeleteObject(hbitmap)
    gdi32.DeleteDC(mem_dc)
    user32.ReleaseDC(found_hwnd, hwnd_dc)

# Test Step 1: Open Coding app (Down x4, Enter)
print("Opening Coding app...")
send_key(VK_DOWN, 200)
send_key(VK_DOWN, 200)
send_key(VK_DOWN, 200)
send_key(VK_DOWN, 200)
send_key(VK_RETURN, 800)

# Capture Normal Select Language Screen (Should show 3 languages, PocketPy help with NO secret hints)
capture("secret_test_01_normal_select")

# Test Step 2: Press '4' (Should do nothing, NOT reveal secret language)
print("Pressing '4' in normal mode...")
send_key(VK_4, 300)
capture("secret_test_02_press_4_ignored")

# Test Step 3: Press F12 to check normal guide in Category 3
print("Pressing F12 for normal manual...")
send_key(VK_F12, 500)
capture("secret_test_03_normal_manual_cat3")

# Close manual
send_key(VK_F12, 400)

# Test Step 4: Press PageDown to unlock secret language
print("Pressing PageDown (Unlock secret mode)...")
send_key(VK_NEXT, 600)
capture("secret_test_04_secret_mode_revealed")

# Test Step 5: Press F12 while in secret mode (Should open directly to BrainF*ck manual!)
print("Pressing F12 while in secret mode...")
send_key(VK_F12, 600)
capture("secret_test_05_brainfuck_manual_visible")

# Close manual
send_key(VK_F12, 400)

# Test Step 6: Press Up to return to normal mode
print("Pressing Up to return to normal...")
send_key(VK_UP, 500)
capture("secret_test_06_returned_to_normal")

# Test Step 7: Press F12 again to ensure BrainF*ck disappeared from guide
print("Pressing F12 after returning to normal...")
send_key(VK_F12, 500)
capture("secret_test_07_manual_hidden_again")

# Close manual and terminate
send_key(VK_F12, 300)
proc.terminate()
print("Verification complete!")
