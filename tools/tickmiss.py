"""tickmiss.py - zoom-pon と同じ形のループで、注入されたキーを見逃す割合を測る

zoom-pon の待受は「メッセージを処理 -> tick -> MsgWaitForMultipleObjects(8ms)」
という形で回っている。押されている時間が短いキーは、この周期の隙間に
丸ごと収まって観測されないことがある。どれくらい押していれば確実に
見えるのかを、実際に同じ形のループを回して数える。

    python tickmiss.py <秒数> <VK(16進)> [ログ]
"""

import ctypes
import sys
import time
from ctypes import wintypes

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.GetAsyncKeyState.argtypes = [ctypes.c_int]
user32.GetAsyncKeyState.restype = ctypes.c_short
user32.MsgWaitForMultipleObjects.argtypes = [
    wintypes.DWORD, ctypes.c_void_p, wintypes.BOOL, wintypes.DWORD, wintypes.DWORD
]
user32.PeekMessageW.argtypes = [ctypes.c_void_p, wintypes.HWND,
                                wintypes.UINT, wintypes.UINT, wintypes.UINT]

POLL_INTERVAL = 8          # zoom-pon と同じ
QS_ALLINPUT = 0x04FF
PM_REMOVE = 0x0001


class MSG(ctypes.Structure):
    _fields_ = [("hwnd", wintypes.HWND), ("message", wintypes.UINT),
                ("wParam", ctypes.c_void_p), ("lParam", ctypes.c_void_p),
                ("time", wintypes.DWORD), ("pt_x", wintypes.LONG),
                ("pt_y", wintypes.LONG)]


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    vk = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x41
    logpath = sys.argv[3] if len(sys.argv) > 3 else None

    msg = MSG()
    periods = []
    stamps = []
    seen = 0
    prev_down = False
    last = time.perf_counter()
    end = last + seconds
    print("READY", flush=True)

    while time.perf_counter() < end:
        while user32.PeekMessageW(ctypes.byref(msg), None, 0, 0, PM_REMOVE):
            pass

        down = bool(user32.GetAsyncKeyState(vk) & 0x8000)
        if down and not prev_down:
            seen += 1
            stamps.append(time.perf_counter())
        prev_down = down

        user32.MsgWaitForMultipleObjects(0, None, False, POLL_INTERVAL, QS_ALLINPUT)

        now = time.perf_counter()
        periods.append((now - last) * 1000.0)
        last = now

    periods.sort()
    n = len(periods)
    out = [
        "SEEN=%d" % seen,
        "PERIOD_MS min=%.1f med=%.1f p90=%.1f max=%.1f loops=%d"
        % (periods[0], periods[n // 2], periods[int(n * 0.9)], periods[-1], n),
    ]
    text = "\n".join(out)
    print(text, flush=True)
    if logpath:
        with open(logpath, "w", encoding="utf-8") as f:
            f.write(text + "\n")


if __name__ == "__main__":
    main()
