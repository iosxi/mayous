# test_capture.ps1 - [記録] ボタンでキー入力を記録し、割り当てに反映されるか検証する
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class KB {
  [StructLayout(LayoutKind.Sequential)] struct KI { public ushort vk, scan; public uint flags, time; public IntPtr extra; public int pad; }
  [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public KI ki; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, INPUT[] p, int cb);
  static void Key(ushort vk, bool up) {
    INPUT[] a = new INPUT[1];
    a[0].type = 1; a[0].ki.vk = vk; a[0].ki.flags = up ? 2u : 0u;
    if (SendInput(1, a, Marshal.SizeOf(typeof(INPUT))) != 1)
      throw new Exception("SendInput(keyboard) failed " + Marshal.GetLastWin32Error());
  }
  public static void Chord(ushort[] vks) {
    foreach (ushort v in vks) { Key(v, false); System.Threading.Thread.Sleep(40); }
    for (int i = vks.Length - 1; i >= 0; i--) { Key(vks[i], true); System.Threading.Thread.Sleep(40); }
  }
}
'@

$proc = Get-MayousProc
if (-not $proc) { throw 'mayous が起動していません。' }
$sw = Find-ProcWnd ([uint32]$proc.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウが開いていません。' }

# サイドボタン1 (BTN_X1=3) + 左クリック (SUF_L=0) -> CH_ID = 3*7+0 = 21
$chordId = 21
$cmbId   = 2000 + $chordId
$recId   = 2100 + $chordId

$cmb = [UI]::GetDlgItem($sw, $cmbId)
Write-Host ("対象コンボ hwnd={0}" -f $cmb)

Write-Host '[記録] を押します...'
[UI]::Post($sw, 0x0111, [IntPtr]$recId, [IntPtr]0) | Out-Null    # WM_COMMAND
Start-Sleep -Seconds 2

$cap = Find-ProcWnd ([uint32]$proc.Id) 'MayousCaptureWnd'
if ($cap -eq [IntPtr]::Zero) { throw '記録ウィンドウが開きませんでした。' }
Write-Host ("記録ウィンドウ hwnd={0}" -f $cap)

Write-Host 'Ctrl+Shift+K を押します...'
[KB]::Chord(@([uint16]0xA2, [uint16]0xA0, [uint16]0x4B))    # LCTRL, LSHIFT, K
Start-Sleep -Milliseconds 600
Write-Host '続けて F9 を押します (2ステップ目)...'
[KB]::Chord(@([uint16]0x78))                                 # F9
Start-Sleep -Milliseconds 600

# 記録ウィンドウの表示内容を読む(2番目の子ウィンドウが表示欄)
$script:kids = @()
$cb = [UI+EnumProc]{ param($h,$p)
  $sb = New-Object Text.StringBuilder 256
  [UI]::GetWindowTextW($h, $sb, 256) | Out-Null
  $script:kids += $sb.ToString()
  return $true }
[UI]::EnumChildWindows($cap, $cb, [IntPtr]::Zero) | Out-Null
Write-Host '記録ウィンドウの表示:'
$script:kids | Where-Object { $_ } | ForEach-Object { Write-Host ('    "' + $_ + '"') }

Write-Host 'OK を押します...'
[UI]::Post($cap, 0x0111, [IntPtr]1, [IntPtr]0) | Out-Null     # IDC_CAP_OK
Start-Sleep -Seconds 1

$sb = New-Object Text.StringBuilder 256
[UI]::GetWindowTextW($cmb, $sb, 256) | Out-Null
Write-Host ("コンボに入った値: `"{0}`"" -f $sb.ToString())

Write-Host 'OK で保存します...'
[UI]::Post($sw, 0x0111, [IntPtr]1130, [IntPtr]0) | Out-Null   # IDC_OK
Start-Sleep -Seconds 2

$ini = 'c:\projects\mayous\build\dist\mayous.ini'
Write-Host '--- ini の [Chords] ---'
(Get-Content $ini) | Where-Object { $_ -match '^Side1Then' } | ForEach-Object { Write-Host ('    ' + $_) }
Write-Host ("mayous 生存: {0}" -f ((Get-MayousProc) -ne $null))
