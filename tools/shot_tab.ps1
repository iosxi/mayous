# shot_tab.ps1 - 設定ウィンドウの指定タブをクリックで開いてキャプチャする
param([int]$TabIndex = 2, [string]$Out = 'settings_tab.png', [string]$ProcName = 'mayous', [switch]$ClickOnly)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WT {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr a, int x, int y, int w, int t, uint f);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, ref RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll", EntryPoint="PostMessageW")] public static extern bool PostMessage2(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X,Y; }
  [StructLayout(LayoutKind.Sequential)] struct MI { public int dx,dy; public uint data,flags,time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public MI mi; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  static void One(uint f){ INPUT[] a=new INPUT[1]; a[0].mi.flags=f; SendInput(1,a,Marshal.SizeOf(typeof(INPUT))); }
  public static void Click(){ One(0x0002); System.Threading.Thread.Sleep(60); One(0x0004); }
}
'@

function Find-Wnd([uint32]$procId, [string]$cls) {
  $script:hit = [IntPtr]::Zero; $script:tp = $procId; $script:tc = $cls
  $cb = [WT+EnumProc]{ param($h,$p)
    $q = 0; [WT]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
    if ($q -eq $script:tp) {
      $sb = New-Object Text.StringBuilder 256; [WT]::GetClassNameW($h,$sb,256) | Out-Null
      if ($sb.ToString() -eq $script:tc) { $script:hit = $h; return $false } }
    return $true }
  [WT]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:hit
}

$proc = Get-Process $ProcName | Where-Object {
    (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -notmatch 'wheel-agent' } | Select-Object -First 1
$sw = Find-Wnd ([uint32]$proc.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) {
    # まだ開いていなければトレイ窓に設定コマンドを送って開く
    $tray = Find-Wnd ([uint32]$proc.Id) 'MayousHiddenWnd'
    if ($tray -eq [IntPtr]::Zero) { throw 'mayous のウィンドウが見つかりません。' }
    [WT]::PostMessage2($tray, 0x0111, [IntPtr]1000, [IntPtr]0) | Out-Null
    Start-Sleep -Seconds 2
    $sw = Find-Wnd ([uint32]$proc.Id) 'MayousSettingsWnd'
    if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウが開きませんでした。' }
}

[WT]::SetWindowPos($sw, [IntPtr]0, 0,0,0,0, 0x0043) | Out-Null
Start-Sleep -Milliseconds 400

# タブの矩形を TCM_GETITEMRECT で取り、その中央を実際にクリックする
$tab = [WT]::GetDlgItem($sw, 900)
$tr = New-Object WT+RECT
[WT]::SendMessage($tab, 0x130A, [IntPtr]$TabIndex, [ref]$tr) | Out-Null   # TCM_GETITEMRECT
$pt = New-Object WT+POINT
$pt.X = [int](($tr.L + $tr.R) / 2); $pt.Y = [int](($tr.T + $tr.B) / 2)
[WT]::ClientToScreen($tab, [ref]$pt) | Out-Null
[WT]::SetCursorPos($pt.X, $pt.Y) | Out-Null
Start-Sleep -Milliseconds 250
[WT]::Click()
Start-Sleep -Milliseconds 700

if ($ClickOnly) { Write-Host 'クリックのみ実行しました。'; exit }
$r = New-Object WT+RECT
if (-not [WT]::GetWindowRect($sw, [ref]$r)) { throw 'ウィンドウが消えました(クラッシュの可能性)。' }
$w = $r.R - $r.L; $h = $r.B - $r.T
if ($w -le 0 -or $h -le 0) { throw ('ウィンドウ矩形が不正: {0}x{1}' -f $w,$h) }
$bmp = New-Object System.Drawing.Bitmap -ArgumentList $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[WT]::PrintWindow($sw, $hdc, 2) | Out-Null
$g.ReleaseHdc($hdc)
$path = Join-Path (Split-Path -Parent $PSScriptRoot) ('build\' + $Out)
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host ("タブ {0} を保存: {1}" -f $TabIndex, $path)
