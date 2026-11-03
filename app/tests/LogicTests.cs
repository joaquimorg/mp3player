// Console tests for AppLogic (no UI, no hardware). Exit code 0 only when every check passes.
using System;
using System.Text;

public static class LogicTests
{
    static int total, failed;

    static void Eq(object expected, object actual, string what)
    {
        total++;
        if (!object.Equals(expected, actual))
        {
            failed++;
            Console.WriteLine("FALHOU: " + what + " -> esperado [" + expected + "] obtido [" + actual + "]");
        }
    }

    static void True(bool v, string what) { Eq(true, v, what); }

    static byte[] Ascii(string s) { return Encoding.ASCII.GetBytes(s); }

    public static int Main()
    {
        // Transport: Wi-Fi only when configured AND reachable AND not forced to USB.
        foreach (bool conf in new bool[] { false, true })
            foreach (bool reach in new bool[] { false, true })
                foreach (bool force in new bool[] { false, true })
                {
                    Transport want = (conf && reach && !force) ? Transport.Wifi : Transport.Usb;
                    Eq(want, AppLogic.ChooseTransport(conf, reach, force),
                       "transport conf=" + conf + " reach=" + reach + " force=" + force);
                }

        // Encode plans (margin 16384, sanity limit 12 MiB).
        Eq(EncodePlan.Keep, AppLogic.PlanAfterFirstEncode(2000000, 12000000), "keep small");
        Eq(EncodePlan.Retry64kMono, AppLogic.PlanAfterFirstEncode(11990000, 12000000), "margin forces retry");
        Eq(EncodePlan.Keep, AppLogic.PlanAfterFirstEncode(12000000 - 16384, 12000000), "exact fit with margin");
        Eq(EncodePlan.Retry64kMono, AppLogic.PlanAfterFirstEncode(12000000 - 16383, 12000000), "one byte over");
        Eq(EncodePlan.Retry64kMono, AppLogic.PlanAfterFirstEncode(13000000, 100000000), "above sanity limit");
        Eq(EncodePlan.TooLarge, AppLogic.PlanAfterRetry(13000000, 100000000), "retry still too large");
        Eq(EncodePlan.DoesNotFit, AppLogic.PlanAfterRetry(5000000, 1000000), "retry does not fit");
        Eq(EncodePlan.Keep, AppLogic.PlanAfterRetry(900000, 1000000), "retry fits");
        Eq(4016384L, AppLogic.MissingBytes(5000000, 1000000), "missing bytes");
        Eq(0L, AppLogic.MissingBytes(100, 1000000), "nothing missing");

        // Formatting identical to the firmware.
        Eq("2,1 MB", AppLogic.FormatMB(2200000), "formatMB 2200000");
        Eq("4,3 MB", AppLogic.FormatMB(4430L * 1024), "formatMB 4430KB");
        Eq("48 KB", AppLogic.FormatMB(50000), "formatMB 50000");
        Eq("0 KB", AppLogic.FormatMB(0), "formatMB 0");
        Eq("9,4 MB", AppLogic.FormatMB(9908224), "formatMB free");

        // Safe display names.
        Eq("abcde.mp3", AppLogic.SafeFileName("a;b,c\"d&e.mp3"), "safe name specials");
        Eq("Música ção.mp3", AppLogic.SafeFileName("C:\\x\\Música ção.mp3"), "safe name keeps accents, drops path");
        Eq("faixa", AppLogic.SafeFileName(";;,,"), "safe name empty");
        Eq("faixa", AppLogic.SafeFileName(null), "safe name null");
        string longName = new string('a', 60);
        Eq(48, Encoding.UTF8.GetByteCount(AppLogic.SafeFileName(longName)), "48 bytes ascii");
        string accents = new string('ç', 40);   // 2 bytes each
        string cut = AppLogic.SafeFileName(accents);
        Eq(48, Encoding.UTF8.GetByteCount(cut), "48 bytes accents cut at code point");
        Eq(24, cut.Length, "24 chars of 2 bytes");
        string emoji = new string('a', 46) + "😀";   // 46 + 4 bytes: must not split the surrogate pair
        string ce = AppLogic.SafeFileName(emoji);
        Eq(46, Encoding.UTF8.GetByteCount(ce), "emoji not split");
        Eq("6162", AppLogic.NameHex("ab"), "name hex");
        Eq("C3A7", AppLogic.NameHex("ç"), "name hex utf8");

        // Checksums: same vectors as the firmware tests.
        byte[] v = Ascii("123456789");
        Eq((ushort)0x29B1, AppLogic.Crc16(v, 0, v.Length), "crc16 vector");
        Eq(0xCBF43926u, AppLogic.Crc32(v, 0, v.Length), "crc32 vector");
        uint inc = AppLogic.Crc32Update(0, v, 0, 4);
        inc = AppLogic.Crc32Update(inc, v, 4, 5);
        Eq(0xCBF43926u, inc, "crc32 incremental");
        Eq(0u, AppLogic.Crc32(new byte[0], 0, 0), "crc32 empty");

        // Wi-Fi validation (same limits as the firmware).
        string err;
        True(AppLogic.ValidWifi("casa", "abc12345", out err), "wifi ok");
        True(AppLogic.ValidWifi("minha rede", "com espacos 1", out err), "wifi with spaces");
        True(AppLogic.ValidWifi("aberta", "", out err), "wifi open network");
        True(!AppLogic.ValidWifi("x", "1234567", out err), "pass too short");
        True(!AppLogic.ValidWifi("x", new string('p', 64), out err), "pass too long");
        True(!AppLogic.ValidWifi("", "abc12345", out err), "ssid empty");
        True(!AppLogic.ValidWifi(new string('s', 33), "abc12345", out err), "ssid 33");
        True(!AppLogic.ValidWifi("a\tb", "abc12345", out err), "ssid with TAB");
        True(!AppLogic.ValidWifi("a", "abc\n12345", out err), "pass with newline");
        True(AppLogic.ValidWifi("a", "p\"a&ss é1", out err), "pass with quote, ampersand, accent");
        True(err == null, "no error text when valid");

        // URLs.
        Eq("https://www.youtube.com/watch?v=jNQXAC9IVRw&t=5s",
           AppLogic.NormalizeUrl("  https://www.youtube.com/watch?v=jNQXAC9IVRw&t=5s "), "watch url");
        Eq("https://youtu.be/jNQXAC9IVRw", AppLogic.NormalizeUrl("https://youtu.be/jNQXAC9IVRw"), "short url");
        Eq(null, AppLogic.NormalizeUrl("ftp://x.com/y"), "ftp rejected");
        Eq(null, AppLogic.NormalizeUrl("javascript:alert(1)"), "javascript rejected");
        Eq(null, AppLogic.NormalizeUrl("C:\\musica\\a.mp3"), "path is not a url");
        Eq(null, AppLogic.NormalizeUrl("https://x.com/\"; calc"), "quote rejected");
        Eq(null, AppLogic.NormalizeUrl(""), "empty");

        // TWATCH? reply parsing (SSID may contain spaces and even ' ip=').
        WatchStatus st = AppLogic.ParseTwatch("OK TWATCH 1.1 modo=PROGRAMACAO wifi=joaquim_iot ip=192.168.1.152 livre=9908224");
        True(st != null, "parse ok");
        Eq("1.1", st.Version, "version");
        Eq("PROGRAMACAO", st.Mode, "mode");
        Eq("joaquim_iot", st.Wifi, "ssid");
        Eq("192.168.1.152", st.Ip, "ip");
        Eq(9908224L, st.FreeBytes, "free");
        True(st.WifiConfigured && st.HasIp && st.InProgramMode, "flags");
        WatchStatus st2 = AppLogic.ParseTwatch("OK TWATCH 1.1 modo=REPRODUCAO wifi=minha rede ip=x ip=- livre=5");
        Eq("minha rede ip=x", st2.Wifi, "ssid with ' ip=' inside");
        Eq("-", st2.Ip, "ip dash");
        True(!st2.HasIp && !st2.InProgramMode, "flags 2");
        WatchStatus st3 = AppLogic.ParseTwatch("OK TWATCH 1.1 modo=REPRODUCAO wifi=- ip=- livre=0");
        True(!st3.WifiConfigured, "wifi not configured");
        Eq(null, AppLogic.ParseTwatch("ERRO ocupado"), "error line");
        Eq(null, AppLogic.ParseTwatch("OK TWATCH 1.1 modo=X wifi=a ip=b livre=abc"), "bad free");
        Eq(null, AppLogic.ParseTwatch(null), "null line");

        Console.WriteLine(failed == 0 ? "OK " + total + "/" + total : "FALHARAM " + failed + " de " + total);
        return failed == 0 ? 0 : 1;
    }
}
