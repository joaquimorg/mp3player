// USB serial link to the watch: probing, the line commands (TWATCH?, WIFI, MODO) and the PUT upload.
// Reference client: tools/pc/serial-put.ps1 (same framing and timers). C# 5.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.IO.Ports;
using System.Text;
using System.Threading;

public enum PutResult { Ok, Refused, Failed, Cancelled, LinkProblem }

public class SerialLink : IDisposable
{
    public SerialPort Port;
    public string Name;
    public int Baud = 115200;

    SerialLink(SerialPort sp, string name) { Port = sp; Name = name; }

    public void Dispose()
    {
        try { if (Port != null && Port.IsOpen) Port.Close(); } catch (Exception) { }
        Port = null;
    }

    public static string[] PortNames()
    {
        string[] n = SerialPort.GetPortNames();
        Array.Sort(n, delegate(string a, string b) { return PortNumber(a).CompareTo(PortNumber(b)); });
        return n;
    }

    static int PortNumber(string name)
    {
        int v;
        return name.Length > 3 && int.TryParse(name.Substring(3), out v) ? v : 0;
    }

    // Opens with DTR/RTS released so the board is NOT reset. Returns null (and a message) when the
    // port is busy or missing.
    public static SerialLink TryOpen(string name, int baud, out string error)
    {
        error = null;
        SerialPort sp = new SerialPort(name, baud, Parity.None, 8, StopBits.One);
        sp.DtrEnable = false;
        sp.RtsEnable = false;
        sp.ReadTimeout = 100;
        sp.WriteTimeout = 5000;
        sp.ReadBufferSize = 65536;
        sp.WriteBufferSize = 65536;
        try
        {
            sp.Open();
            sp.DtrEnable = false;
            sp.RtsEnable = false;
        }
        catch (Exception e)
        {
            error = name + " está ocupada ou não existe (" + e.Message + ")";
            try { sp.Dispose(); } catch (Exception) { }
            return null;
        }
        SerialLink l = new SerialLink(sp, name);
        l.Baud = baud;
        return l;
    }

    public void SetBaud(int baud)
    {
        Port.BaudRate = baud;
        Baud = baud;
    }

    // One line (without the terminator), or null on timeout. Empty lines are skipped.
    public string ReadLine(int timeoutMs)
    {
        StringBuilder sb = new StringBuilder();
        Stopwatch sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < timeoutMs)
        {
            int b;
            try { b = Port.ReadByte(); }
            catch (TimeoutException) { continue; }
            if (b == '\n')
            {
                string s = sb.ToString().TrimEnd('\r');
                if (s.Length > 0) return s;
                continue;
            }
            sb.Append((char)b);
        }
        return null;
    }

    // Ends any partial line in the watch's buffer. Send this before the first command.
    public void Resync()
    {
        Port.Write("\n");
        Thread.Sleep(80);
        Port.DiscardInBuffer();
    }

    // Sends one command line and returns the first reply line that starts with OK or ERRO.
    // Other lines (logs, leftovers of earlier noise) are ignored.
    public string Command(string line, int timeoutMs)
    {
        Port.Write(line + "\n");
        Stopwatch sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < timeoutMs)
        {
            string r = ReadLine((int)Math.Max(50, timeoutMs - sw.ElapsedMilliseconds));
            if (r == null) return null;
            if (r.StartsWith("OK", StringComparison.Ordinal) || r.StartsWith("ERRO", StringComparison.Ordinal)) return r;
        }
        return null;
    }

    public WatchStatus Hello(int timeoutMs)
    {
        Resync();
        string r = Command("TWATCH?", timeoutMs);
        return r == null ? null : AppLogic.ParseTwatch(r);
    }

    // Probes the serial ports with TWATCH? only (nothing else is ever sent to other devices).
    // `preferred` (the last port that worked) is tried first and, if silent at 115200, once at 921600
    // (a crashed previous session can leave the watch there; it returns to 115200 by itself in 10 s).
    public static WatchStatus FindWatch(string preferred, Action<string> log, out string portName)
    {
        portName = null;
        List<string> order = new List<string>();
        if (!string.IsNullOrEmpty(preferred)) order.Add(preferred);
        foreach (string p in PortNames()) if (!order.Contains(p)) order.Add(p);
        foreach (string p in order)
        {
            string err;
            using (SerialLink l = TryOpen(p, 115200, out err))
            {
                if (l == null) { log("  " + p + ": " + err); continue; }
                WatchStatus st = l.Hello(700);
                if (st == null && p == preferred)
                {
                    l.SetBaud(921600);
                    st = l.Hello(700);
                }
                if (st != null) { portName = p; log("  " + p + ": relógio encontrado"); return st; }
                log("  " + p + ": não é o relógio");
            }
        }
        return null;
    }

    // WIFI <ssid>\t<password> -> "OK" or an error line. The password is never logged.
    public string SendWifi(string ssid, string pass)
    {
        Resync();
        return Command("WIFI " + ssid + "\t" + pass, 3000);
    }

    // WIFI? validates only (nothing is saved): used by the hardware test.
    public string CheckWifi(string ssid, string pass)
    {
        Resync();
        return Command("WIFI? " + ssid + "	" + pass, 3000);
    }

    public string SetMode(bool program)
    {
        Resync();
        return Command(program ? "MODO PROG" : "MODO PLAY", 3000);
    }

    // Polls TWATCH? until `done` is satisfied or the time runs out.
    public WatchStatus WaitStatus(Func<WatchStatus, bool> done, int totalMs)
    {
        Stopwatch sw = Stopwatch.StartNew();
        WatchStatus last = null;
        while (sw.ElapsedMilliseconds < totalMs)
        {
            WatchStatus st = Hello(1000);
            if (st != null) { last = st; if (done(st)) return st; }
            Thread.Sleep(500);
        }
        return last;
    }

    void WaitSent()
    {
        Stopwatch w = Stopwatch.StartNew();
        while (Port.BytesToWrite > 0 && w.ElapsedMilliseconds < 10000) Thread.Sleep(1);
    }

    void Quiet(int ms)
    {
        Stopwatch sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < ms)
        {
            if (Port.BytesToRead > 0) { Port.DiscardInBuffer(); sw.Reset(); sw.Start(); }
            Thread.Sleep(5);
        }
    }

    // After a frame: "K", "R", a line starting with O/E (OK.../ERRO...), or null on timeout.
    // The timer starts when the frame has left the PC.
    string BlockReply(bool last, int timeoutMs)
    {
        WaitSent();
        Stopwatch sw = Stopwatch.StartNew();
        while (sw.ElapsedMilliseconds < timeoutMs)
        {
            int b;
            try { b = Port.ReadByte(); }
            catch (TimeoutException) { continue; }
            if (b == 'K' && !last) return "K";
            if (b == 'R') return "R";
            if (b == 'O' || b == 'E')
            {
                string rest = ReadLine(last ? 10000 : 3000);
                return ((char)b) + (rest ?? "");
            }
        }
        return null;
    }

    // Uploads a file into a slot over USB. `progress` receives the bytes accepted so far. `fast` uses
    // 921600 baud (the link occasionally needs resends there; a block that fails 5 times ends with
    // LinkProblem so the caller can retry at 115200). The watch pauses the music during the transfer.
    public PutResult PutFile(string path, int slot, string displayName, bool fast, Action<long> progress,
                             CancellationToken ct, out string message)
    {
        message = null;
        byte[] data = File.ReadAllBytes(path);
        uint crc = AppLogic.Crc32(data, 0, data.Length);
        string nameHex = AppLogic.NameHex(AppLogic.SafeFileName(displayName));
        Resync();
        if (fast)
        {
            string b = Command("BAUD 921600", 2000);
            if (b != "OK") { message = "O relógio não aceitou a velocidade rápida (" + (b ?? "sem resposta") + ")."; return PutResult.Failed; }
            SetBaud(921600);
            Thread.Sleep(30);
            Port.Write("\n");
            Thread.Sleep(30);
            Port.DiscardInBuffer();
        }
        try
        {
            string put = "PUT " + slot + " " + data.Length + " " + crc.ToString("X8") + " " + nameHex;
            Port.Write(put + "\n");
            string ok = ReadLine(3000);
            if (ok != "OK PUT")
            {
                message = DescribeError(ok);
                return PutResult.Refused;
            }
            int blocks = (data.Length + 4095) / 4096;
            for (int i = 0; i < blocks; i++)
            {
                if (ct.IsCancellationRequested) return Cancel(out message);
                int off = i * 4096, len = Math.Min(4096, data.Length - off);
                byte[] frame = new byte[len + 3];
                frame[0] = (byte)(i & 0xFF);
                Buffer.BlockCopy(data, off, frame, 1, len);
                ushort c16 = AppLogic.Crc16(frame, 0, len + 1);
                frame[len + 1] = (byte)(c16 >> 8);
                frame[len + 2] = (byte)(c16 & 0xFF);
                bool last = i == blocks - 1;
                int attempt = 0;
                string final = null;
                for (;;)
                {
                    if (attempt > 5)
                    {
                        Quiet(50);
                        Port.Write("CANCELAR\n");
                        ReadLine(3000);
                        message = "A ligação USB está instável (o mesmo bloco falhou 6 vezes).";
                        return PutResult.LinkProblem;
                    }
                    Port.Write(frame, 0, frame.Length);
                    string r = BlockReply(last, last ? 6000 : 3000);
                    if (r == null && last)
                    {
                        // Never resend the last block: the watch may already have committed. Ask instead.
                        message = "A resposta final do relógio perdeu-se. Verifica na lista se a música ficou gravada.";
                        return PutResult.Failed;
                    }
                    if (r == "K") break;
                    if (r != null && r != "R")
                    {
                        if (last && r.StartsWith("OK ", StringComparison.Ordinal)) { final = r; break; }
                        message = DescribeError(r);
                        return PutResult.Failed;
                    }
                    attempt++;
                    Thread.Sleep(50);
                    Port.DiscardInBuffer();
                }
                if (progress != null) progress(off + len);
                if (last)
                {
                    string want = "OK " + crc.ToString("X8");
                    if (final == want) return PutResult.Ok;
                    message = "A soma de verificação final não coincide (" + (final ?? "sem resposta") + ").";
                    return PutResult.Failed;
                }
            }
            message = "Nada para enviar.";
            return PutResult.Failed;
        }
        finally
        {
            // The watch returns to 115200 at the end of every PUT.
            try { if (Port.IsOpen && Baud != 115200) SetBaud(115200); } catch (Exception) { }
        }
    }

    PutResult Cancel(out string message)
    {
        Quiet(50);
        Port.Write("CANCELAR\n");
        string r = ReadLine(5000);
        message = "Envio cancelado.";
        return PutResult.Cancelled;
    }

    public static string DescribeError(string reply)
    {
        if (reply == null) return "O relógio não respondeu.";
        switch (reply)
        {
            case "ERRO espaco": return "Não há espaço livre suficiente no relógio.";
            case "ERRO nao-mp3": return "O ficheiro não é um MP3 válido.";
            case "ERRO ocupado": return "O relógio está ocupado (a receber outra coisa ou a mudar de modo).";
            case "ERRO argumentos": return "O relógio recusou o pedido (argumentos inválidos).";
        }
        return "O relógio respondeu: " + reply;
    }
}
