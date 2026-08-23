# test_settings.ps1 - 設定ウィンドウで割り当てを変更し、保存と即時反映を検証する
$ErrorActionPreference = 'Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class T {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
}
'@

function Find-Wnd([uint32]$procId, [string]$cls) {
  $script:hit = [IntPtr]::Zero; $script:tp = $procId; $script:tc = $cls
  $cb = [T+EnumProc]{ param($h,$p)
    $q = 0; [T]::GetWindowThreadProcessId($h, [ref]$q) | Out-Null
    if ($q -eq $script:tp) {
      $sb = New-Object Text.StringBuilder 256; [T]::GetClassNameW($h,$sb,256) | Out-Null
      if ($sb.ToString() -eq $script:tc) { $script:hit = $h; return $false } }
    return $true }
  [T]::EnumWindows($cb, [IntPtr]::Zero) | Out-Null
  return $script:hit
}

$ini = 'c:\projects\mayous\build\dist\mayous.ini'
$proc = Get-Process mayous | Where-Object {
    (Get-CimInstance Win32_Process -Filter "ProcessId=$($_.Id)").CommandLine -notmatch 'wheel-agent' } | Select-Object -First 1
if (-not $proc) { throw 'mayous が起動していません。' }

Write-Host '--- 変更前の [Chords] ---'
(Get-Content $ini) | Where-Object { $_ -match '^(Right|Left)Then' } | ForEach-Object { Write-Host ('  ' + $_) }

$tray = Find-Wnd ([uint32]$proc.Id) 'MayousHiddenWnd'
[T]::PostMessage($tray, 0x0111, [IntPtr]1000, [IntPtr]0) | Out-Null   # 設定を開く
Start-Sleep -Milliseconds 1500
$sw = Find-Wnd ([uint32]$proc.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウが開きません。' }

# CH_R_M = 1 -> コントロール ID 1001 (右押し + 中クリック)
$combo = [T]::GetDlgItem($sw, 1001)
$CB_SELECTSTRING = 0x014D
[T]::SendMessageW($combo, $CB_SELECTSTRING, [IntPtr](-1), 'コピー (Ctrl+C)') | Out-Null

# CH_L_WUP = 6 -> 1006 (左押し + ホイール上) には一覧に無い指定を手入力する
$combo2 = [T]::GetDlgItem($sw, 1006)
$WM_SETTEXT = 0x000C
[T]::SendMessageW($combo2, $WM_SETTEXT, [IntPtr]0, 'ctrl+shift+f9') | Out-Null

$sb = New-Object Text.StringBuilder 128
[T]::GetWindowTextW($combo, $sb, 128) | Out-Null
Write-Host ("右押し+中クリック に選んだ表示: {0}" -f $sb.ToString())

Start-Sleep -Milliseconds 300
[T]::PostMessage($sw, 0x0111, [IntPtr]1130, [IntPtr]0) | Out-Null      # OK
Start-Sleep -Milliseconds 1500

Write-Host '--- 変更後の [Chords] ---'
(Get-Content $ini) | Where-Object { $_ -match '^(Right|Left)Then' } | ForEach-Object { Write-Host ('  ' + $_) }

$sw2 = Find-Wnd ([uint32]$proc.Id) 'MayousSettingsWnd'
Write-Host ("OK で閉じたか: {0}" -f ($sw2 -eq [IntPtr]::Zero))
Write-Host ("mayous は生存: {0}" -f (-not $proc.HasExited))
