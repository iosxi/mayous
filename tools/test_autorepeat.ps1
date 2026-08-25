# test_autorepeat.ps1 - 注入したキーを押しっぱなしにするとオートリピートするか
#
#   物理キーボードのオートリピートはキーボード側(タイプマティック)が作るもので、
#   SendInput で注入した押下に対して Windows が勝手に繰り返しを足すことは無い
#   ……というのが前提。全ての割り当てを「押している間ずっと」にしてよいかは
#   この前提が正しいかどうかで決まるので、実物の入力欄で確かめる。
#
#   イベントハンドラは別スコープで走り、外側の変数が見えないので使わない。
#   Show() で出してから DoEvents で回す。
$ErrorActionPreference = 'Stop'

Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class KB {
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

function Pump([int]$ms) {
    $end = (Get-Date).AddMilliseconds($ms)
    while ((Get-Date) -lt $end) {
        [Windows.Forms.Application]::DoEvents()
        Start-Sleep -Milliseconds 15
    }
}

$holdMs = 3000

$form = New-Object Windows.Forms.Form
$form.Text = 'autorepeat test'
$form.TopMost = $true
$form.Width = 420
$form.Height = 140
$box = New-Object Windows.Forms.TextBox
$box.Multiline = $true
$box.Dock = 'Fill'
$form.Controls.Add($box)

$form.Show()
$form.Activate()
$box.Focus() | Out-Null
Pump 800

[KB]::Down(0x41)          # 'a' を押しっぱなし
Pump $holdMs
[KB]::Up(0x41)
Pump 400

$got = $box.Text
$form.Close()
$form.Dispose()

$n = ($got -replace '[^a]', '').Length
Write-Host ("{0}ms 押しっぱなしにした結果: 入力欄の文字数 = {1}  (受信テキスト: '{2}')" -f $holdMs, $n, $got)
if ($n -le 1) { Write-Host '  -> オートリピートは起きない。押しっぱなしを既定にできる。' }
else          { Write-Host '  -> オートリピートが起きている。押しっぱなしを既定にはできない。' }
