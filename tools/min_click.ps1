# min_click.ps1 - 素の単クリックだけを 3 回打ち、両ログを突き合わせる最小テスト
param([string]$Exe = 'mayous-debug.exe')
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$test  = Join-Path $build 'test'
$log   = Join-Path $build 'minclick.log'

if (Test-Path $test) { Remove-Item $test -Recurse -Force }
New-Item -ItemType Directory -Path $test | Out-Null
Copy-Item (Join-Path $build $Exe) (Join-Path $test 'mayous.exe')
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
public static class C1 {
    [StructLayout(LayoutKind.Sequential)]
    struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
    [StructLayout(LayoutKind.Sequential)]
    struct INPUT { public uint type; public MOUSEINPUT mi; }
    [DllImport("user32.dll", SetLastError = true)]
    static extern uint SendInput(uint n, INPUT[] p, int cb);
    [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
    [DllImport("kernel32.dll")] public static extern uint GetTickCount();
    static void One(uint f) {
        INPUT[] a = new INPUT[1]; a[0].mi.dwFlags = f;
        if (SendInput(1, a, Marshal.SizeOf(typeof(INPUT))) != 1) throw new Exception("SendInput failed");
    }
    public static void LDown(){One(0x0002);} public static void LUp(){One(0x0004);}
    public static void RDown(){One(0x0008);} public static void RUp(){One(0x0010);}
}
'@

Remove-Item "$env:TEMP\mayous_debug.log" -ErrorAction SilentlyContinue
$tgt = Start-Process (Join-Path $build 'target.exe') -ArgumentList "`"$log`"", '40' -PassThru
Start-Sleep -Milliseconds 1200
$may = Start-Process (Join-Path $test 'mayous.exe') -PassThru
Start-Sleep -Milliseconds 1500
[C1]::SetCursorPos(520, 420) | Out-Null
Start-Sleep -Milliseconds 500

for ($i=1; $i -le 3; $i++) {
    Write-Host ("  左クリック {0}  (注入 tick={1})" -f $i, [C1]::GetTickCount())
    [C1]::LDown(); Start-Sleep -Milliseconds 60; [C1]::LUp()
    Start-Sleep -Milliseconds 900
}
Write-Host ("  右クリック    (注入 tick={0})" -f [C1]::GetTickCount())
[C1]::RDown(); Start-Sleep -Milliseconds 60; [C1]::RUp()
Start-Sleep -Milliseconds 900

Start-Process (Join-Path $test 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Milliseconds 600
if (-not $may.HasExited) { $may.Kill() }
if (-not $tgt.HasExited) { $tgt.CloseMainWindow() | Out-Null; Start-Sleep -Milliseconds 600 }
if (-not $tgt.HasExited) { $tgt.Kill() }
Start-Sleep -Milliseconds 300

Write-Host ''
Write-Host '=== 時系列(同じ GetTickCount で突き合わせ) ==='
$rows = @()
foreach ($l in Get-Content $log) {
    if ($l -match '^\s*(\d+)\s+\[.*?\]\s*(.*)$') { $rows += [pscustomobject]@{ t=[uint32]$matches[1]; who='APP   '; msg=$matches[2].Trim() } }
}
foreach ($l in (Get-Content "$env:TEMP\mayous_debug.log" -ErrorAction SilentlyContinue)) {
    if ($l -match '^\s*(\d+)\s+(.*)$') { $rows += [pscustomobject]@{ t=[uint32]$matches[1]; who='mayous'; msg=$matches[2].Trim() } }
}
$rows | Sort-Object t | ForEach-Object { Write-Host ("  {0}  {1}  {2}" -f $_.t, $_.who, $_.msg) }
