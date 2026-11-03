// Pure application logic (no UI, no I/O): decisions, parsing and checksums shared by the
// Windows app and its console tests. C# 5 only (the csc.exe that ships with Windows).
using System;
using System.Collections.Generic;
using System.Text;

public enum Transport { Wifi, Usb }

// What to do with a freshly encoded MP3 given the free space on the watch.
public enum EncodePlan { Keep, Retry64kMono, DoesNotFit, TooLarge }

public class WatchStatus
{
    public string Version = "";
    public string Mode = "";     // REPRODUCAO | PROGRAMACAO
    public string Wifi = "-";    // SSID verbatim, "-" when not configured
    public string Ip = "-";
    public long FreeBytes;

    public bool WifiConfigured { get { return Wifi != "-" && Wifi.Length > 0; } }
    public bool HasIp { get { return Ip != "-" && Ip.Length > 0; } }
    public bool InProgramMode { get { return Mode == "PROGRAMACAO"; } }
}

public static class AppLogic
{
    // Same values as the firmware (slotlogic::kFsOverhead, the 12 MB sanity limit of the old .bat).
    public const long FsOverhead = 16384;
    public const long MaxSane = 12L * 1024 * 1024;
    public const int MaxNameBytes = 48;

    // Wi-Fi when the watch has a network configured and answers there; otherwise USB.
    public static Transport ChooseTransport(bool wifiConfigured, bool wifiReachable, bool forceUsb)
    {
        if (forceUsb || !wifiConfigured || !wifiReachable) return Transport.Usb;
        return Transport.Wifi;
    }

    public static bool Fits(long size, long freeBytes)
    {
        return size + FsOverhead <= freeBytes;
    }

    // After the first encode (44.1 kHz, 112 kbps).
    public static EncodePlan PlanAfterFirstEncode(long size112k, long freeBytes)
    {
        if (size112k <= MaxSane && Fits(size112k, freeBytes)) return EncodePlan.Keep;
        return EncodePlan.Retry64kMono;
    }

    // After the 64 kbps mono retry: nothing more to try.
    public static EncodePlan PlanAfterRetry(long size64k, long freeBytes)
    {
        if (size64k > MaxSane) return EncodePlan.TooLarge;
        if (!Fits(size64k, freeBytes)) return EncodePlan.DoesNotFit;
        return EncodePlan.Keep;
    }

    public static long MissingBytes(long size, long freeBytes)
    {
        long m = size + FsOverhead - freeBytes;
        return m < 0 ? 0 : m;
    }

    // "4,3 MB" (decimal comma) like the watch; below 100 KB: "48 KB".
    public static string FormatMB(long bytes)
    {
        if (bytes < 0) bytes = 0;
        if (bytes < 100L * 1024) return (bytes / 1024) + " KB";
        long tenths = (bytes * 10 + 524288) / 1048576;
        return (tenths / 10) + "," + (tenths % 10) + " MB";
    }

    // Display name for the slot: no path, none of ; , " & % ! ^ < > or control characters,
    // at most 48 UTF-8 bytes (never splits a code point); empty becomes "faixa".
    public static string SafeFileName(string name)
    {
        if (name == null) name = "";
        int cut = name.LastIndexOfAny(new char[] { '/', '\\' });
        if (cut >= 0) name = name.Substring(cut + 1);
        const string bad = ";,\"&%!^<>";
        StringBuilder sb = new StringBuilder();
        foreach (char c in name)
        {
            if (c < 0x20 || c == 0x7F) continue;
            if (bad.IndexOf(c) >= 0) continue;
            sb.Append(c);
        }
        string s = sb.ToString().Trim();
        s = CutUtf8(s, MaxNameBytes).Trim();
        return s.Length == 0 ? "faixa" : s;
    }

    // Longest prefix with at most maxBytes UTF-8 bytes, keeping surrogate pairs whole.
    public static string CutUtf8(string s, int maxBytes)
    {
        StringBuilder sb = new StringBuilder();
        int bytes = 0;
        for (int i = 0; i < s.Length; i++)
        {
            int len = char.IsHighSurrogate(s[i]) && i + 1 < s.Length ? 2 : 1;
            int n = Encoding.UTF8.GetByteCount(s.Substring(i, len));
            if (bytes + n > maxBytes) break;
            sb.Append(s, i, len);
            bytes += n;
            i += len - 1;
        }
        return sb.ToString();
    }

    // Upper-case hex of the UTF-8 bytes (the PUT name field).
    public static string NameHex(string name)
    {
        byte[] b = Encoding.UTF8.GetBytes(name);
        StringBuilder sb = new StringBuilder(b.Length * 2);
        foreach (byte x in b) sb.Append(x.ToString("X2"));
        return sb.ToString();
    }

    // CRC16-CCITT (poly 0x1021, init 0xFFFF), the serial block checksum (sent high byte first).
    public static ushort Crc16(byte[] data, int offset, int count)
    {
        ushort crc = 0xFFFF;
        for (int i = 0; i < count; i++)
        {
            crc ^= (ushort)(data[offset + i] << 8);
            for (int b = 0; b < 8; b++)
                crc = (crc & 0x8000) != 0 ? (ushort)((crc << 1) ^ 0x1021) : (ushort)(crc << 1);
        }
        return crc;
    }

    // Standard CRC32 (zlib); Crc32Update is incremental: start from 0, feed chunks.
    public static uint Crc32Update(uint crc, byte[] data, int offset, int count)
    {
        crc = ~crc;
        for (int i = 0; i < count; i++)
        {
            crc ^= data[offset + i];
            for (int b = 0; b < 8; b++)
                crc = (crc & 1) != 0 ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
        }
        return ~crc;
    }

    public static uint Crc32(byte[] data, int offset, int count)
    {
        return Crc32Update(0, data, offset, count);
    }

    // Same limits as the firmware: SSID 1-32 bytes without TAB/CR/LF/NUL; password empty or 8-63
    // bytes without CR/LF/NUL (a TAB inside the password is fine: the first TAB splits the fields).
    public static bool ValidWifi(string ssid, string pass, out string erro)
    {
        erro = null;
        if (ssid == null) ssid = "";
        if (pass == null) pass = "";
        int sb = Encoding.UTF8.GetByteCount(ssid);
        if (sb < 1 || sb > 32) { erro = "O nome da rede tem de ter entre 1 e 32 caracteres."; return false; }
        if (ssid.IndexOfAny(new char[] { '\t', '\r', '\n', '\0' }) >= 0)
        { erro = "O nome da rede não pode ter tabulações nem mudanças de linha."; return false; }
        int pb = Encoding.UTF8.GetByteCount(pass);
        if (pb != 0 && (pb < 8 || pb > 63)) { erro = "A palavra-passe tem de ter entre 8 e 63 caracteres (ou ficar vazia)."; return false; }
        if (pass.IndexOfAny(new char[] { '\r', '\n', '\0' }) >= 0)
        { erro = "A palavra-passe não pode ter mudanças de linha."; return false; }
        return true;
    }

    // Accepts http(s) links with a host (yt-dlp handles YouTube and others). Returns the trimmed
    // URL, or null for anything else (ftp:, javascript:, file paths, ...).
    public static string NormalizeUrl(string input)
    {
        if (input == null) return null;
        string s = input.Trim();
        if (s.Length == 0 || s.IndexOfAny(new char[] { '\r', '\n', '\0', '"' }) >= 0) return null;
        Uri u;
        if (!Uri.TryCreate(s, UriKind.Absolute, out u)) return null;
        if (u.Scheme != Uri.UriSchemeHttp && u.Scheme != Uri.UriSchemeHttps) return null;
        if (string.IsNullOrEmpty(u.Host) || u.Host.IndexOf('.') < 0) return null;
        return s;
    }

    // Parses "OK TWATCH <ver> modo=<M> wifi=<ssid, may contain spaces> ip=<ip|-> livre=<n>".
    // The SSID is everything between "wifi=" and the LAST " ip=".
    public static WatchStatus ParseTwatch(string line)
    {
        if (line == null) return null;
        line = line.Trim();
        const string head = "OK TWATCH ";
        if (!line.StartsWith(head, StringComparison.Ordinal)) return null;
        string rest = line.Substring(head.Length);
        int mi = rest.IndexOf(" modo=", StringComparison.Ordinal);
        int wi = rest.IndexOf(" wifi=", StringComparison.Ordinal);
        int ii = rest.LastIndexOf(" ip=", StringComparison.Ordinal);
        int li = rest.LastIndexOf(" livre=", StringComparison.Ordinal);
        if (mi < 0 || wi < mi || ii < wi || li < ii) return null;
        WatchStatus st = new WatchStatus();
        st.Version = rest.Substring(0, mi);
        st.Mode = rest.Substring(mi + 6, wi - (mi + 6));
        st.Wifi = rest.Substring(wi + 6, ii - (wi + 6));
        st.Ip = rest.Substring(ii + 4, li - (ii + 4));
        long free;
        if (!long.TryParse(rest.Substring(li + 7), out free)) return null;
        st.FreeBytes = free;
        return st;
    }
}
