# shot_settings.ps1 - 設定ウィンドウを開いて画面キャプチャする(目視確認用)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WS {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int w, int t, uint f);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
'@

function Find-WndByClass([uint32]$procId, [string]$cls) {
  $script:hit = [IntPtr]::Zero
  $cb = [WS+EnumProc]{ param($h,$p)
    $q = 0; [WS]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
    if ($q -eq $script:targetPid) {
      $sb = New-Object Text.StringBuilder 256
      [WS]::GetClassNameW($h,$sb,256) | Out-Null
      if ($sb.ToString() -eq $script:targetCls) { $script:hit = $h; return $false }
    }
    return $true }
  $script:targetPid = $procId
  $script:targetCls = $cls
  [WS]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:hit
}

$proc = Get-Process mayous -ErrorAction SilentlyContinue |
        Where-Object { (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -notmatch 'wheel-agent' } |
        Select-Object -First 1
if (-not $proc) { throw 'mayous が起動していません。' }

$tray = Find-WndByClass ([uint32]$proc.Id) 'MayousHiddenWnd'
if ($tray -eq [IntPtr]::Zero) { throw 'トレイ用ウィンドウが見つかりません。' }

[WS]::PostMessage($tray, 0x0111, [IntPtr]1000, [IntPtr]0) | Out-Null   # WM_COMMAND / IDM_SETTINGS
Start-Sleep -Milliseconds 1500

$sw = Find-WndByClass ([uint32]$proc.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウが開きませんでした。' }
[WS]::SetForegroundWindow($sw) | Out-Null
Start-Sleep -Milliseconds 700

$r = New-Object WS+RECT
[WS]::GetWindowRect($sw, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
Write-Host ("設定ウィンドウ {0} x {1}  at ({2},{3})" -f $w, $h, $r.L, $r.T)

# HWND_TOP に持ち上げてから PrintWindow で中身を直接描かせる。
# CopyFromScreen だと手前に別ウィンドウがあると真っ黒になる。
[WS]::SetWindowPos($sw, [IntPtr]0, 0, 0, 0, 0, 0x0043) | Out-Null   # NOSIZE|NOMOVE|SHOWWINDOW
Start-Sleep -Milliseconds 500
$bmp = New-Object System.Drawing.Bitmap -ArgumentList $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$hdc = $g.GetHdc()
[WS]::PrintWindow($sw, $hdc, 2) | Out-Null       # PW_RENDERFULLCONTENT
$g.ReleaseHdc($hdc)
$out = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\settings.png'
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host ("保存: {0}" -f $out)
