# mayous が実際に何ミリ秒キーを押しているかを測る(zoom-pon は使わない)
# -PrefixHoldMs: 同時押し成立後、先に押したボタン(右)を離すまでの時間
param([string]$Action = 'a', [int]$KeyHoldMs = 120, [int]$Shots = 6, [int]$PrefixHoldMs = 100, [string]$Vk = '41')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')

Add-Type @'
using System;using System.Runtime.InteropServices;
public static class ZP2{
 [DllImport("user32.dll")] static extern uint SendInput(uint n, INP[] p, int cb);
 [StructLayout(LayoutKind.Sequential)] struct MI{public int dx,dy;public uint data,flags,time;public IntPtr extra;}
 [StructLayout(LayoutKind.Sequential)] struct INP{public uint type;public MI mi;}
 static void F(uint f){INP[] a=new INP[1];a[0].mi.flags=f;SendInput(1,a,Marshal.SizeOf(typeof(INP)));}
 public static void RDown(){F(0x0008);} public static void RUp(){F(0x0010);}
 public static void LDown(){F(0x0002);} public static void LUp(){F(0x0004);}
}
'@

$mdir = Join-Path $env:TEMP 'mayous-keydur'
Remove-Item $mdir -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $mdir | Out-Null
Copy-Item (Join-Path $PSScriptRoot '..\build\mayous.exe') $mdir
@"
[General]
Enabled=1
SuspendOnFullscreen=0
KeyHoldMs=$KeyHoldMs

[Chords]
RightThenLeft=$Action
"@ | Set-Content -Path (Join-Path $mdir 'mayous.ini') -Encoding ASCII

Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1
Start-Process (Join-Path $mdir 'mayous.exe') | Out-Null
Start-Sleep -Seconds 2

$secs = $Shots * ($PrefixHoldMs / 1000.0 + 1.3) + 3
$job = Start-Process python -ArgumentList @((Join-Path $PSScriptRoot 'keydur.py'), $secs, $Vk) `
        -NoNewWindow -PassThru -RedirectStandardOutput (Join-Path $mdir 'out.txt')
Start-Sleep -Seconds 2

function W([int]$ms){ Start-Sleep -Milliseconds $ms }
for ($i = 1; $i -le $Shots; $i++) {
    [ZP2]::RDown(); W 60; [ZP2]::LDown(); W 40; [ZP2]::LUp()
    $rest = $PrefixHoldMs - 40
    if ($rest -gt 0) { W $rest }
    [ZP2]::RUp()
    W 1000
}
$job.WaitForExit()
Get-Content (Join-Path $mdir 'out.txt') | Where-Object { $_ -match 'PRESSES|DUR_MS' }
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
