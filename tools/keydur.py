"""keydur.py - 注入されたキーが実際に何ミリ秒押されていたかを測る

GetAsyncKeyState を 1ms 間隔で叩き続けるので、40ms の押下でも取りこぼさない。
mayous が KeyHoldMs どおりに押しっぱなしにできているかの確認用。

    python keydur.py <秒数> <VK(16進)>
"""
import ctypes, sys, time

u = ctypes.WinDLL("user32", use_last_error=True)
u.GetAsyncKeyState.argtypes = [ctypes.c_int]
u.GetAsyncKeyState.restype = ctypes.c_short


def main():
    seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 20.0
    vk = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x41
    durs, prev, t0 = [], False, 0.0
    end = time.perf_counter() + seconds
    print("READY", flush=True)
    while time.perf_counter() < end:
        down = bool(u.GetAsyncKeyState(vk) & 0x8000)
        if down and not prev:
            t0 = time.perf_counter()
        elif prev and not down:
            durs.append((time.perf_counter() - t0) * 1000.0)
        prev = down
        time.sleep(0.001)
    print("PRESSES=%d" % len(durs), flush=True)
    print("DUR_MS " + " ".join("%.0f" % d for d in durs), flush=True)


main()
