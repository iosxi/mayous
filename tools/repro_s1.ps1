# repro_s1.ps1 - 「右を長く保持してから左クリック」だけを最小再現する
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build 'mayous-debug.exe') (Join-Path $test 'mayous.exe')

@"
[General]
Enabled=1
DragThreshold=8
LeftHoldTimeoutMs=200
RightHoldTimeoutMs=0
SuspendOnFullscreen=0
[Chords]
RightThenLeft=f14
RightThenMiddle=none
RightThenWheelUp=hwheel_right
RightThenWheelDown=hwheel_left
LeftThenRight=f13
LeftThenMiddle=none
LeftThenWheelUp=none
LeftThenWheelDown=none
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $test 'mayous.ini') -Encoding ASCII

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class R1 {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    static void One(uint f) {
        INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f;
        if (SendInput(1, a, Marshal.SizeOf(typeof(INPUT))) != 1)
            throw new Exception("SendInput failed");
    }
    public static void LDown(){One(0x0002);} public static void LUp(){One(0x0004);}
    public static void RDown(){One(0x0008);} public static void RUp(){One(0x0010);}
}
'@

Remove-Item "$env:TEMP\mayous_debug.log" -ErrorAction SilentlyContinue
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
Start-Sleep -Milliseconds 1500
[R1]::SetCursorPos(500,500) | Out-Null
Start-Sleep -Milliseconds 500

Write-Host '右を押して 3 秒保持 -> 左クリック -> 右を離す'
[R1]::RDown()
Start-Sleep -Milliseconds 3000
[R1]::LDown(); Start-Sleep -Milliseconds 60; [R1]::LUp()
Start-Sleep -Milliseconds 200
[R1]::RUp()
Start-Sleep -Milliseconds 800

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Milliseconds 600
if (-not $may.HasExited) { $may.Kill() }
Write-Host '--- mayous_debug.log ---'
Get-Content "$env:TEMP\mayous_debug.log"
