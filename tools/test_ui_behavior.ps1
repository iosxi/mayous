# test_ui_behavior.ps1 - 二重起動で設定画面が出るか / [適用] の活性制御を検証する
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')
Add-Type @'
using System; using System.Runtime.InteropServices; using System.Text;
public static class UB {
  [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr h, int id);
  [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, string l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
}
'@
$build = Join-Path (Split-Path -Parent $PSScriptRoot) 'build'
$dist  = Join-Path $build 'dist'

Write-Host '=== 1. 常駐中にもう一度 exe を起動する ==='
Get-Process mayous -ErrorAction SilentlyContinue | ForEach-Object {
  try { Start-Process $_.Path -ArgumentList '--exit' -Wait } catch {} }
Start-Sleep -Seconds 2
Get-Process mayous -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 800

Start-Process (Join-Path $dist 'mayous.exe') | Out-Null
Start-Sleep -Seconds 3
$proc = Get-MayousProc
$before = @(Get-Process mayous).Count
Write-Host ("  1つ目を起動。プロセス数={0}  設定画面={1}" -f $before,
    $(if ((Find-ProcWnd ([uint32]$proc.Id) 'MayousSettingsWnd') -ne [IntPtr]::Zero) {'開いている'} else {'閉じている'}))

Start-Process (Join-Path $dist 'mayous.exe') | Out-Null
Start-Sleep -Seconds 3
$sw = Find-ProcWnd ([uint32]$proc.Id) 'MayousSettingsWnd'
Write-Host ("  2つ目を起動 -> 設定画面={0}   プロセス数={1} (増えていないこと)" -f
    $(if ($sw -ne [IntPtr]::Zero) {'開いた'} else {'開かない'}), @(Get-Process mayous).Count)

if ($sw -eq [IntPtr]::Zero) { throw '設定画面が開きませんでした' }

Write-Host ''
Write-Host '=== 2. [適用] ボタンの活性 ==='
$apply = [UB]::GetDlgItem($sw, 1132)
Write-Host ("  開いた直後      : 適用={0} (無効であるべき)" -f $(if([UB]::IsWindowEnabled($apply)){'有効'}else{'無効'}))

# 右クリックタブの「+左クリック」コンボ(CH_ID(1,0)=7)を書き換える
$cmb = [UB]::GetDlgItem($sw, 2000 + 7)
[UB]::SendMessageW($cmb, 0x000C, [IntPtr]0, 'ctrl+alt+9') | Out-Null   # WM_SETTEXT
# WM_SETTEXT では通知が飛ばないので、利用者の入力と同じ CBN_EDITCHANGE を送る
[UB]::SendMessage($sw, 0x0111, [IntPtr]((5 -shl 16) -bor (2000+7)), $cmb) | Out-Null
Start-Sleep -Milliseconds 400
Write-Host ("  値を変更した後  : 適用={0} (有効であるべき)" -f $(if([UB]::IsWindowEnabled($apply)){'有効'}else{'無効'}))

[UI]::Post($sw, 0x0111, [IntPtr]1132, [IntPtr]0) | Out-Null    # 適用
Start-Sleep -Seconds 2
Write-Host ("  適用を押した後  : 適用={0} (無効に戻るべき)" -f $(if([UB]::IsWindowEnabled($apply)){'有効'}else{'無効'}))

Write-Host ''
Write-Host '=== 3. ini に反映されたか ==='
(Get-Content (Join-Path $dist 'mayous.ini')) | Where-Object { $_ -match '^RightThenLeft' } | ForEach-Object { Write-Host ('  ' + $_) }
