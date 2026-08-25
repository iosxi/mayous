# dbg_settings_paint.ps1 - Alt+Tab の一覧が重なったあと、設定画面のコントロールが
#   本当に消えているのか、描画されていないだけなのかを切り分ける。
#
#   ・子ウィンドウの一覧(クラス/ID/矩形/可視)を前後で比べる
#   ・実画面から切り出して目視する(PrintWindow は強制再描画になるので使わない)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')
Add-Type -AssemblyName System.Drawing

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class KB2 {
  [StructLayout(LayoutKind.Sequential)] struct KI { public ushort vk, sc; public uint flags, time; public IntPtr extra; }
  [StructLayout(LayoutKind.Sequential)] struct IN { public uint type; public KI ki; public int pad1, pad2; }
  [DllImport("user32.dll", SetLastError=true)] static extern uint SendInput(uint n, IN[] p, int cb);
  static void One(ushort vk, bool up){
    IN[] a = new IN[1];
    a[0].type = 1; a[0].ki.vk = vk; a[0].ki.flags = up ? 2u : 0u;
    SendInput(1, a, Marshal.SizeOf(typeof(IN)));
  }
  public static void Down(ushort vk){ One(vk, false); }
  public static void Up(ushort vk){ One(vk, true); }
}
'@

function Get-Children([IntPtr]$parent) {
    $script:kids = @()
    $cb = [UI+EnumProc]{ param($h, $p)
        $sb = New-Object Text.StringBuilder 256
        [UI]::GetClassNameW($h, $sb, 256) | Out-Null
        $tb = New-Object Text.StringBuilder 256
        [UI]::GetWindowTextW($h, $tb, 256) | Out-Null
        $r = New-Object UI+RECT
        [UI]::GetWindowRect($h, [ref]$r) | Out-Null
        $script:kids += [pscustomobject]@{
            Class   = $sb.ToString()
            Text    = $tb.ToString()
            Visible = [UI]::IsWindowVisible($h)
            Rect    = ('{0},{1} {2}x{3}' -f $r.L, $r.T, ($r.R - $r.L), ($r.B - $r.T))
        }
        return $true }
    [UI]::EnumChildWindows($parent, $cb, [IntPtr]::Zero) | Out-Null
    return $script:kids
}

function Save-Screen([IntPtr]$hwnd, [string]$name) {
    $r = New-Object UI+RECT
    [UI]::GetWindowRect($hwnd, [ref]$r) | Out-Null
    $w = $r.R - $r.L; $h = $r.B - $r.T
    $bmp = New-Object System.Drawing.Bitmap -ArgumentList $w, $h
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size -ArgumentList $w, $h))
    $g.Dispose()
    $path = Join-Path (Split-Path -Parent $PSScriptRoot) ('build\' + $name)
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
    Write-Host ("  実画面から保存: {0} ({1}x{2})" -f $path, $w, $h)
}

$sw = Find-WndByClass 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウがありません。先に開いてください。' }
[UI]::SetForegroundWindow($sw) | Out-Null
Start-Sleep -Milliseconds 700

$wr = New-Object UI+RECT
[UI]::GetWindowRect($sw, [ref]$wr) | Out-Null
Write-Host ("=== 設定ウィンドウ矩形: {0},{1} {2}x{3} ===" -f $wr.L, $wr.T, ($wr.R-$wr.L), ($wr.B-$wr.T))
$before = Get-Children $sw
Write-Host ("=== 前: 子ウィンドウ {0} 個 (うち可視 {1} 個) ===" -f $before.Count, ($before | Where-Object Visible).Count)
$before | Where-Object Visible | Select-Object -First 60 |
    ForEach-Object { Write-Host ("    {0,-16} {1,-28} {2}" -f $_.Class, $_.Text, $_.Rect) }
Save-Screen $sw 'paint_before.png'

Write-Host '=== Alt+Tab の一覧を出して重ねる ==='
[KB2]::Down(0xA4)              # 左 Alt
Start-Sleep -Milliseconds 120
[KB2]::Down(0x09); Start-Sleep -Milliseconds 60; [KB2]::Up(0x09)
Start-Sleep -Milliseconds 1200 # 一覧が出たまま重なっている状態を保つ
[KB2]::Up(0xA4)
Start-Sleep -Milliseconds 1500

[UI]::SetForegroundWindow($sw) | Out-Null
Start-Sleep -Milliseconds 1200

$after = Get-Children $sw
Write-Host ("=== 後: 子ウィンドウ {0} 個 (うち可視 {1} 個) ===" -f $after.Count, ($after | Where-Object Visible).Count)
Save-Screen $sw 'paint_after.png'

$b = $before | Where-Object Visible
$a = $after  | Where-Object Visible
$gone = Compare-Object $b $a -Property Class, Text, Rect | Where-Object SideIndicator -eq '<='
if ($gone) {
    Write-Host '  可視でなくなったコントロール:'
    $gone | ForEach-Object { Write-Host ("    {0}  '{1}'  {2}" -f $_.Class, $_.Text, $_.Rect) }
} else {
    Write-Host '  コントロールは全て可視のまま = 消えたのではなく描かれていないだけ'
}
