// Main window: three tabs (Relógio, Músicas, Ferramentas), built by the partial class files.
using System;
using System.Drawing;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

public partial class MainForm : Form
{
    TabControl tabs;

    // Only one operation may use the USB serial port at a time (a second open would fail and the
    // watch would look absent). Every serial task goes through UsbRun.
    readonly SemaphoreSlim usbLock = new SemaphoreSlim(1, 1);

    async Task UsbRun(Action a)
    {
        await usbLock.WaitAsync();
        try { await Task.Run(a); }
        finally { usbLock.Release(); }
    }

    async Task<T> UsbRun<T>(Func<T> f)
    {
        await usbLock.WaitAsync();
        try { return await Task.Run(f); }
        finally { usbLock.Release(); }
    }

    string[] autoArgs;

    public MainForm(string[] args)
    {
        AppConfig.Load();
        if (args != null && args.Length >= 3 && args[0] == "--autosend") autoArgs = args;
        Text = "T-Watch MP3";
        Font = new Font("Segoe UI", 10F);
        ClientSize = new Size(760, 600);
        MinimumSize = new Size(700, 560);
        StartPosition = FormStartPosition.CenterScreen;

        tabs = new TabControl();
        tabs.Dock = DockStyle.Fill;
        Controls.Add(tabs);

        BuildRelogioTab();
        BuildMusicasTab();
        BuildFerramentasTab();
        NameControls();

        Load += OnFormLoad;
        FormClosed += delegate { AppConfig.Save(); };
    }

    async void OnFormLoad(object sender, EventArgs e)
    {
        if (autoArgs != null) { await AutoSend(); return; }
        RelogioRefresh();
        MusicasRefresh();
        FerramentasRefresh();
    }

    // A read-only log box shared by the tabs; messages never contain Wi-Fi passwords.
    static TextBox NewLogBox()
    {
        TextBox t = new TextBox();
        t.Multiline = true;
        t.ReadOnly = true;
        t.ScrollBars = ScrollBars.Vertical;
        t.Font = new Font("Consolas", 9F);
        t.BackColor = Color.White;
        return t;
    }

    static void AppendLog(TextBox box, string line)
    {
        if (box.IsDisposed) return;
        box.AppendText(DateTime.Now.ToString("HH:mm:ss") + "  " + line + Environment.NewLine);
    }

    static string ModeName(string mode)
    {
        if (mode == "PROGRAMACAO") return "Programação";
        if (mode == "REPRODUCAO") return "Reprodução";
        return mode;
    }
}
