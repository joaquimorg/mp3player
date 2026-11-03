// Main window: three tabs (Relógio, Músicas, Ferramentas), built by the partial class files.
using System;
using System.Drawing;
using System.Windows.Forms;

public partial class MainForm : Form
{
    TabControl tabs;

    public MainForm()
    {
        AppConfig.Load();
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

        Load += OnFormLoad;
        FormClosed += delegate { AppConfig.Save(); };
    }

    void OnFormLoad(object sender, EventArgs e)
    {
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
