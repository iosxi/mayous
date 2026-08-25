# test_left_latency.ps1 - 既定設定で左クリックが素通しか(遅延ゼロか)を実測する
#
#   本物のウィンドウ(target.exe)に左クリックを注入し、
#   注入した時刻とアプリが受け取った時刻の差を測る。
#   左ボタンを乗っ取っていなければ、押下は即座に届き、
#   ログ上も [mayous injected] ではなく素の [injected] になる。

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$tlog  = Join-Path $build 'left.log'

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test
# ini を作らせない = 既定値そのままで動かす

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class L2 {
    [StructLayout(LayoutKind.Sequential)]
    struct MI { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)]
    struct IN { public uint type; public MI mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, IN[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("kernel32.dll")] public static extern uint GetTickCount();
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
[L2]::SetCursorPos(520, 420) | Out-Null
W 400

Write-Host '左クリックを 3 回。押した時刻を記録する。'
$stamps = @()
for ($i = 0; $i -lt 3; $i++) {
    $t = [L2]::GetTickCount()
    [L2]::LDown(); W 50; [L2]::LUp()
    $stamps += $t
    Write-Host ("  {0} 回目 押下 tick={1}" -f ($i+1), $t)
    W 700
}

Write-Host ''
Write-Host '押してすぐ動かす(枠を掴む操作の再現)'
$tDrag = [L2]::GetTickCount()
[L2]::LDown(); W 20
[L2]::Move(30, 0); W 30; [L2]::Move(30, 0); W 30
[L2]::LUp()
Write-Host ("  ドラッグ開始 tick={0}" -f $tDrag)
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
$lines = Get-Content $tlog
$lines | ForEach-Object { Write-Host ('  ' + $_) }

Write-Host ''
Write-Host '===== 遅延 ====='
$downs = @()
foreach ($l in $lines) {
    if ($l -match '^\s*(\d+)\s+\[.*?\]\s+LEFT\s+DOWN') { $downs += [uint32]$matches[1] }
}
for ($i = 0; $i -lt [Math]::Min($stamps.Count, $downs.Count); $i++) {
    Write-Host ("  {0} 回目: 注入 {1} -> アプリ受信 {2}   遅延 {3} ms" -f ($i+1), $stamps[$i], $downs[$i], ($downs[$i] - $stamps[$i]))
}
