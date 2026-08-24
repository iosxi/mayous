# shot_screen.ps1 - 実画面から設定ウィンドウを切り出す(PrintWindow を使わない)
param([string]$Out = 'screen.png')
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')
Add-Type -AssemblyName System.Drawing

$proc = Get-MayousProc
$sw = Find-ProcWnd ([uint32]$proc.Id) 'MayousSettingsWnd'
if ($sw -eq [IntPtr]::Zero) { throw '設定ウィンドウがありません' }
[UI]::SetWindowPos($sw, [IntPtr](-1), 0,0,0,0, 0x0043) | Out-Null   # HWND_TOPMOST
[UI]::SetForegroundWindow($sw) | Out-Null
Start-Sleep -Milliseconds 900
$r = New-Object UI+RECT
[UI]::GetWindowRect($sw, [ref]$r) | Out-Null
$w = $r.R - $r.L; $h = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap -ArgumentList $w, $h
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, (New-Object System.Drawing.Size -ArgumentList $w, $h))
$g.Dispose()
$path = Join-Path (Split-Path -Parent $PSScriptRoot) ('build\' + $Out)
$bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
$s = New-Object System.Drawing.Bitmap -ArgumentList ([int]($w*0.72)), ([int]($h*0.72))
$g2 = [System.Drawing.Graphics]::FromImage($s)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g2.DrawImage($bmp, 0, 0, $s.Width, $s.Height); $g2.Dispose()
$s.Save(($path -replace '\.png$','_small.png'), [System.Drawing.Imaging.ImageFormat]::Png)
$s.Dispose(); $bmp.Dispose()
[UI]::SetWindowPos($sw, [IntPtr](-2), 0,0,0,0, 0x0043) | Out-Null   # HWND_NOTOPMOST
Write-Host ("  実画面から保存: {0} ({1}x{2})" -f $path, $w, $h)
