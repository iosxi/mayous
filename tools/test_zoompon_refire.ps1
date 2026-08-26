# test_zoompon_refire.ps1 - 右を離さずに左クリックを繰り返したとき、
#   2 回目以降も zoom-pon に届くかを本物で数える(利用者のテストケース)
#
#   zoom-pon は「押すたびに固定」なので、1 回の同時押しで拡大が ON/OFF する。
#   拡大窓(ZoomPonHost)の表示状態を見れば、届いたかどうかが分かる。
#   利用者の設定を汚さないよう、exe と config を一時フォルダへ複製して動かす。
param([string]$Action = 'f13', [int]$KeyHoldMs = 120, [int]$Shots = 10)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'uilib.ps1')

$root  = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'
$zsrc  = 'c:\projects\zoom-pon\dist'
$zdir  = Join-Path $build 'zp'
$mdir  = Join-Path $build 'zpmayous'

if (@(Get-Process zoom-pon -ErrorAction SilentlyContinue).Count -gt 0) {
    throw 'zoom-pon が既に起動しています。単一インスタンスなので、終了してから実行してください。'
}

foreach ($d in @($zdir, $mdir)) {
    if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    New-Item -ItemType Directory -Path $d | Out-Null
}
Copy-Item (Join-Path $zsrc 'zoom-pon.exe') $zdir
# 利用者のテストケースどおり F13 をトリガーにする(VK 0x7C = 124)
'{"trigger_vks":[124],"zoom":2.0,"lock_mode":"toggle","show_cursor":false}' |
    Set-Content -Path (Join-Path $zdir 'config.json') -Encoding ascii
Copy-Item (Join-Path $build 'mayous.exe')  $mdir
@"
[General]
Enabled=1
SuspendOnFullscreen=0
KeyHoldMs=$KeyHoldMs
[Chords]
RightThenLeft=$Action
[Single]
[Exclude]
Processes=
"@ | Set-Content -Path (Join-Path $mdir 'mayous.ini') -Encoding ASCII

Add-Type @'
using System; using System.Runtime.InteropServices;
public static class ZP {
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

$zp  = Start-Process (Join-Path $zdir 'zoom-pon.exe') -PassThru
W 3000
$may = Start-Process (Join-Path $mdir 'mayous.exe') -PassThru
W 1800
[ZP]::SetCursorPos(900, 500) | Out-Null
W 400

function Zoomed {
    $h = Find-WndByClass 'ZoomPonHost'
    if ($h -eq [IntPtr]::Zero) { return $false }
    return [UI]::IsWindowVisible($h)
}

Write-Host '右クリックを押したまま、左クリックを繰り返す'
[ZP]::RDown()
W 300
$ok = 0
$seq = @()
$onN = 0; $onOk = 0; $offN = 0; $offOk = 0
for ($i = 1; $i -le $Shots; $i++) {
    $before = Zoomed
    [ZP]::LDown(); W 70; [ZP]::LUp()
    W 700
    $after = Zoomed
    # 「押すたびに固定」なので、1 回の同時押しで必ず反転するのが正しい
    if ($after -ne $before) { $ok++; $seq += '○' } else { $seq += '×' }
    if ($before) { $onN++;  if ($after -ne $before) { $onOk++  } }
    else         { $offN++; if ($after -ne $before) { $offOk++ } }
    W 300
}
[ZP]::RUp()
W 400
Write-Host ('  ' + ($seq -join ' ') + '   (○=反転した ×=反応なし)')
Write-Host ("  拡大していない状態から: {0}/{1}   拡大中から: {2}/{3}" -f $offOk,$offN,$onOk,$onN)

# 拡大したまま終わらせない
if (Zoomed) { [ZP]::RDown(); W 60; [ZP]::LDown(); W 40; [ZP]::LUp(); W 60; [ZP]::RUp(); W 600 }

Start-Process (Join-Path $mdir 'mayous.exe') -ArgumentList '--exit' -Wait
W 600
if (-not $may.HasExited) { $may.Kill() }
if (-not $zp.HasExited)  { $zp.Kill() }
W 400

Write-Host ("Action='{0}' KeyHoldMs={1}  ->  {2} 回中 {3} 回成功 (失敗 {4} 回)" `
            -f $Action, $KeyHoldMs, $Shots, $ok, ($Shots - $ok))
