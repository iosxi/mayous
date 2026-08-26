"""test_asyncbit0.py - GetAsyncKeyState の最下位ビットで短い押下を拾えるか

GetAsyncKeyState の戻り値は
    0x8000 … いま押されているか
    0x0001 … 前回この関数を呼んでから押されたか
の 2 つを持つ。ポーリング方式のアプリが 0x8000 しか見ていないと、
周期より短い押下は丸ごと取りこぼす。0x0001 なら「間に押された」ことが
分かるはずだが、本当にそうかを実測する。

    python test_asyncbit0.py
"""

import ctypes
import threading
import time
from ctypes import wintypes

u = ctypes.WinDLL("user32", use_last_error=True)
u.GetAsyncKeyState.argtypes = [ctypes.c_int]
u.GetAsyncKeyState.restype = ctypes.c_short

VK = 0x7C          # F13
POLL_MS = 100      # わざと粗い周期。zoom-pon が拡大中に伸びる状況を模す
PRESS_MS = 20      # 周期よりずっと短い押下
SHOTS = 8


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong))]


class INPUT(ctypes.Structure):
    _fields_ = [("type", wintypes.DWORD), ("ki", KEYBDINPUT),
                ("pad", ctypes.c_ubyte * 8)]


def key(vk, up):
    i = INPUT()
    i.type = 1
    i.ki.wVk = vk
    i.ki.dwFlags = 2 if up else 0
    u.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


def presser():
    time.sleep(1.0)
    for _ in range(SHOTS):
        key(VK, False)
        time.sleep(PRESS_MS / 1000.0)
        key(VK, True)
        time.sleep(0.45)


def main():
    seen_down = 0      # 0x8000 で見えた回数
    seen_bit0 = 0      # 0x0001 で見えた回数
    prev = False

    u.GetAsyncKeyState(VK)          # 溜まっているぶんを捨てる
    t = threading.Thread(target=presser, daemon=True)
    t.start()

    end = time.perf_counter() + SHOTS * 0.47 + 2.0
    while time.perf_counter() < end:
        s = u.GetAsyncKeyState(VK)
        down = bool(s & 0x8000)
        if down and not prev:
            seen_down += 1
        if s & 0x0001:
            seen_bit0 += 1
        prev = down
        time.sleep(POLL_MS / 1000.0)

    print("PRESSED=%d  POLL_MS=%d  PRESS_MS=%d" % (SHOTS, POLL_MS, PRESS_MS))
    print("SEEN_BY_0x8000=%d" % seen_down)
    print("SEEN_BY_0x0001=%d" % seen_bit0)


main()
