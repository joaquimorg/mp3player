// Small key=value settings file next to the exe (last COM port, last watch IP, "force USB").
using System;
using System.IO;

public static class AppConfig
{
    public static string LastPort = "";
    public static string LastIp = "";
    public static bool ForceUsb;

    static string FilePath
    {
        get { return Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "twatch-config.txt"); }
    }

    public static void Load()
    {
        try
        {
            if (!File.Exists(FilePath)) return;
            foreach (string line in File.ReadAllLines(FilePath))
            {
                int eq = line.IndexOf('=');
                if (eq <= 0) continue;
                string k = line.Substring(0, eq).Trim();
                string v = line.Substring(eq + 1).Trim();
                if (k == "port") LastPort = v;
                else if (k == "ip") LastIp = v;
                else if (k == "usb") ForceUsb = v == "1";
            }
        }
        catch (Exception) { }
    }

    public static void Save()
    {
        try
        {
            File.WriteAllLines(FilePath, new string[] {
                "port=" + LastPort, "ip=" + LastIp, "usb=" + (ForceUsb ? "1" : "0") });
        }
        catch (Exception) { }
    }
}
