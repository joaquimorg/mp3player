// yt-dlp.exe and ffmpeg.exe: presence, version, download and update (official HTTPS sources, same as
// tools\pc\obter-ferramentas.bat). They live next to the app. C# 5.
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Net;
using System.Text;
using System.Threading;

public static class Tools
{
    public const string YtDlpExe = "yt-dlp.exe";
    public const string FfmpegExe = "ffmpeg.exe";
    const string YtDlpUrl = "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe";
    const string FfmpegUrl = "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip";

    public static string Dir { get { return AppDomain.CurrentDomain.BaseDirectory; } }

    public static string PathOf(string exe) { return Path.Combine(Dir, exe); }

    public static bool Have(string exe) { return File.Exists(PathOf(exe)); }

    // Runs a program and returns its first output line (version), or null.
    public static string Version(string exe)
    {
        if (!Have(exe)) return null;
        try
        {
            ProcessStartInfo psi = new ProcessStartInfo(PathOf(exe), exe == FfmpegExe ? "-version" : "--version");
            psi.UseShellExecute = false;
            psi.CreateNoWindow = true;
            psi.RedirectStandardOutput = true;
            using (Process p = Process.Start(psi))
            {
                string line = p.StandardOutput.ReadLine();
                if (!p.WaitForExit(8000)) { try { p.Kill(); } catch (Exception) { } }
                return line == null ? null : line.Trim();
            }
        }
        catch (Exception) { return null; }
    }

    static WebClient NewClient()
    {
        ServicePointManager.SecurityProtocol = (SecurityProtocolType)3072;   // TLS 1.2
        WebClient c = new WebClient();
        c.Proxy = null;
        return c;
    }

    // Downloads to `<exe>.new` and replaces the file only when the download is complete.
    public static string DownloadYtDlp(Action<string> log)
    {
        string tmp = PathOf(YtDlpExe) + ".new";
        try
        {
            log("A descarregar o yt-dlp...");
            using (WebClient c = NewClient()) c.DownloadFile(YtDlpUrl, tmp);
            if (new FileInfo(tmp).Length < 1000000) { File.Delete(tmp); return "O ficheiro descarregado do yt-dlp é demasiado pequeno."; }
            File.Copy(tmp, PathOf(YtDlpExe), true);
            File.Delete(tmp);
            return null;
        }
        catch (Exception e)
        {
            try { if (File.Exists(tmp)) File.Delete(tmp); } catch (Exception) { }
            return "Falhou o descarregamento do yt-dlp: " + e.Message;
        }
    }

    // Downloads the ffmpeg zip and extracts only bin\ffmpeg.exe.
    public static string DownloadFfmpeg(Action<string> log)
    {
        string zip = Path.Combine(Path.GetTempPath(), "twatch-ffmpeg-" + Guid.NewGuid().ToString("N") + ".zip");
        try
        {
            log("A descarregar o ffmpeg (cerca de 80 MB, pode demorar)...");
            using (WebClient c = NewClient()) c.DownloadFile(FfmpegUrl, zip);
            using (ZipArchive za = ZipFile.OpenRead(zip))
            {
                foreach (ZipArchiveEntry en in za.Entries)
                {
                    string n = en.FullName.Replace('\\', '/');
                    if (n.EndsWith("/bin/ffmpeg.exe", StringComparison.OrdinalIgnoreCase))
                    {
                        string tmp = PathOf(FfmpegExe) + ".new";
                        en.ExtractToFile(tmp, true);
                        File.Copy(tmp, PathOf(FfmpegExe), true);
                        File.Delete(tmp);
                        return null;
                    }
                }
            }
            return "O ficheiro do ffmpeg não tem bin\\ffmpeg.exe.";
        }
        catch (Exception e) { return "Falhou o descarregamento do ffmpeg: " + e.Message; }
        finally { try { if (File.Exists(zip)) File.Delete(zip); } catch (Exception) { } }
    }

    // Runs a child process, forwarding every output line; kills the whole tree on cancel.
    // Returns the exit code (or -1 when cancelled).
    public static int Run(string exe, string args, Action<string> onLine, CancellationToken ct)
    {
        ProcessStartInfo psi = new ProcessStartInfo(exe, args);
        psi.UseShellExecute = false;
        psi.CreateNoWindow = true;
        psi.RedirectStandardOutput = true;
        psi.RedirectStandardError = true;
        psi.StandardOutputEncoding = Encoding.UTF8;
        psi.StandardErrorEncoding = Encoding.UTF8;
        psi.WorkingDirectory = Dir;
        using (Process p = new Process())
        {
            p.StartInfo = psi;
            DataReceivedEventHandler h = delegate(object s, DataReceivedEventArgs e) { if (e.Data != null && onLine != null) onLine(e.Data); };
            p.OutputDataReceived += h;
            p.ErrorDataReceived += h;
            p.Start();
            p.BeginOutputReadLine();
            p.BeginErrorReadLine();
            while (!p.WaitForExit(200))
            {
                if (ct.IsCancellationRequested)
                {
                    KillTree(p.Id);
                    return -1;
                }
            }
            p.WaitForExit();
            return p.ExitCode;
        }
    }

    static void KillTree(int pid)
    {
        try
        {
            ProcessStartInfo k = new ProcessStartInfo("taskkill.exe", "/PID " + pid + " /T /F");
            k.UseShellExecute = false;
            k.CreateNoWindow = true;
            using (Process kp = Process.Start(k)) kp.WaitForExit(5000);
        }
        catch (Exception) { }
    }
}
