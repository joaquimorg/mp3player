// Main window: a header with the watch state, two sections (Músicas first, then Definições) and
// drag-and-drop of files and links. The sections are built by the partial class files. C# 5.
using System;
using System.Drawing;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

public enum Conn { Searching, Usb, Wifi, None }

public partial class MainForm : Form
{
    string[] autoArgs;

    Panel header, navBar, content, pageMusic, pageSettings;
    Label hdrDot, hdrText;
    Pill navMusic, navSettings;
    TextBox logBox;   // technical details, shown under Definições > Avançado

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

    public MainForm(string[] args)
    {
        AppConfig.Load();
        if (args != null && args.Length >= 3 && args[0] == "--autosend") autoArgs = args;
        Text = "T-Watch MP3";
        Font = Theme.Normal;
        BackColor = Theme.Bg;
        // Fixed size: the layout is designed for it (no resizing or maximising).
        FormBorderStyle = FormBorderStyle.FixedSingle;
        MaximizeBox = false;
        ClientSize = new Size(840, 760);
        StartPosition = FormStartPosition.CenterScreen;
        AllowDrop = true;

        logBox = NewLogBox();

        // The pages first (docked Fill), then the bars on top of them.
        content = new Panel { Dock = DockStyle.Fill, BackColor = Theme.Bg };
        Controls.Add(content);
        pageMusic = NewPage();
        pageSettings = NewPage();
        content.Controls.Add(pageMusic);
        content.Controls.Add(pageSettings);

        navBar = new Panel { Dock = DockStyle.Top, Height = 58, BackColor = Theme.Bg };
        Controls.Add(navBar);
        header = new Panel { Dock = DockStyle.Top, Height = 74, BackColor = Color.White };
        Controls.Add(header);
        header.Paint += delegate(object s, PaintEventArgs e)
        {
            using (Pen p = new Pen(Theme.Line)) e.Graphics.DrawLine(p, 0, header.Height - 1, header.Width, header.Height - 1);
        };

        Label title = Theme.Label("T-Watch MP3", Theme.Title, Theme.Text);
        title.Location = new Point(24, 18);
        header.Controls.Add(title);
        hdrText = Theme.Label("", Theme.Semi, Theme.Muted);
        hdrDot = Theme.Label("●", new Font("Segoe UI", 14F), Theme.Muted);
        hdrText.Anchor = hdrDot.Anchor = AnchorStyles.Top | AnchorStyles.Right;
        header.Controls.Add(hdrDot);
        header.Controls.Add(hdrText);
        header.Resize += delegate { PlaceHeaderStatus(); };

        navMusic = NavPill("Músicas");
        navSettings = NavPill("Definições");
        navMusic.Location = new Point(24, 8);
        navSettings.Location = new Point(24 + navMusic.Width + 8, 8);
        navMusic.Click += delegate { ShowPage(true); };
        navSettings.Click += delegate { ShowPage(false); };
        navBar.Controls.Add(navMusic);
        navBar.Controls.Add(navSettings);

        BuildMusicasTab();
        BuildDefinicoesTab();
        NameControls();
        ShowPage(args == null || Array.IndexOf(args, "--definicoes") < 0);   // --definicoes: open on the settings page (screenshots)
        SetConn(Conn.Searching);

        EnableDrop(this);
        DragEnter += OnDragEnter;
        DragDrop += delegate(object s, DragEventArgs e) { HandleDrop(e.Data, 0); };

        Load += OnFormLoad;
        FormClosed += delegate { AppConfig.Save(); };
        // Closing the window during a send/conversion: cancel it so yt-dlp/ffmpeg are killed and the watch
        // sees a clean abort (its previous slot stays intact).
        FormClosing += delegate { if (musCts != null) { musCts.Cancel(); Thread.Sleep(1500); } };
    }

    static Panel NewPage()
    {
        Panel p = new Panel();
        p.Dock = DockStyle.Fill;
        p.AutoScroll = true;
        p.BackColor = Theme.Bg;
        p.Padding = new Padding(24, 6, 24, 12);
        return p;
    }

    // One column of full-width cards.
    static TableLayoutPanel NewColumn(Panel page)
    {
        TableLayoutPanel t = new TableLayoutPanel();
        t.Dock = DockStyle.Top;
        t.AutoSize = true;
        t.AutoSizeMode = AutoSizeMode.GrowAndShrink;
        t.ColumnCount = 1;
        t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        t.BackColor = Theme.Bg;
        page.Controls.Add(t);
        return t;
    }

    static void AddCard(TableLayoutPanel col, Control card)
    {
        card.Dock = DockStyle.Fill;
        col.RowCount++;
        col.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        col.Controls.Add(card, 0, col.RowCount - 1);
    }

    Pill NavPill(string text)
    {
        return new Pill(text);
    }

    void ShowPage(bool music)
    {
        pageMusic.Visible = music;
        pageSettings.Visible = !music;
        navMusic.Selected = music;
        navSettings.Selected = !music;
    }

    void PlaceHeaderStatus()
    {
        int right = header.Width - 24;
        hdrText.Location = new Point(right - hdrText.Width, 26);
        hdrDot.Location = new Point(hdrText.Left - hdrDot.Width - 2, 21);
    }

    // The watch state shown at the top right.
    void SetConn(Conn c)
    {
        switch (c)
        {
            case Conn.Usb: hdrText.Text = "Relógio ligado"; hdrDot.ForeColor = Theme.Ok; hdrText.ForeColor = Theme.Text; break;
            case Conn.Wifi: hdrText.Text = "Relógio ligado (Wi-Fi)"; hdrDot.ForeColor = Theme.Ok; hdrText.ForeColor = Theme.Text; break;
            case Conn.Searching: hdrText.Text = "A procurar o relógio..."; hdrDot.ForeColor = Theme.Warn; hdrText.ForeColor = Theme.Muted; break;
            default: hdrText.Text = "Relógio não encontrado"; hdrDot.ForeColor = Theme.Err; hdrText.ForeColor = Theme.Err; break;
        }
        PlaceHeaderStatus();
    }

    async void OnFormLoad(object sender, EventArgs e)
    {
        if (autoArgs != null) { await AutoSend(); return; }
        await RefreshWatchTask();
        FerramentasRefresh();
    }

    // ---- drag and drop ---------------------------------------------------------
    void EnableDrop(Control c)
    {
        if (!(c is SlotRow))
        {
            c.AllowDrop = true;
            c.DragEnter += OnDragEnter;
            c.DragDrop += delegate(object s, DragEventArgs e) { HandleDrop(e.Data, 0); };
        }
        foreach (Control k in c.Controls) EnableDrop(k);
    }

    void OnDragEnter(object sender, DragEventArgs e)
    {
        e.Effect = (e.Data.GetDataPresent(DataFormats.FileDrop) || e.Data.GetDataPresent(DataFormats.Text))
            ? DragDropEffects.Copy : DragDropEffects.None;
    }

    // A dropped file or link goes into the "adicionar" box; dropping on a slot also picks that slot.
    void HandleDrop(IDataObject d, int slot)
    {
        string text = null;
        if (d.GetDataPresent(DataFormats.FileDrop))
        {
            string[] files = (string[])d.GetData(DataFormats.FileDrop);
            if (files != null && files.Length > 0) text = files[0];
        }
        else if (d.GetDataPresent(DataFormats.Text)) text = ((string)d.GetData(DataFormats.Text)).Trim();
        if (string.IsNullOrEmpty(text) || musBusy) return;
        musFile.Text = text;
        if (slot > 0) PickSlot(slot);
        ShowPage(true);
        musStatus.Text = "Pronto: escolhe o lugar e carrega em «Adicionar ao relógio».";
    }

    // ---- shared helpers ----------------------------------------------------------
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr SendMessage(IntPtr hWnd, int msg, IntPtr wp, string lp);

    // Grey hint text inside an empty single-line TextBox.
    static void Cue(TextBox t, string hint)
    {
        EventHandler set = delegate { SendMessage(t.Handle, 0x1501, (IntPtr)1, hint); };
        if (t.IsHandleCreated) set(t, EventArgs.Empty);
        t.HandleCreated += set;
    }

    // A read-only log box for the technical details.
    static TextBox NewLogBox()
    {
        TextBox t = new TextBox();
        t.Multiline = true;
        t.ReadOnly = true;
        t.ScrollBars = ScrollBars.Vertical;
        t.Font = new Font("Consolas", 9F);
        t.BackColor = ColorTranslator.FromHtml("#F9FAFB");
        t.BorderStyle = BorderStyle.FixedSingle;
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
