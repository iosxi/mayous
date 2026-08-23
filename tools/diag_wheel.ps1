$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$log   = Join-Path $build 'wheel.log'

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Inj3 {
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
    public static void RDown() { One(0x0008, 0); }
    public static void RUp()   { One(0x0010, 0); }
    public static void Wheel(int d) { One(0x0800, (uint)d); }
}
'@

$probe = Start-Process -FilePath (Join-Path $build 'probe.exe') `
                       -ArgumentList "`"$log`"", '25' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 800
$may = Start-Process -FilePath (Join-Path $test 'mayous.exe') -PassThru
Start-Sleep -Milliseconds 1500
[Inj3]::SetCursorPos(400, 400) | Out-Null
Start-Sleep -Milliseconds 300

Write-Host 'A: 右押し中に上上下下 (300ms 間隔)'
[Inj3]::RDown()
Start-Sleep -Milliseconds 300
[Inj3]::Wheel(120);  Start-Sleep -Milliseconds 300
[Inj3]::Wheel(120);  Start-Sleep -Milliseconds 300
[Inj3]::Wheel(-120); Start-Sleep -Milliseconds 300
[Inj3]::Wheel(-120); Start-Sleep -Milliseconds 300
[Inj3]::RUp()
Start-Sleep -Milliseconds 800

Write-Host 'B: 右を押さずに上下 (素通しの確認)'
[Inj3]::Wheel(120);  Start-Sleep -Milliseconds 300
[Inj3]::Wheel(-120); Start-Sleep -Milliseconds 800

Start-Process -FilePath (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Milliseconds 500
if (-not $may.HasExited) { $may.Kill() }
if (-not $probe.HasExited) { Stop-Process -Id $probe.Id -Force }
Start-Sleep -Milliseconds 300
Write-Host '--- wheel.log ---'
Get-Content $log | Where-Object { $_ -notmatch 'alive' }
