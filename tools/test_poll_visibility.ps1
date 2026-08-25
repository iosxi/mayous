# test_poll_visibility.ps1 - 注入したキーが「ポーリング方式」のアプリから見えるか
#
#   zoom-pon のようにキーボードフックを張らず GetAsyncKeyState を
#   一定間隔で見に行くアプリは、押下時間が短すぎるキーを拾えない。
#   フックから見た回数とポーリングから見た回数を並べて比較する。

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$plog  = Join-Path $build 'poll.log'

if (-not (Test-Path (Join-Path $build 'pollwatch.exe'))) {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'pollwatch.c') `
          -o (Join-Path $build 'pollwatch.exe') -luser32
    if ($LASTEXITCODE -ne 0) { throw 'pollwatch のビルドに失敗' }
}

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test
@"
[General]
Enabled=1
SuspendOnFullscreen=0
[Chords]
RightThenLeft=a
RightThenMiddle=hold:a
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class PV {
    [StructLayout(LayoutKind.Sequential)]
    struct MI { public int dx, dy; public uint data, flags, time; public IntPtr extra; }
    [StructLayout(LayoutKind.Sequential)]
    struct IN { public uint type; public MI mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, IN[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint f){ IN[] a=new IN[1]; a[0].mi.flags=f;
        if (SendInput(1,a,Marshal.SizeOf(typeof(IN)))!=1) throw new Exception("fail"); }
    public static void RDown(){One(0x0008);} public static void RUp(){One(0x0010);}
    public static void LDown(){One(0x0002);} public static void LUp(){One(0x0004);}
    public static void MDown(){One(0x0020);} public static void MUp(){One(0x0040);}
}
'@
function W([int]$ms) { Start-Sleep -Milliseconds $ms }

# 'A' = 0x41 を 8ms 間隔で監視(zoom-pon と同じ間隔)
$watch = Start-Process (Join-Path $build 'pollwatch.exe') `
         -ArgumentList "`"$plog`"", '14', '41', '8' -PassThru -WindowStyle Hidden
W 900
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1500
[PV]::SetCursorPos(700, 500) | Out-Null
W 300

Write-Host '(1) 右押し + 左クリック ("a" を一回叩く) を 3 回'
for ($i = 0; $i -lt 3; $i++) {
    [PV]::RDown(); W 80; [PV]::LDown(); W 60; [PV]::LUp(); W 100; [PV]::RUp()
    W 900
}
W 800
Write-Host '(2) 右押し + 中クリック ("hold:a") -> 右を離すまで押しっぱなしのはず'
[PV]::RDown(); W 80; [PV]::MDown(); W 60; [PV]::MUp()
W 1500                     # ここで 1.5 秒 押しっぱなしが続く
[PV]::RUp()
W 1200

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited) { $may.Kill() }
W 3000
if (-not $watch.HasExited) { Stop-Process -Id $watch.Id -Force }
W 400

Write-Host ''
Write-Host '===== 観測結果 ====='
Get-Content $plog | ForEach-Object { Write-Host ('  ' + $_) }
