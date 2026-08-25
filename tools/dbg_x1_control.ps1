# dbg_x1_control.ps1 - mayous を一切動かさずにサイドボタン1 を押す対照実験
#   併用ツール(X-Mouse / LG HUB)がサイドボタンを別のボタンへ差し替えていないかを見る
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$tlog  = Join-Path $build 'x1control.log'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MC {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint f, uint data){ INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f; a[0].mi.mouseData = data; SendInput(1, a, Marshal.SizeOf(typeof(INPUT))); }
    public static void X1Down(){ One(0x0080,1); } public static void X1Up(){ One(0x0100,1); }
}
'@
function W([int]$ms) { Start-Sleep -Milliseconds $ms }

Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
W 1500
$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '10' -PassThru
W 1200
[MC]::SetCursorPos(520, 420) | Out-Null
W 400
Write-Host 'mayous なしでサイドボタン1 を 2 回クリック'
for ($i = 0; $i -lt 2; $i++) { [MC]::X1Down(); W 80; [MC]::X1Up(); W 400 }
W 1000
if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; W 800 }
if (-not $tgt.HasExited) { $tgt.Kill() }
W 300
Write-Host ''
Write-Host '===== アプリが受け取ったもの (mayous なし) ====='
Get-Content $tlog | ForEach-Object { Write-Host ('  ' + $_) }
