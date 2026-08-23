# debug_tab.ps1 - 設定ウィンドウのタブをクリックさせ、落ちたら gdb でスタックを取る
$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$cmds  = Join-Path $build 'gdb.txt'
$out   = Join-Path $build 'gdb.log'

@"
set pagination off
set confirm off
run
bt 30
info registers rip
quit
"@ | Set-Content -Path $cmds -Encoding ASCII

# 別プロセスから、設定を開く -> タブ2をクリック する係を先に仕込む
$driver = @'
Add-Type @"
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class D {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
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
"@
function FW([uint32]$pid2,[string]$cls) {
  $script:hit=[IntPtr]::Zero; $script:tp=$pid2; $script:tc=$cls
  $cb=[D+EnumProc]{ param($h,$p)
    $q=0;[D]::GetWindowThreadProcessId($h,[ref]$q)|Out-Null
    if($q -eq $script:tp){$sb=New-Object Text.StringBuilder 256;[D]::GetClassNameW($h,$sb,256)|Out-Null
      if($sb.ToString() -eq $script:tc){$script:hit=$h;return $false}}
    return $true}
  [D]::EnumWindows($cb,[IntPtr]::Zero)|Out-Null
  return $script:hit
}
Start-Sleep -Seconds 6
$p = Get-Process mayous-debug -ErrorAction SilentlyContinue | Where-Object { (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -notmatch 'wheel-agent' } | Select-Object -First 1
if (-not $p) { Write-Host 'driver: mayous-debug が見つからない'; exit }
$tray = FW ([uint32]$p.Id) 'MayousHiddenWnd'
[D]::PostMessage($tray, 0x0111, [IntPtr]1000, [IntPtr]0) | Out-Null
Start-Sleep -Seconds 2
$sw = FW ([uint32]$p.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) { Write-Host 'driver: 設定窓なし'; exit }
[D]::SetForegroundWindow($sw) | Out-Null
$tab = [D]::GetDlgItem($sw, 900)
$tr = New-Object D+RECT
[D]::SendMessage($tab, 0x130A, [IntPtr]2, [ref]$tr) | Out-Null
$pt = New-Object D+POINT
$pt.X=[int](($tr.L+$tr.R)/2); $pt.Y=[int](($tr.T+$tr.B)/2)
[D]::ClientToScreen($tab,[ref]$pt)|Out-Null
Write-Host ("driver: タブ2 を ({0},{1}) でクリック" -f $pt.X,$pt.Y)
[D]::SetCursorPos($pt.X,$pt.Y)|Out-Null
Start-Sleep -Milliseconds 300
[D]::Click()
'@
$driverPath = Join-Path $build 'driver.ps1'
$driver | Set-Content -Path $driverPath -Encoding UTF8

Start-Process powershell -ArgumentList '-NoProfile','-ExecutionPolicy','Bypass','-File',$driverPath -WindowStyle Hidden
& gdb --batch -x $cmds (Join-Path $build 'mayous-debug.exe') 2>&1 | Tee-Object -FilePath $out
