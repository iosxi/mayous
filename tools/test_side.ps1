# test_side.ps1 - サイドボタンの同時押し・単独クリック・素通しを検証する
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$tlog  = Join-Path $build 'side_target.log'
$plog  = Join-Path $build 'side_probe.log'

if (-not (Test-Path (Join-Path $build 'probe.exe'))) {
    & gcc -O2 -std=gnu11 -Wall -mconsole (Join-Path $PSScriptRoot 'probe.c') `
          -o (Join-Path $build 'probe.exe') -luser32
}
if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $test

@"
[General]
Enabled=1
DragThreshold=8
LeftHoldTimeoutMs=200
RightHoldTimeoutMs=0
Side1HoldTimeoutMs=0
Side2HoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
RightThenLeft=none
LeftThenRight=none
RightThenWheelUp=none
RightThenWheelDown=none
Side1ThenLeft=f13
Side1ThenWheelUp=hwheel_right
Side1ThenWheelDown=hwheel_left
Side2ThenRight=f15, f16
[Single]
Side1Alone=f14
Side2Alone=passthru
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class S {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint flags, uint data) {
        INPUT[] a = new INPUT[1];
        a[0].mi.dwFlags = flags; a[0].mi.mouseData = data;
        if (SendInput(1, a, Marshal.SizeOf(typeof(INPUT))) != 1)
            throw new Exception("SendInput failed " + Marshal.GetLastWin32Error());
    }
    public static void LDown(){One(0x0002,0);}  public static void LUp(){One(0x0004,0);}
    public static void RDown(){One(0x0008,0);}  public static void RUp(){One(0x0010,0);}
    public static void X1Down(){One(0x0080,1);} public static void X1Up(){One(0x0100,1);}
    public static void X2Down(){One(0x0080,2);} public static void X2Up(){One(0x0100,2);}
    public static void Wheel(int d){One(0x0800,(uint)d);}
}
'@
function W([int]$ms) { Start-Sleep -Milliseconds $ms }

$tgt   = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$tlog`"", '90' -PassThru
W 1000
$probe = Start-Process (Join-Path $build 'probe.exe') -ArgumentList "`"$plog`"", '90' -PassThru -WindowStyle Hidden
W 800
$may   = Start-Process (Join-Path $test 'mayous.exe') -PassThru
W 1500
[S]::SetCursorPos(520, 420) | Out-Null
W 400

Write-Host '  T1 サイド1 + 左クリック  -> F13 が出るはず'
[S]::X1Down(); W 80; [S]::LDown(); W 60; [S]::LUp(); W 100; [S]::X1Up(); W 700

Write-Host '  T2 サイド1 単独          -> F14 が出るはず(本来のサイドボタンは出ない)'
[S]::X1Down(); W 80; [S]::X1Up(); W 700

Write-Host '  T3 サイド1 + ホイール    -> 水平ホイールが出るはず'
[S]::X1Down(); W 80; [S]::Wheel(120); W 80; [S]::Wheel(-120); W 80; [S]::X1Up(); W 700

Write-Host '  T4 サイド2 + 右クリック  -> F15,F16 の2ステップが出るはず'
[S]::X2Down(); W 80; [S]::RDown(); W 60; [S]::RUp(); W 100; [S]::X2Up(); W 700

Write-Host '  T5 サイド2 単独          -> passthru なので本物のサイド2が届くはず'
[S]::X2Down(); W 80; [S]::X2Up(); W 700

Write-Host '  T6 素の左クリック / 右クリック (割り当て無し = 素通し)'
[S]::LDown(); W 60; [S]::LUp(); W 400
[S]::RDown(); W 60; [S]::RUp(); W 700

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited)   { $may.Kill() }
if (-not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
W 300
if (-not $tgt.HasExited)   { $tgt.CloseMainWindow() | Out-Null; W 700 }
if (-not $tgt.HasExited)   { $tgt.Kill() }
W 400

Write-Host ''
Write-Host '===== アプリが受け取ったマウス ====='
Get-Content $tlog | ForEach-Object { Write-Host ('  ' + $_) }
Write-Host ''
Write-Host '===== 流れたキー ====='
Get-Content $plog | Where-Object { $_ -match 'KEY|WHEEL_H' } | ForEach-Object { Write-Host ('  ' + $_) }
