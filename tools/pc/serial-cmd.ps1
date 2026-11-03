<#
Dev helper: sends ONE command line to the watch over its serial port and prints the reply lines (max ~1 s).
The port is opened with DTR/RTS released so the board is NOT reset. Skips (exit 2) if the port is busy.

  serial-cmd.ps1 -Port COM8 -Line "TWATCH?"
  serial-cmd.ps1 -Port COM8 -Hex "00ff80c3"     # raw bytes + newline (binary junk test)
  serial-cmd.ps1 -Port COM8 -Junk 10000 -Line "TWATCH?"   # junk without newline, then "\n" + the line

Never use it with a real WIFI line unless you mean to overwrite the saved network.
#>
param(
  [string]$Port = "COM8",
  [string]$Line = "",
  [string]$Hex = "",
  [int]$Junk = 0,
  [int]$WaitMs = 1000
)

$bytes = New-Object System.Collections.Generic.List[byte]
if ($Junk -gt 0) { for ($i = 0; $i -lt $Junk; $i++) { $bytes.Add([byte](0x41 + ($i % 26))) }; $bytes.Add(10) }
if ($Hex -ne "") {
  for ($i = 0; $i + 1 -lt $Hex.Length; $i += 2) { $bytes.Add([Convert]::ToByte($Hex.Substring($i, 2), 16)) }
  $bytes.Add(10)
} elseif ($Line -ne "") {
  $bytes.AddRange([System.Text.Encoding]::UTF8.GetBytes($Line)); $bytes.Add(10)
}

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
$sp.DtrEnable = $false
$sp.RtsEnable = $false
$sp.ReadTimeout = 100
try { $sp.Open() } catch { Write-Host "SKIP: $Port busy or missing ($($_.Exception.Message))"; exit 2 }
try {
  $sp.DtrEnable = $false; $sp.RtsEnable = $false
  $sp.DiscardInBuffer()
  if ($bytes.Count -gt 0) { $arr = $bytes.ToArray(); $sp.Write($arr, 0, $arr.Length) }
  $sw = [Diagnostics.Stopwatch]::StartNew()
  $buf = ""
  while ($sw.ElapsedMilliseconds -lt $WaitMs) {
    try { $buf += $sp.ReadExisting() } catch {}
    Start-Sleep -Milliseconds 20
  }
  foreach ($l in ($buf -split "`r?`n")) { if ($l -ne "") { Write-Host "<< $l" } }
} finally { $sp.Close() }
