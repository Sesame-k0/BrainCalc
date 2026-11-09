# -*- coding: utf-8 -*-
import subprocess
import time
import os
import ctypes
from ctypes import wintypes

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

WM_KEYDOWN = 0x0100
WM_KEYUP   = 0x0101
VK_RETURN  = 0x0D
VK_ESCAPE  = 0x1B
VK_LEFT    = 0x25
VK_UP      = 0x26
VK_RIGHT   = 0x27
VK_DOWN    = 0x28
VK_F12     = 0x7B

artifact_dir = r"C:\Users\takan\.gemini\antigravity\brain\90946f9e-7a76-4960-9378-6ce82d13b1b0"
exe_path = r"output\release\simulator\brain-host\Lilium\AppMain.exe"
cwd = r"output\release\simulator\brain-host\Lilium"

# Kill existing instances
subprocess.run(["powershell", "-Command", "Get-Process AppMain* -ErrorAction SilentlyContinue | Stop-Process -Force"])
time.sleep(0.5)

proc = subprocess.Popen([exe_path], cwd=cwd)
print(f"Started AppMain.exe (PID={proc.pid})")
time.sleep(8)

# Find Window
hwnd = 0
WNDENUMPROC = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

def enum_windows_callback(h, l):
    global hwnd
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, buf, 256)
    if buf.value == "BrainCalcWinCEClass":
        pid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
        if pid.value == proc.pid:
            hwnd = h
            return False
    return True

user32.EnumWindows(WNDENUMPROC(enum_windows_callback), 0)
print(f"Found hwnd: {hwnd}")

if not hwnd:
    proc.terminate()
    sys.exit(1)

user32.SetForegroundWindow(hwnd)
time.sleep(0.5)

def capture(name):
    # Capture using GDI PrintWindow
    hdc_screen = user32.GetDC(hwnd)
    hdc_mem = gdi32.CreateCompatibleDC(hdc_screen)
    hbm = gdi32.CreateCompatibleBitmap(hdc_screen, 800, 480)
    gdi32.SelectObject(hdc_mem, hbm)
    user32.PrintWindow(hwnd, hdc_mem, 0)

    # Save to bitmap
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
    bih.biHeight = -480 # top-down
    bih.biPlanes = 1
    bih.biBitCount = 32
    bih.biCompression = 0

    buf_size = 800 * 480 * 4
    buf = (ctypes.c_char * buf_size)()
    gdi32.GetDIBits(hdc_mem, hbm, 0, 480, buf, ctypes.byref(bih), 0)

    # Save PNG using PIL or BMP
    from PIL import Image
    img = Image.frombuffer('RGBA', (800, 480), bytes(buf), 'raw', 'BGRA', 0, 1)
    # convert to RGB
    img = img.convert('RGB')
    out_path = os.path.join(artifact_dir, f"{name}.png")
    img.save(out_path)
    print(f"Saved: {out_path}")

    gdi32.DeleteObject(hbm)
    gdi32.DeleteDC(hdc_mem)
    user32.ReleaseDC(hwnd, hdc_screen)

def send_key(vk, delay=0.5):
    user32.PostMessageW(hwnd, WM_KEYDOWN, vk, 0)
    time.sleep(0.08)
    user32.PostMessageW(hwnd, WM_KEYUP, vk, 0)
    time.sleep(delay)

# 1. Open F12 Guide
send_key(VK_F12, 1.2)
capture("py_guide_01_math_solve_01")

# 2. Down to topic 2
send_key(VK_DOWN, 0.6)
capture("py_guide_02_math_solve_02")

# 3. Down to topic 3
send_key(VK_DOWN, 0.6)
capture("py_guide_03_math_solve_03")

# 4. Right to App Features
send_key(VK_RIGHT, 0.8)
capture("py_guide_04_app_features")

# 5. Down in App Features
send_key(VK_DOWN, 0.6)
capture("py_guide_04b_functions")

# 6. Right to 3D Graph
send_key(VK_RIGHT, 0.8)
capture("py_guide_05_plot3d")

# 7. Right to Coding
send_key(VK_RIGHT, 0.8)
capture("py_guide_06_coding")

# 8. Right to Keyboard
send_key(VK_RIGHT, 0.8)
capture("py_guide_07_keyboard")

# 9. Down in Keyboard
send_key(VK_DOWN, 0.6)
capture("py_guide_08_keyboard_func")

send_key(VK_ESCAPE, 0.3)
proc.terminate()
print("All captures finished successfully!")
