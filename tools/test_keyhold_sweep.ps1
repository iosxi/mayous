# test_keyhold_sweep.ps1 - 注入したキーが zoom-pon 形のループから見えるかを数える。
#
#   フック側(pollwatch)とポーリング側(tickmiss)を同時に走らせ、
#   「mayous が本当に発行したか」と「ポーリングで見えたか」を切り分ける。
param([int[]]$Holds = @(40, 200), [int]$Shots = 15)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$dir   = Join-Path $build 'sweep'

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class SW {
  [StructLayout(LayoutKind.Sequential)] struct MI { public int dx,dy; public uint data,flags,time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct IN { public uint type; public MI mi; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, IN[] p, int cb);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  static void One(uint f){ IN[] a=new IN[1]; a[0].mi.flags=f;
      if (SendInput(1,a,Marshal.SizeOf(typeof(IN)))!=1) throw new Exception("fail"); }
  public static void RDown(){One(0x0008);} public static void RUp(){One(0x0010);}
  public static void LDown(){One(0x0002);} public static void LUp(){One(0x0004);}
}
'@
function W([int]$ms) { Start-Sleep -Milliseconds $ms }

foreach ($hold in $Holds) {
    if (Test-Path $dir) { Remove-Item $dir -Recurse -Force }
    New-Item -ItemType Directory -Path $dir | Out-Null
    Copy-Item (Join-Path $build 'mayous.exe') $dir
    @"
[General]
Enabled=1
SuspendOnFullscreen=0
KeyHoldMs=$hold
[Chords]
RightThenLeft=a
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $dir 'mayous.ini') -Encoding ASCII

    $secs  = [int]($Shots * 0.6 + 8)
    $plog  = Join-Path $build ("sweep{0}_poll.txt" -f $hold)
    $hlog  = Join-Path $build ("sweep{0}_hook.log" -f $hold)

    $hook = Start-Process (Join-Path $build 'pollwatch.exe') `
            -ArgumentList "`"$hlog`"", $secs, '41', '4' -PassThru -WindowStyle Hidden
    W 500
    $py = Start-Process 'python' -ArgumentList (Join-Path $PSScriptRoot 'tickmiss.py'), $secs, '41', "`"$plog`"" `
          -PassThru -WindowStyle Hidden
    W 1200
    $may = Start-Process (Join-Path $dir 'mayous.exe') -PassThru
    W 1500
    [SW]::SetCursorPos(700, 500) | Out-Null
    W 300

    for ($i = 0; $i -lt $Shots; $i++) {
        [SW]::RDown(); W 60; [SW]::LDown(); W 40; [SW]::LUp(); W 60; [SW]::RUp()
        W 400
    }
    W 1500

    Start-Process (Join-Path $dir 'mayous.exe') -ArgumentList '--exit' -Wait
    W 600
    if (-not $may.HasExited) { $may.Kill() }
    $py.WaitForExit(20000)   | Out-Null
    $hook.WaitForExit(20000) | Out-Null
    if (-not $py.HasExited)   { $py.Kill() }
    if (-not $hook.HasExited) { Stop-Process -Id $hook.Id -Force }
    W 300

    $seen = 0
    if (Test-Path $plog) {
        $t = Get-Content $plog -Encoding UTF8
        if (($t -join ' ') -match 'SEEN=(\d+)') { $seen = [int]$matches[1] }
        $period = ($t | Where-Object { $_ -match 'PERIOD_MS' })
    }
    $emitted = 0
    if (Test-Path $hlog) {
        $emitted = @(Get-Content $hlog | Where-Object { $_ -match 'HOOK.*DOWN' }).Count
    }
    Write-Host ("KeyHoldMs={0,4}  指示 {1} 回 / mayous が発行 {2} 回 / ポーリングで観測 {3} 回" `
                -f $hold, $Shots, $emitted, $seen)
    if ($period) { Write-Host ('              ' + $period) }
}
