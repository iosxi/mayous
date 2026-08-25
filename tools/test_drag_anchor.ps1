# test_drag_anchor.ps1 - 保留した押下をドラッグで世に出すとき、
#   「押した位置」で押下が届くかを確認する。
#   ここがずれると、ウィンドウの枠のような細い当たり判定を掴み損ねる。

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$tlog  = Join-Path $build 'drag.log'

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test
@"
[General]
Enabled=1
DragThreshold=8
LeftHoldTimeoutMs=200
RightHoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
LeftThenRight=f13
RightThenLeft=win
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class DA {
    [StructLayout(LayoutKind.Sequential)]
    struct MI { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)]
    struct IN { public uint type; public MI mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, IN[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint f, int dx, int dy) {
        IN[] a = new IN[1]; a[0].mi.flags = f; a[0].mi.dx = dx; a[0].mi.dy = dy;
        if (SendInput(1, a, Marshal.SizeOf(typeof(IN))) != 1) throw new Exception("SendInput failed");
    }
    public static void LDown(){One(0x0002,0,0);} public static void LUp(){One(0x0004,0,0);}
    public static void Move(int dx,int dy){One(0x0001,dx,dy);}
}
'@
function W([int]$ms) { Start-Sleep -Milliseconds $ms }

$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '40' -PassThru
W 1200
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1500
[DA]::SetCursorPos(520, 420) | Out-Null
W 500

Write-Host '左ボタンを乗っ取っている状態で、押してすぐ右へ 60px 動かす'
Write-Host '  (押した画面座標 520,420 = ウィンドウ内 312,189 のはず)'
[DA]::LDown(); W 20
[DA]::Move(30, 0); W 30
[DA]::Move(30, 0); W 30
W 200
[DA]::LUp()
W 800

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited) { $may.Kill() }
W 200
if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; W 700 }
if (-not $tgt.HasExited) { $tgt.Kill() }
W 400

Write-Host ''
Write-Host '===== アプリが受け取ったもの ====='
Get-Content $tlog | ForEach-Object { Write-Host ('  ' + $_) }
Write-Host ''
$down = (Get-Content $tlog) | Where-Object { $_ -match 'LEFT\s+DOWN' } | Select-Object -First 1
if ($down -match 'at\(\s*(\d+),\s*(\d+)\)') {
    $x = [int]$matches[1]
    Write-Host ("押下が届いた x = {0}  (押した位置 312 と一致していれば成功)" -f $x)
    if ([Math]::Abs($x - 312) -le 2) { Write-Host '  -> 押した位置で掴めています' }
    else { Write-Host ('  -> {0}px ずれています' -f ($x - 312)) }
}
