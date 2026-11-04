<#
Developer tool: saves what the watch screen shows as a PNG (240x240, enlarged 2x).
Needs the debug firmware (env watch_uidebug), whose console has a `shot` command that sends the frame over
the serial port. The port is opened with DTR/RTS released so the board is NOT reset, unless -Reset is given.

  powershell -ExecutionPolicy Bypass -File tools\watch-shot.ps1 -Out docs\images\relogio-player.png
  powershell -ExecutionPolicy Bypass -File tools\watch-shot.ps1 -Out docs\images\relogio-splash.png -Reset -WaitMs 3200
  powershell -ExecutionPolicy Bypass -File tools\watch-shot.ps1 -Out x.png -Command "tap 60 206"   # tap first

-Command "<a;b>"   console commands sent before the capture, separated by ";" (e.g. "tap 60 206").
-Reset              reset the watch first; -WaitMs is then the time to wait (the splash shows from ~2.4 s to ~7.4 s).
The dump takes about 20 s (115 KB of hex at 115200 baud) and the watch UI is busy meanwhile.
#>
param(
  [string]$Port = "COM8",
  [Parameter(Mandatory = $true)][string]$Out,
  [string]$Command = "",
  [switch]$Reset,
  [int]$WaitMs = 600,
  [int]$Scale = 2
)

Add-Type -AssemblyName System.Drawing
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.ReadTimeout = 200
$sp.NewLine = "`n"
try { $sp.Open() } catch { Write-Host "SKIP: $Port busy or missing ($($_.Exception.Message))"; exit 2 }
try {
  if ($Reset) {
    $sp.RtsEnable = $true; Start-Sleep -Milliseconds 120; $sp.RtsEnable = $false
  }
  Start-Sleep -Milliseconds $WaitMs
  $sp.DiscardInBuffer()
  foreach ($c in ($Command -split ";")) {   # several console commands, separated by ";"
    if ($c.Trim()) { $sp.Write("`n" + $c.Trim() + "`n"); Start-Sleep -Milliseconds 900 }
  }
  $sp.Write("`nshot`n")
  $w = 240; $h = 240
  $raw = New-Object byte[] ($w * $h * 2)
  $got = 0
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $done = $false
  while (-not $done -and $sw.Elapsed.TotalSeconds -lt 90) {
    try { $line = $sp.ReadLine().TrimEnd("`r") } catch [TimeoutException] { continue }
    if ($line -eq "SHOT END") { $done = $true; break }
    if ($line.StartsWith("SHOT ")) {
      $parts = $line.Split(" ")
      if ($parts.Length -lt 3) { continue }
      $off = [int]$parts[1]
      $hex = $parts[2]
      for ($i = 0; $i + 1 -lt $hex.Length; $i += 2) {
        $raw[$off + $i / 2] = [Convert]::ToByte($hex.Substring($i, 2), 16)
      }
      $got += $hex.Length / 2
    }
  }
  if (-not $done) { Write-Host "ERRO: the screen dump did not finish ($got bytes)"; exit 1 }
} finally { $sp.Close() }

$bmp = New-Object System.Drawing.Bitmap $w, $h
for ($y = 0; $y -lt $h; $y++) {
  for ($x = 0; $x -lt $w; $x++) {
    $i = ($y * $w + $x) * 2
    $v = ([int]$raw[$i] -shl 8) -bor [int]$raw[$i + 1]
    $r = (($v -shr 11) -band 31) * 255 / 31
    $g = (($v -shr 5) -band 63) * 255 / 63
    $b = ($v -band 31) * 255 / 31
    $bmp.SetPixel($x, $y, [System.Drawing.Color]::FromArgb([int]$r, [int]$g, [int]$b))
  }
}
$big = New-Object System.Drawing.Bitmap ($w * $Scale), ($h * $Scale)
$g2 = [System.Drawing.Graphics]::FromImage($big)
$g2.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$g2.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$g2.DrawImage($bmp, 0, 0, $w * $Scale, $h * $Scale)
$dir = Split-Path -Parent ([System.IO.Path]::GetFullPath($Out))
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
$big.Save([System.IO.Path]::GetFullPath($Out), [System.Drawing.Imaging.ImageFormat]::Png)
Write-Host "OK: $Out"
