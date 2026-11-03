<#
Dev harness (not shipped to users): uploads a file into a watch slot over the serial port (PUT protocol).
It is the REFERENCE CLIENT for the C# app (Task 5): same framing, timers and recovery rules.
The port is opened with DTR/RTS released so the board is NOT reset. Skips (exit 2) if the port is busy.
Notes for the app:
  - Send a lone "\n" first: it ends any partial line left in the watch's buffer.
  - TWATCH? reply: parse "wifi=" up to the LAST " ip=" (the SSID may contain spaces).
  - If TWATCH? at 115200 gets no reply, try once at 921600 (a previous session may have left the watch there;
    it falls back to 115200 by itself after 10 s without a valid command).
  - The 3 s reply timer starts when the frame has left the PC (BytesToWrite == 0), not when Write() returns.
  - Before CANCELAR wait >= 50 ms of quiet so the watch sees it as a line, not as part of a frame.
  - Never resend the LAST block after losing the final reply: the watch may already have committed and gone
    back to 115200. Ask TWATCH? instead.

  powershell -ExecutionPolicy Bypass -File serial-put.ps1 -Port COM8 -File x.mp3 -Slot 3 [-Baud 921600] [-Name nome]

Test options (each acts once, on the first attempt of that block):
  -CorruptBlock n     flip one data byte of block n (the watch answers R, the block is resent)
  -DropAck n          ignore the K of block n and resend the same block (the watch must answer K, not rewrite)
  -KillAfterBlocks n  exit abruptly (no CANCELAR) after n blocks were accepted
  -CancelAtBlock n    send CANCELAR instead of block n (n = 67 or 68: seq 0x43 'C' collision) and time the reply
Exit codes: 0 OK, 1 refused/failed, 2 port busy/missing, 3 killed on purpose.
Protocol: PUT <slot> <size> <crc32 hex> <name hex>, then frames [seq][data<=4096][crc16 CCITT BE]; replies K/R;
up to 5 resends per block (R or 3 s without reply), then CANCELAR; the last block is answered with a line.
#>
param(
  [string]$Port = "COM8",
  [Parameter(Mandatory = $true)][string]$File,
  [Parameter(Mandatory = $true)][int]$Slot,
  [int]$Baud = 115200,
  [string]$Name = "",
  [int]$CorruptBlock = -1,
  [int]$DropAck = -1,
  [int]$KillAfterBlocks = -1,
  [int]$CancelAtBlock = -1
)

$src = @'
using System;
using System.IO;
using System.IO.Ports;
using System.Diagnostics;
using System.Text;
using System.Threading;

public static class SerialPut {
  static ushort Crc16(byte[] d, int off, int n) {
    ushort crc = 0xFFFF;
    for (int i = 0; i < n; i++) {
      crc ^= (ushort)(d[off + i] << 8);
      for (int b = 0; b < 8; b++) crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021) : (ushort)(crc << 1);
    }
    return crc;
  }

  static uint[] table;
  static uint Crc32(byte[] d) {
    if (table == null) {
      table = new uint[256];
      for (uint i = 0; i < 256; i++) {
        uint c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) != 0 ? (c >> 1) ^ 0xEDB88320u : (c >> 1);
        table[i] = c;
      }
    }
    uint crc = 0xFFFFFFFFu;
    foreach (byte x in d) crc = table[(crc ^ x) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
  }

  static string ReadLine(SerialPort sp, int timeoutMs) {
    var sb = new StringBuilder();
    var sw = Stopwatch.StartNew();
    while (sw.ElapsedMilliseconds < timeoutMs) {
      int b;
      try { b = sp.ReadByte(); } catch (TimeoutException) { continue; }
      if (b == '\n') {
        string s = sb.ToString().TrimEnd('\r');
        if (s.Length > 0) return s;
        continue;
      }
      sb.Append((char)b);
    }
    return null;
  }

  // Waits for K/R or, when `last`, for a line. Returns "K", "R", a line, or null on timeout.
  static void Drain(SerialPort sp) {
    var w = Stopwatch.StartNew();
    while (sp.BytesToWrite > 0 && w.ElapsedMilliseconds < 10000) Thread.Sleep(1);
  }

  static string Reply(SerialPort sp, bool last, int timeoutMs) {
    Drain(sp);  // the reply timer starts when the frame has left the PC
    var sw = Stopwatch.StartNew();
    while (sw.ElapsedMilliseconds < timeoutMs) {
      int b;
      try { b = sp.ReadByte(); } catch (TimeoutException) { continue; }
      if (b == 'K' && !last) return "K";
      if (b == 'R') return "R";
      if (b == 'O' || b == 'E') {
        string rest = ReadLine(sp, last ? 10000 : 3000);
        return ((char)b) + (rest ?? "");
      }
      // anything else: ignore
    }
    return null;
  }

  static void Status(SerialPort sp, int baud) {
    if (baud != 115200) { Thread.Sleep(50); sp.BaudRate = 115200; }
    Thread.Sleep(50);
    sp.DiscardInBuffer();
    sp.Write("\nTWATCH?\n");
    Console.WriteLine("<< " + ReadLine(sp, 2000));
  }

  static void Quiet(SerialPort sp, int ms) {  // wait until nothing has arrived for `ms`
    var sw = Stopwatch.StartNew();
    while (sw.ElapsedMilliseconds < ms) {
      if (sp.BytesToRead > 0) { sp.DiscardInBuffer(); sw.Reset(); sw.Start(); }
      Thread.Sleep(5);
    }
  }

  public static int Run(string port, string file, int slot, int baud, string name, int corrupt, int dropAck, int killAfter,
                        int cancelAt) {
    byte[] data = File.ReadAllBytes(file);
    uint crc = Crc32(data);
    if (name == null || name.Length == 0) name = Path.GetFileNameWithoutExtension(file);
    byte[] nameBytes = Encoding.UTF8.GetBytes(name);
    if (nameBytes.Length > 48) {
      int n = 48;
      while (n > 0 && (nameBytes[n] & 0xC0) == 0x80) n--;  // nameBytes[n] must start a character
      Array.Resize(ref nameBytes, n);
    }
    var hex = new StringBuilder();
    foreach (byte x in nameBytes) hex.Append(x.ToString("X2"));

    var sp = new SerialPort(port, 115200, Parity.None, 8, StopBits.One);
    sp.DtrEnable = false;
    sp.RtsEnable = false;
    sp.ReadTimeout = 100;
    sp.WriteTimeout = 5000;
    sp.ReadBufferSize = 65536;
    sp.WriteBufferSize = 65536;
    try { sp.Open(); } catch (Exception e) { Console.WriteLine("SKIP: " + port + " busy or missing (" + e.Message + ")"); return 2; }
    try {
      sp.DtrEnable = false; sp.RtsEnable = false;
      sp.Write("\n");
      Thread.Sleep(100);
      sp.DiscardInBuffer();
      if (baud != 115200) {
        sp.Write("BAUD " + baud + "\n");
        string r = ReadLine(sp, 2000);
        Console.WriteLine("<< " + r);
        if (r != "OK") return 1;
        sp.BaudRate = baud;
        Thread.Sleep(20);
        sp.Write("\n");
        Thread.Sleep(30);
        sp.DiscardInBuffer();
      }
      string put = "PUT " + slot + " " + data.Length + " " + crc.ToString("X8") + " " + hex;
      Console.WriteLine(">> " + put);
      sp.Write(put + "\n");
      string ok = ReadLine(sp, 3000);
      Console.WriteLine("<< " + ok);
      if (ok != "OK PUT") return 1;

      var sw = Stopwatch.StartNew();
      int blocks = (data.Length + 4095) / 4096;
      int resends = 0;
      bool corrupted = false, dropped = false;
      string final = null;
      for (int i = 0; i < blocks; i++) {
        int off = i * 4096, len = Math.Min(4096, data.Length - off);
        byte[] frame = new byte[len + 3];
        frame[0] = (byte)(i & 0xFF);
        Buffer.BlockCopy(data, off, frame, 1, len);
        ushort c16 = Crc16(frame, 0, len + 1);
        frame[len + 1] = (byte)(c16 >> 8);
        frame[len + 2] = (byte)(c16 & 0xFF);
        bool last = i == blocks - 1;
        if (i == cancelAt) {
          Quiet(sp, 50);
          var cw = Stopwatch.StartNew();
          sp.Write("CANCELAR\n");
          string cr = ReadLine(sp, 5000);
          Console.WriteLine(string.Format("CANCELAR before block {0} (seq 0x{1:X2}): << {2} after {3} ms", i, i & 0xFF,
                                          cr ?? "(nothing)", cw.ElapsedMilliseconds));
          Status(sp, baud);
          return cr == "OK" ? 0 : 1;
        }
        int attempt = 0;
        for (;;) {
          if (attempt > 5) {
            Console.WriteLine("block " + i + ": no K after 5 resends, sending CANCELAR");
            Quiet(sp, 50);
            sp.Write("CANCELAR\n");
            Console.WriteLine("<< " + ReadLine(sp, 3000));
            return 1;
          }
          byte[] tx = frame;
          if (i == corrupt && !corrupted) {
            corrupted = true;
            tx = (byte[])frame.Clone();
            tx[1 + len / 2] ^= 0x5A;
            Console.WriteLine("block " + i + ": sending corrupted copy");
          }
          sp.Write(tx, 0, tx.Length);
          // Last block: 6 s, so the watch's own 3 s mid-frame 'R' can still arrive if the frame was cut.
          string r = Reply(sp, last, last ? 6000 : 3000);
          if (r == null && last) {
            Console.WriteLine("final reply lost: NOT resending the last block; asking TWATCH?");
            Status(sp, baud);
            return 1;
          }
          if (r == "K" && i == dropAck && !dropped) {
            dropped = true;
            Console.WriteLine("block " + i + ": K received, pretending it was lost; resending the same block");
            sp.Write(frame, 0, frame.Length);
            r = Reply(sp, last, 3000);
            Console.WriteLine("block " + i + ": duplicate answered " + (r ?? "(nothing)"));
          }
          if (r == "K") break;
          if (r != null && r != "R") {  // a line
            if (!last || !r.StartsWith("OK ")) { Console.WriteLine("<< " + r + "  (at block " + i + ")"); return 1; }
            final = r;
            break;
          }
          attempt++;
          resends++;
          Console.WriteLine("block " + i + ": " + (r == null ? "no reply in 3 s" : "R") + ", resend " + attempt);
          Thread.Sleep(50);
          sp.DiscardInBuffer();
        }
        if (killAfter >= 0 && i + 1 >= killAfter) {
          Console.WriteLine("killing the client abruptly after " + (i + 1) + " blocks");
          Environment.Exit(3);
        }
      }
      double s = sw.Elapsed.TotalSeconds;
      Console.WriteLine("<< " + final);
      Console.WriteLine(string.Format("{0} bytes in {1:F1} s = {2:F1} KB/s at {3} baud, {4} resends", data.Length, s,
                                      data.Length / 1024.0 / s, baud, resends));
      string want = "OK " + crc.ToString("X8");
      Console.WriteLine(final == want ? "CRC MATCH " + crc.ToString("X8") : "CRC MISMATCH: expected " + want);
      Status(sp, baud);
      return final == want ? 0 : 1;
    } finally {
      if (sp.IsOpen) sp.Close();
    }
  }
}
'@

if (-not ("SerialPut" -as [type])) { Add-Type -TypeDefinition $src -ReferencedAssemblies System }
$path = (Resolve-Path $File).Path
exit [SerialPut]::Run($Port, $path, $Slot, $Baud, $Name, $CorruptBlock, $DropAck, $KillAfterBlocks, $CancelAtBlock)
