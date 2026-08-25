# test_hold_release.ps1 - hold: で押さえたキーが、異常な終わり方でも必ず離されるか
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dir   = Join-Path $build 'holdtest'
$log   = Join-Path $build 'stuck.log'

if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
New-Item -ItemType Directory -Path $dir | Out-Null
Copy-Item (Join-Path $build 'mayous.exe') $dir
@"
[General]
Enabled=1
SuspendOnFullscreen=0
[Chords]
RightThenMiddle=hold:a
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $dir 'mayous.ini') -Encoding ASCII

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class SK {
  [StructLayout(LayoutKind.Sequential)] struct MI { public int dx,dy; public uint data,flags,time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct IN { public uint type; public MI mi; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, IN[] p, int cb);
  static void One(uint f){ IN[] a=new IN[1]; a[0].mi.flags=f; SendInput(1,a,Marshal.SizeOf(typeof(IN))); }
  public static void RDown(){One(0x0008);} public static void RUp(){One(0x0010);}
  public static void MDown(){One(0x0020);} public static void MUp(){One(0x0040);}
}
'@

$w = Start-Process (Join-Path $build 'pollwatch.exe') `
     -ArgumentList "`"$log`"", '14', '41', '8' -PassThru -WindowStyle Hidden
Start-Sleep -Milliseconds 900
$m = Start-Process (Join-Path $dir 'mayous.exe') -PassThru
Start-Sleep -Seconds 2

Write-Host '右押し + 中クリックで "a" を押しっぱなしにする'
[SK]::RDown(); Start-Sleep -Milliseconds 100
[SK]::MDown(); Start-Sleep -Milliseconds 60; [SK]::MUp()
Start-Sleep -Milliseconds 800

Write-Host '右ボタンを離さないまま mayous を終了させる'
Start-Process (Join-Path $dir 'mayous.exe') -ArgumentList '--exit' -Wait
Start-Sleep -Seconds 2
[SK]::RUp()
Start-Sleep -Seconds 3

if (-not $m.HasExited) { $m.Kill() }
if (-not $w.HasExited) { Stop-Process -Id $w.Id -Force }
Start-Sleep -Milliseconds 500

Write-Host ''
Write-Host '===== 観測 ====='
Get-Content $log | ForEach-Object { Write-Host ('  ' + $_) }
$last = (Get-Content $log) | Where-Object { $_ -match 'POLL' } | Select-Object -Last 1
if ($last -match 'UP') { Write-Host ''; Write-Host '  終了後にキーは離されています(押しっぱなしの取り残しなし)' }
else { Write-Host ''; Write-Host '  !! キーが押されたまま残りました' }
