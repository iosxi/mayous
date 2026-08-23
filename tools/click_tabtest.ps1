# click_tabtest.ps1 - tabtest.exe のタブを実際にクリックして落ちるか見る
param([string]$Exe = 'tabtest_v6.exe', [string]$Args2 = '')

$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class TT {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, ref RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
  [StructLayout(LayoutKind.Sequential)] struct MI { public int dx,dy; public uint data,flags,time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public MI mi; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  static void One(uint f){ INPUT[] a=new INPUT[1]; a[0].mi.flags=f; SendInput(1,a,Marshal.SizeOf(typeof(INPUT))); }
  public static void Click(){ One(0x0002); System.Threading.Thread.Sleep(70); One(0x0004); }
}
'@

$build = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
$p = if ($Args2) { Start-Process (Join-Path $build $Exe) -ArgumentList $Args2 -PassThru } else { Start-Process (Join-Path $build $Exe) -PassThru }
Start-Sleep -Seconds 2

$script:hit = [IntPtr]::Zero; $script:tp = [uint32]$p.Id
$cb = [TT+EnumProc]{ param($h,$q)
  $z = 0; [TT]::GetWindowThreadProcessId($h,[ref]$z) | Out-Null
  if ($z -eq $script:tp) {
    $sb = New-Object Text.StringBuilder 256; [TT]::GetClassNameW($h,$sb,256) | Out-Null
    if ($sb.ToString() -eq 'TabTestWnd') { $script:hit = $h; return $false } }
  return $true }
[TT]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
if ($script:hit -eq [IntPtr]::Zero) { throw 'tabtest のウィンドウが見つかりません' }

[TT]::SetForegroundWindow($script:hit) | Out-Null
$tab = [TT]::GetDlgItem($script:hit, 900)
foreach ($idx in 1,2,3,0) {
    $tr = New-Object TT+RECT
    [TT]::SendMessage($tab, 0x130A, [IntPtr]$idx, [ref]$tr) | Out-Null
    $pt = New-Object TT+POINT
    $pt.X = [int](($tr.L+$tr.R)/2); $pt.Y = [int](($tr.T+$tr.B)/2)
    [TT]::ClientToScreen($tab, [ref]$pt) | Out-Null
    [TT]::SetCursorPos($pt.X, $pt.Y) | Out-Null
    Start-Sleep -Milliseconds 250
    [TT]::Click()
    Start-Sleep -Milliseconds 500
    $p.Refresh()
    if ($p.HasExited) { Write-Host ("  タブ {0} のクリックで落ちた" -f $idx); break }
    Write-Host ("  タブ {0} クリック OK" -f $idx)
}
$p.Refresh()
Write-Host ("{0}: 生存={1}" -f $Exe, (-not $p.HasExited))
if (-not $p.HasExited) { $p.Kill() }
