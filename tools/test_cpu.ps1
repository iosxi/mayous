# test_cpu.ps1 - マウス移動を大量に流したときの mayous の CPU 時間
#   Raw Input を常時登録した影響を見る。移動はフックがどのみち全部受けている。
param([string]$Exe = '')
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'cputest'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MM {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
    static void One(uint f, int dx, int dy){ INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f; a[0].mi.dx = dx; a[0].mi.dy = dy; SendInput(1, a, Marshal.SizeOf(typeof(INPUT))); }
    public static void Move(int dx,int dy){ One(0x0001,dx,dy); }
}
'@

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
$src = if ($Exe) { $Exe } else { Join-Path $build 'mayous.exe' }
Copy-Item $src (Join-Path $test 'mayous.exe')
"[General]`nEnabled=1`nSuspendOnFullscreen=0`n[Chords]`nRightThenLeft=f14`n" |
    Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
$p = Start-Process (Join-Path $test 'mayous.exe') -PassThru
Start-Sleep -Seconds 2

$p.Refresh(); $idle0 = $p.TotalProcessorTime
Start-Sleep -Seconds 5
$p.Refresh(); $idle1 = $p.TotalProcessorTime
$idleMs = ($idle1 - $idle0).TotalMilliseconds

$p.Refresh(); $t0 = $p.TotalProcessorTime
$sw = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt 4000; $i++) { [MM]::Move(1,0); [MM]::Move(-1,0) }
$sw.Stop()
Start-Sleep -Milliseconds 700
$p.Refresh(); $t1 = $p.TotalProcessorTime
$busyMs = ($t1 - $t0).TotalMilliseconds

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Milliseconds 600
if (-not $p.HasExited) { $p.Kill() }

Write-Host ("  待機 5 秒の CPU 時間 : {0,7:N1} ms" -f $idleMs)
Write-Host ("  移動 8000 回の CPU 時間: {0,7:N1} ms  (実時間 {1:N0} ms)" -f $busyMs, $sw.ElapsedMilliseconds)
