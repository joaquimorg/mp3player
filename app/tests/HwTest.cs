// Hardware test driver for SerialLink (console). Not part of the shipped app.
//   hwtest find | wificheck <ssid> <pass> | mode prog|play | gen <file> <bytes> |
//   put <file> <slot> [slow] [cancelafter=N]
using System;
using System.IO;
using System.Threading;

public static class HwTest
{
    public static int Main(string[] a)
    {
        if (a.Length == 0) { Console.WriteLine("uso: hwtest find|wificheck|mode|gen|put"); return 1; }
        string port;
        if (a[0] == "gen")
        {
            int n = int.Parse(a[2]);
            byte[] d = new byte[n];
            d[0] = (byte)'I'; d[1] = (byte)'D'; d[2] = (byte)'3';
            Random r = new Random(7);
            for (int i = 3; i < n; i++) d[i] = (byte)r.Next(256);
            File.WriteAllBytes(a[1], d);
            Console.WriteLine("gerado " + a[1] + " " + n + " bytes");
            return 0;
        }
        WatchStatus st = SerialLink.FindWatch(AppConfigPort(), delegate(string m) { Console.WriteLine(m); }, out port);
        if (st == null) { Console.WriteLine("relogio NAO encontrado"); return 2; }
        Console.WriteLine("porta=" + port + " modo=" + st.Mode + " wifi=" + st.Wifi + " ip=" + st.Ip + " livre=" + st.FreeBytes);
        string err;
        if (a[0] == "find") return 0;
        using (SerialLink l = SerialLink.TryOpen(port, 115200, out err))
        {
            if (l == null) { Console.WriteLine(err); return 2; }
            if (a[0] == "wificheck") { Console.WriteLine("WIFI? -> " + l.CheckWifi(a[1], a[2])); return 0; }
            if (a[0] == "mode")
            {
                bool prog = a[1] == "prog";
                Console.WriteLine("MODO -> " + l.SetMode(prog));
                WatchStatus s2 = l.WaitStatus(delegate(WatchStatus s) { return prog ? (s.InProgramMode && s.HasIp) : !s.InProgramMode; }, 60000);
                Console.WriteLine(s2 == null ? "sem estado" : "modo=" + s2.Mode + " ip=" + s2.Ip);
                return s2 != null ? 0 : 1;
            }
            if (a[0] == "put")
            {
                bool slow = false; int cancelAfter = -1;
                for (int i = 3; i < a.Length; i++)
                {
                    if (a[i] == "slow") slow = true;
                    if (a[i].StartsWith("cancelafter=")) cancelAfter = int.Parse(a[i].Substring(12));
                }
                CancellationTokenSource cts = new CancellationTokenSource();
                string msg;
                System.Diagnostics.Stopwatch sw = System.Diagnostics.Stopwatch.StartNew();
                long lastShown = 0;
                PutResult pr = l.PutFile(a[1], int.Parse(a[2]), Path.GetFileNameWithoutExtension(a[1]), !slow,
                    delegate(long b)
                    {
                        if (b - lastShown >= 262144) { lastShown = b; Console.WriteLine("  " + b + " bytes"); }
                        if (cancelAfter >= 0 && b >= cancelAfter) cts.Cancel();
                    }, cts.Token, out msg);
                double secs = sw.Elapsed.TotalSeconds;
                Console.WriteLine("PUT -> " + pr + " " + msg + " (" + secs.ToString("F1") + " s)");
                WatchStatus s3 = l.Hello(1500);
                Console.WriteLine("depois: " + (s3 == null ? "sem resposta" : "modo=" + s3.Mode + " livre=" + s3.FreeBytes));
                return pr == PutResult.Ok ? 0 : (pr == PutResult.Cancelled ? 3 : 1);
            }
        }
        return 1;
    }

    static string AppConfigPort() { return Environment.GetEnvironmentVariable("TW_PREF") ?? "COM8"; }
}
