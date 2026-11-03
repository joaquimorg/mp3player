// Turns a link (yt-dlp) or an audio file (ffmpeg) into an MP3 in a temp folder: 44.1 kHz, 112 kbps;
// and the 64 kbps mono retry for files that do not fit. C# 5.
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;

public class Converter : IDisposable
{
    public string WorkDir;

    public Converter()
    {
        WorkDir = Path.Combine(Path.GetTempPath(), "twatch-" + Guid.NewGuid().ToString("N"));
        Directory.CreateDirectory(WorkDir);
    }

    public void Dispose()
    {
        try { if (Directory.Exists(WorkDir)) Directory.Delete(WorkDir, true); } catch (Exception) { }
    }

    static string Q(string s) { return AppLogic.QuoteArg(s); }

    static string Tail(List<string> lines)
    {
        StringBuilder sb = new StringBuilder();
        int from = Math.Max(0, lines.Count - 3);
        for (int i = from; i < lines.Count; i++) sb.Append(lines[i]).Append(' ');
        return sb.ToString().Trim();
    }

    string FirstMp3()
    {
        string[] f = Directory.GetFiles(WorkDir, "*.mp3");
        return f.Length > 0 ? f[0] : null;
    }

    // Link -> MP3 (the title becomes the display name). `status` gets short progress texts.
    public string FromUrl(string url, Action<string> log, Action<string> status, CancellationToken ct, out string title,
                          out string error)
    {
        title = null;
        error = null;
        string args = "-x --audio-format mp3 --postprocessor-args \"ffmpeg:-ar 44100 -b:a 112k\" --no-playlist --newline " +
                      "--windows-filenames --ffmpeg-location " + Q(Tools.Dir) + " -o " + Q(Path.Combine(WorkDir, "%(title).60s.%(ext)s")) +
                      " -- " + Q(url);
        List<string> lines = new List<string>();
        int code = Tools.Run(Tools.PathOf(Tools.YtDlpExe), args, delegate(string l)
        {
            lines.Add(l);
            if (l.StartsWith("[download]", StringComparison.Ordinal)) status(l.Substring(10).Trim());
            else if (l.StartsWith("[ExtractAudio]", StringComparison.Ordinal)) status("A converter para MP3...");
            else if (l.StartsWith("ERROR", StringComparison.Ordinal)) log(l);
        }, ct);
        if (code == -1) { error = "Cancelado."; return null; }
        string mp3 = FirstMp3();
        if (code != 0 || mp3 == null)
        {
            error = "O yt-dlp não conseguiu obter o áudio. " + Tail(lines);
            return null;
        }
        title = Path.GetFileNameWithoutExtension(mp3);
        return mp3;
    }

    // Any audio/video file -> MP3 (44.1 kHz, 112 kbps).
    public string FromFile(string path, Action<string> log, CancellationToken ct, out string error)
    {
        return Encode(path, "in", "-ar 44100 -b:a 112k", log, ct, out error);
    }

    // Retry for files that do not fit: 64 kbps mono.
    public string Reencode64kMono(string mp3, Action<string> log, CancellationToken ct, out string error)
    {
        return Encode(mp3, "mono64", "-ar 44100 -ac 1 -b:a 64k", log, ct, out error);
    }

    string Encode(string input, string tag, string audioArgs, Action<string> log, CancellationToken ct, out string error)
    {
        error = null;
        string outPath = Path.Combine(WorkDir, tag + "-" + Guid.NewGuid().ToString("N").Substring(0, 6) + ".mp3");
        string args = "-y -nostdin -hide_banner -loglevel error -i " + Q(input) + " -vn " + audioArgs + " " + Q(outPath);
        List<string> lines = new List<string>();
        int code = Tools.Run(Tools.PathOf(Tools.FfmpegExe), args, delegate(string l) { lines.Add(l); }, ct);
        if (code == -1) { error = "Cancelado."; return null; }
        if (code != 0 || !File.Exists(outPath))
        {
            error = "O ffmpeg não conseguiu converter o ficheiro. " + Tail(lines);
            return null;
        }
        return outPath;
    }
}
