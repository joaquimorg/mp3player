// "Músicas" section (the main page): the 4 slots, adding a song (link or file) and sending it to the
// watch by Wi-Fi when a network is saved, otherwise by USB. C# 5.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

public partial class MainForm
{
    SlotRow[] musRows = new SlotRow[4];
    Label musFree, musStatus;
    TextBox musFile;
    ComboBox musSlot;
    Button musSend, musCancel, musPick;
    ProgressBar musBar;
    Panel musProgress;
    CancellationTokenSource musCts;
    SlotInfo[] musSlots;
    long musFreeBytes;
    bool musBusy;
    bool musUserPicked;   // the user chose a destination (otherwise: the first empty slot)

    static TableLayoutPanel Stack()
    {
        TableLayoutPanel t = new TableLayoutPanel();
        t.Dock = DockStyle.Top;
        t.AutoSize = true;
        t.AutoSizeMode = AutoSizeMode.GrowAndShrink;
        t.ColumnCount = 1;
        t.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        t.BackColor = Theme.Card;
        t.Margin = new Padding(0);
        return t;
    }

    static void AddRow(TableLayoutPanel t, Control c, int bottom)
    {
        c.Margin = new Padding(0, 0, 0, bottom);
        t.RowCount++;
        t.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        t.Controls.Add(c, 0, t.RowCount - 1);
    }

    void BuildMusicasTab()
    {
        TableLayoutPanel col = NewColumn(pageMusic);

        // ---- the 4 songs ----
        Card songs = new Card();
        TableLayoutPanel st = Stack();
        TableLayoutPanel head = new TableLayoutPanel();
        head.Dock = DockStyle.Top;
        head.AutoSize = true;
        head.ColumnCount = 2;
        head.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        head.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        head.BackColor = Theme.Card;
        head.Controls.Add(Theme.Label("As tuas músicas", Theme.Big, Theme.Text), 0, 0);
        musFree = Theme.Label("", Theme.Small, Theme.Muted);
        musFree.Anchor = AnchorStyles.Right;
        head.Controls.Add(musFree, 1, 0);
        AddRow(st, head, 12);
        for (int i = 0; i < 4; i++)
        {
            SlotRow row = new SlotRow(i + 1);
            row.Dock = DockStyle.Fill;
            row.Picked += PickSlot;
            row.DeleteClicked += MusicasDelete;
            row.Dropped += delegate(int n, IDataObject d) { HandleDrop(d, n); };
            musRows[i] = row;
            AddRow(st, row, 0);
        }
        songs.Controls.Add(st);
        AddCard(col, songs);

        // ---- add a song ----
        Card add = new Card();
        TableLayoutPanel at = Stack();
        AddRow(at, Theme.Label("Adicionar uma música", Theme.Big, Theme.Text), 4);
        Label hint = Theme.Label("Cola um link (por exemplo do YouTube) ou escolhe um ficheiro de áudio do computador. " +
                                 "Também podes arrastar um ficheiro ou um link para esta janela.", Theme.Small, Theme.Muted);
        hint.MaximumSize = new Size(700, 0);
        AddRow(at, hint, 12);

        TableLayoutPanel input = new TableLayoutPanel();
        input.Dock = DockStyle.Top;
        input.AutoSize = true;
        input.ColumnCount = 2;
        input.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        input.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        input.BackColor = Theme.Card;
        musFile = new TextBox();
        musFile.Font = new Font("Segoe UI", 11.5F);
        musFile.BorderStyle = BorderStyle.FixedSingle;
        musFile.Dock = DockStyle.Fill;
        musFile.Margin = new Padding(0, 2, 10, 0);
        Cue(musFile, "Cola aqui o link ou o caminho do ficheiro");
        input.Controls.Add(musFile, 0, 0);
        musPick = Theme.SecondaryButton("Escolher ficheiro...");
        musPick.Width = 170;
        musPick.Margin = new Padding(0);
        musPick.Click += MusicasPick;
        input.Controls.Add(musPick, 1, 0);
        AddRow(at, input, 12);

        // Fixed-height row (a flow panel clipped the bottom of the combo box).
        Panel dest = new Panel();
        dest.Height = 40;
        dest.Dock = DockStyle.Fill;
        dest.BackColor = Theme.Card;
        Label lbl = Theme.Label("Guardar em:", Theme.Normal, Theme.Text);
        lbl.Location = new Point(0, 8);
        dest.Controls.Add(lbl);
        musSlot = new ComboBox();
        musSlot.DropDownStyle = ComboBoxStyle.DropDownList;
        musSlot.Font = Theme.Normal;
        musSlot.Location = new Point(lbl.PreferredWidth + 12, 3);
        musSlot.Width = 380;
        musSlot.SelectedIndexChanged += delegate { SyncSlotSelection(); };
        dest.Controls.Add(musSlot);
        AddRow(at, dest, 10);

        musSend = Theme.PrimaryButton("Adicionar ao relógio");
        musSend.Dock = DockStyle.Fill;
        musSend.Margin = new Padding(0);
        musSend.Click += MusicasSend;
        AddRow(at, musSend, 10);

        musStatus = Theme.Label("", Theme.Normal, Theme.Muted);
        musStatus.MaximumSize = new Size(700, 0);
        AddRow(at, musStatus, 8);

        musProgress = new Panel();
        musProgress.Height = 40;
        musProgress.Dock = DockStyle.Fill;
        musProgress.BackColor = Theme.Card;
        musProgress.Visible = false;
        musCancel = Theme.SecondaryButton("Cancelar");
        musCancel.Width = 110;
        musCancel.Dock = DockStyle.Right;
        musCancel.Click += delegate { if (musCts != null) musCts.Cancel(); };
        musBar = new ProgressBar();
        musBar.Style = ProgressBarStyle.Continuous;
        musBar.Minimum = 0;
        musBar.Maximum = 1000;
        musBar.Dock = DockStyle.Fill;
        Panel barHost = new Panel { Dock = DockStyle.Fill, Padding = new Padding(0, 8, 14, 8), BackColor = Theme.Card };
        barHost.Controls.Add(musBar);
        musProgress.Controls.Add(barHost);
        musProgress.Controls.Add(musCancel);
        AddRow(at, musProgress, 0);
        add.Controls.Add(at);
        AddCard(col, add);

        FillSlots();
    }

    // Stable names so UI automation (tests) can find the controls.
    void NameControls()
    {
        musFile.Name = "musFile"; musSlot.Name = "musSlot"; musSend.Name = "musSend"; musStatus.Name = "musStatus";
        logBox.Name = "logBox"; musForceUsb.Name = "musForceUsb";
    }

    void MusLog(string s) { AppendLog(logBox, s); }

    void WatchChanged() { MusicasRefresh(); }

    // ---- slots ---------------------------------------------------------------------
    // Clicking a slot (or dropping a file on it) makes it the destination.
    void PickSlot(int n)
    {
        if (musBusy || n < 1 || n > 4) return;
        musUserPicked = true;
        if (musSlot.SelectedIndex != n - 1) musSlot.SelectedIndex = n - 1;
        SyncSlotSelection();
    }

    void SyncSlotSelection()
    {
        for (int i = 0; i < 4; i++)
        {
            bool sel = musSlot.SelectedIndex == i;
            if (musRows[i].Selected != sel) { musRows[i].Selected = sel; musRows[i].Invalidate(); }
        }
    }

    void FillSlots()
    {
        int keep = musSlot.SelectedIndex;
        musSlot.Items.Clear();
        for (int i = 0; i < 4; i++)
        {
            SlotInfo si = musSlots == null ? null : musSlots[i];
            musRows[i].SetInfo(si);
            string text = "Lugar " + (i + 1) + " - ";
            if (si == null || si.Empty) text += "vazio";
            else text += si.Name;
            musSlot.Items.Add(text);
        }
        int pick = keep;
        if (!musUserPicked || pick < 0)
        {
            pick = 0;
            if (musSlots != null)
                for (int i = 0; i < 4; i++) if (musSlots[i].Empty) { pick = i; break; }
        }
        musSlot.SelectedIndex = pick;
        SyncSlotSelection();
        musFree.Text = musSlots == null ? "" : "Espaço livre: " + AppLogic.FormatMB(musFreeBytes);
    }

    // Possible Wi-Fi addresses, most likely first.
    List<string> WifiHosts()
    {
        List<string> h = new List<string>();
        h.Add("twatch.local");
        if (!string.IsNullOrEmpty(AppConfig.LastIp)) h.Add(AppConfig.LastIp);
        if (watch != null && watch.HasIp) h.Add(watch.Ip);
        return h;
    }

    WatchApi FindApi()
    {
        WatchApi api = WatchApi.Find(WifiHosts(), delegate(string m) { BeginInvoke((Action)delegate { MusLog(m); }); });
        if (api != null)
        {
            string h = api.Base.Substring("http://".Length);
            if (h != "twatch.local") { AppConfig.LastIp = h; AppConfig.Save(); }
        }
        return api;
    }

    // Reads the slots over USB when the watch is plugged in, else over Wi-Fi.
    async Task<bool> LoadSlotsAsync()
    {
        SlotInfo[] slots = null;
        long free = 0;
        Conn via = Conn.None;
        string port = watchPort;
        await UsbRun(delegate
        {
            if (port != null)
            {
                string err;
                using (SerialLink l = SerialLink.TryOpen(port, 115200, out err))
                {
                    if (l != null) { slots = l.ListSlots(out free); if (slots != null) via = Conn.Usb; }
                }
            }
            if (slots == null)
            {
                WatchApi api = FindApi();
                if (api != null)
                {
                    string e;
                    slots = api.GetSlots(out free, out e);
                    if (slots != null) via = Conn.Wifi;
                }
            }
        });
        musSlots = slots;
        musFreeBytes = free;
        FillSlots();
        SetConn(via);
        return slots != null;
    }

    async void MusicasRefresh()
    {
        if (musBusy) return;
        await LoadSlotsAsync();
    }

    void MusicasPick(object sender, EventArgs e)
    {
        OpenFileDialog d = new OpenFileDialog();
        d.Title = "Escolhe um ficheiro de áudio";
        d.Filter = "Áudio (*.mp3;*.m4a;*.wav;*.flac;*.ogg;*.opus;*.mp4;*.webm)|*.mp3;*.m4a;*.wav;*.flac;*.ogg;*.opus;*.mp4;*.webm|Todos os ficheiros|*.*";
        if (d.ShowDialog(this) == DialogResult.OK) musFile.Text = d.FileName;
    }

    async void MusicasDelete(int n)
    {
        if (musBusy || musSlots == null || musSlots[n - 1].Empty) return;
        if (MessageBox.Show(this, "Apagar «" + musSlots[n - 1].Name + "» do relógio?", "Apagar música",
                            MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
        SetBusy(true);
        musStatus.Text = "A apagar...";
        string port = watchPort;
        string result = null;
        await UsbRun(delegate
        {
            string err;
            if (port != null)
            {
                using (SerialLink l = SerialLink.TryOpen(port, 115200, out err))
                {
                    if (l != null) { string r = l.DeleteSlot(n); result = r == "OK" ? null : SerialLink.DescribeError(r); return; }
                }
            }
            WatchApi api = FindApi();
            if (api == null) { result = "Não encontro o relógio. Liga-o ao computador com o cabo USB."; return; }
            string e2;
            if (!api.Delete(n, out e2)) result = e2;
        });
        if (result != null) { MusLog("Não foi possível apagar: " + result); musStatus.Text = "Não consegui apagar: " + result; }
        else { MusLog("Slot " + n + " apagado."); musStatus.Text = "Música apagada."; }
        SetBusy(false);
        await LoadSlotsAsync();
    }

    void SetBusy(bool busy)
    {
        musBusy = busy;
        musSend.Enabled = !busy;
        musPick.Enabled = !busy;
        musFile.Enabled = !busy;
        musSlot.Enabled = !busy;
        foreach (SlotRow r in musRows) r.DeleteButton.Enabled = !busy;
        musCancel.Enabled = busy && musCts != null;
        musProgress.Visible = busy && musCts != null;
        if (!busy) musBar.Value = 0;
    }

    void SetProgress(long done, long total, string what)
    {
        BeginInvoke((Action)delegate
        {
            int v = total <= 0 ? 0 : (int)Math.Min(1000, done * 1000 / total);
            musBar.Value = v;
            musStatus.Text = what + "  " + (v / 10) + "%  (" + AppLogic.FormatMB(done) + " de " + AppLogic.FormatMB(total) + ")";
        });
    }

    async void MusicasSend(object sender, EventArgs e)
    {
        await MusicasSendTask();
    }

    // Used by the test mode: --autosend <file-or-link> <slot> [usb] runs the same flow as the button and
    // writes the details log to twatch-autosend.log next to the exe, then closes.
    async Task AutoSend()
    {
        await RefreshWatchTask();
        musFile.Text = autoArgs[1];
        musUserPicked = true;
        musSlot.SelectedIndex = int.Parse(autoArgs[2]) - 1;
        musForceUsb.Checked = autoArgs.Length > 3 && autoArgs[3] == "usb";
        await MusicasSendTask();
        File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "twatch-autosend.log"),
                          logBox.Text + Environment.NewLine + "ESTADO " + musStatus.Text);
        Close();
    }

    void SetStatus(string s)
    {
        BeginInvoke((Action)delegate { musStatus.Text = s; });
    }

    // yt-dlp progress ("45.2% of 3.4MiB at ...") -> "A descarregar o áudio... 45%".
    static string FriendlyDownload(string s)
    {
        int sp = s.IndexOf(' ');
        string first = sp > 0 ? s.Substring(0, sp) : s;
        if (first.EndsWith("%")) return "A descarregar o áudio... " + first.Split('.')[0] + "%";
        return s.StartsWith("A ") ? s : "A descarregar o áudio...";
    }

    async Task MusicasSendTask()
    {
        if (musBusy) return;
        string input = musFile.Text.Trim().Trim('"');
        string url = AppLogic.NormalizeUrl(input);
        bool isFile = url == null && File.Exists(input);
        if (url == null && !isFile)
        {
            MessageBox.Show(this, "Escreve um link (YouTube, por exemplo) ou escolhe um ficheiro de áudio que exista.", "Enviar",
                            MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        bool isMp3 = isFile && input.EndsWith(".mp3", StringComparison.OrdinalIgnoreCase);
        bool needYt = url != null;
        bool needFf = url != null || !isMp3;
        if (!await EnsureTools(needYt, needFf)) return;

        int slot = musSlot.SelectedIndex + 1;
        if (musSlots != null && !musSlots[slot - 1].Empty &&
            MessageBox.Show(this, "O lugar " + slot + " já tem «" + musSlots[slot - 1].Name + "». Queres substituí-la?",
                            "Substituir música", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
        musCts = new CancellationTokenSource();
        CancellationToken ct = musCts.Token;
        SetBusy(true);
        try
        {
            using (Converter cv = new Converter())
            {
                string mp3 = null, display = null, err = null;
                if (url != null)
                {
                    SetStatus("A descarregar o áudio...");
                    MusLog("A descarregar e converter: " + url);
                    string title = null;
                    mp3 = await Task.Run(delegate
                    {
                        string t, e2;
                        string r = cv.FromUrl(url, delegate(string m) { BeginInvoke((Action)delegate { MusLog(m); }); }, delegate(string st) { SetStatus(FriendlyDownload(st)); }, ct, out t, out e2);
                        title = t; err = e2;
                        return r;
                    });
                    display = title;
                }
                else if (isMp3)
                {
                    mp3 = input;
                    display = Path.GetFileNameWithoutExtension(input);
                }
                else
                {
                    SetStatus("A converter o ficheiro para MP3...");
                    MusLog("A converter " + Path.GetFileName(input) + " para MP3 (44,1 kHz, 112 kbps)...");
                    mp3 = await Task.Run(delegate
                    {
                        string e2;
                        string r = cv.FromFile(input, delegate(string m) { }, ct, out e2);
                        err = e2;
                        return r;
                    });
                    display = Path.GetFileNameWithoutExtension(input);
                }
                if (mp3 == null) { MusLog(err == "Cancelado." ? "Cancelado." : "Falhou: " + err); return; }

                // Will it fit? Plan against the free space (a song already in the slot can be replaced).
                SetStatus("A ler o relógio...");
                if (!await LoadSlotsAsync() || musSlots == null)
                {
                    MessageBox.Show(this, "Não consigo falar com o relógio. Liga-o ao computador com o cabo USB e tenta outra vez.",
                                    "Enviar", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    return;
                }
                SlotInfo cur = musSlots[slot - 1];
                long freeEff = musFreeBytes + (cur.Empty ? 0 : cur.Size);
                long size = new FileInfo(mp3).Length;
                if (AppLogic.PlanAfterFirstEncode(size, freeEff) == EncodePlan.Retry64kMono)
                {
                    if (!await EnsureTools(false, true)) return;
                    MusLog("Com " + AppLogic.FormatMB(size) + " não cabe. A recodificar a 64 kbps mono...");
                    SetStatus("A comprimir a música para caber no relógio...");
                    string m64 = await Task.Run(delegate
                    {
                        string e2;
                        string r = cv.Reencode64kMono(mp3, delegate(string m) { }, ct, out e2);
                        err = e2;
                        return r;
                    });
                    if (m64 == null) { MusLog(err == "Cancelado." ? "Cancelado." : "Falhou: " + err); return; }
                    long size2 = new FileInfo(m64).Length;
                    EncodePlan p2 = AppLogic.PlanAfterRetry(size2, freeEff);
                    if (p2 == EncodePlan.TooLarge)
                    {
                        MessageBox.Show(this, "A música é demasiado grande (mais de " + AppLogic.FormatMB(AppLogic.MaxSane) + ") mesmo comprimida ao máximo.",
                                        "Enviar", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return;
                    }
                    if (p2 == EncodePlan.DoesNotFit)
                    {
                        MessageBox.Show(this, "Não cabe, mesmo comprimida ao máximo: faltam " + AppLogic.FormatMB(AppLogic.MissingBytes(size2, freeEff)) +
                                        " no relógio. Apaga outra música e tenta de novo.", "Enviar", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return;
                    }
                    MusLog("Ficou com " + AppLogic.FormatMB(size2) + " (64 kbps mono).");
                    mp3 = m64;
                }
                await DoSend(mp3, slot, display);
            }
        }
        catch (Exception ex) { MusLog("Erro inesperado: " + ex.Message); }
        finally { musCts = null; SetBusy(false); musStatus.Text = ""; }
        await LoadSlotsAsync();
    }

    // Sends a ready MP3 to a slot: checks the space, chooses Wi-Fi or USB, retries on link problems.
    async Task DoSend(string path, int slot, string displayName)
    {
        long size = new FileInfo(path).Length;
        musStatus.Text = "A ler o relógio...";
        if (!await LoadSlotsAsync() || musSlots == null)
        {
            MessageBox.Show(this, "Não consigo falar com o relógio. Liga-o ao computador com o cabo USB e tenta outra vez.",
                            "Enviar", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        SlotInfo cur = musSlots[slot - 1];
        bool deleteFirst = false;
        if (!AppLogic.Fits(size, musFreeBytes))
        {
            if (!cur.Empty && AppLogic.Fits(size, musFreeBytes + cur.Size))
            {
                if (MessageBox.Show(this, "Para caber, «" + cur.Name + "» (lugar " + slot + ") tem de ser apagada antes. Apagar e enviar?",
                                    "Enviar", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
                deleteFirst = true;
            }
            else
            {
                MessageBox.Show(this, "Não cabe: faltam " + AppLogic.FormatMB(AppLogic.MissingBytes(size, musFreeBytes)) +
                                " no relógio. Apaga outra música ou usa um ficheiro mais pequeno.", "Enviar",
                                MessageBoxButtons.OK, MessageBoxIcon.Warning);
                return;
            }
        }

        // Wi-Fi if the watch has a network configured and answers there, otherwise USB.
        bool wifiConfigured = watch != null && watch.WifiConfigured;
        bool forceUsb = musForceUsb.Checked;
        WatchApi api = null;
        if (!forceUsb && wifiConfigured)
        {
            if (!watch.InProgramMode && watchPort != null)
            {
                MusLog("A preparar o relógio para receber por Wi-Fi...");
                musStatus.Text = "A preparar o relógio...";
                await SwitchMode(true);
            }
            musStatus.Text = "A procurar o relógio na rede...";
            api = await Task.Run(delegate { return FindApi(); });
        }
        Transport tr = AppLogic.ChooseTransport(wifiConfigured, api != null, forceUsb);
        if (tr == Transport.Usb && wifiConfigured && !forceUsb)
        {
            if (MessageBox.Show(this, "O relógio não respondeu pelo Wi-Fi. Queres enviar pelo cabo USB?", "Enviar",
                                MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
        }
        if (tr == Transport.Usb && watchPort == null)
        {
            MessageBox.Show(this, "Liga o relógio ao computador com o cabo USB e tenta outra vez.", "Enviar",
                            MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }

        if (deleteFirst)
        {
            string derr = null;
            await UsbRun(delegate
            {
                if (tr == Transport.Wifi) { string e2; if (!api.Delete(slot, out e2)) derr = e2; }
                else
                {
                    string err;
                    using (SerialLink l = SerialLink.TryOpen(watchPort, 115200, out err))
                    {
                        if (l == null) derr = err;
                        else { string r = l.DeleteSlot(slot); if (r != "OK") derr = SerialLink.DescribeError(r); }
                    }
                }
            });
            if (derr != null) { MusLog("Não foi possível apagar o slot " + slot + ": " + derr); return; }
            MusLog("Slot " + slot + " apagado.");
        }

        string name = AppLogic.SafeFileName(displayName);
        CancellationToken ct = musCts.Token;
        PutResult res = PutResult.Failed;
        string msg = null;
        MusLog("A enviar \"" + name + "\" (" + AppLogic.FormatMB(size) + ") para o slot " + slot + (tr == Transport.Wifi ? " por Wi-Fi..." : " por USB..."));
        if (tr == Transport.Wifi)
        {
            for (int attempt = 1; attempt <= 3; attempt++)
            {
                string m = null;
                res = await Task.Run(delegate
                {
                    return api.Upload(path, slot, name, delegate(long b) { SetProgress(b, size, "A enviar para o relógio"); }, ct, out m);
                });
                msg = m;
                if (res != PutResult.LinkProblem) break;
                MusLog(msg + " A tentar de novo (" + attempt + " de 3)...");
                bool back = await Task.Run(delegate { return api.WaitUntilAnswers(90000, ct); });
                if (!back) break;
            }
        }
        else
        {
            bool fast = true;
            for (int attempt = 1; attempt <= 2; attempt++)
            {
                string m = null;
                bool f = fast;
                res = await UsbRun<PutResult>(delegate
                {
                    string err;
                    using (SerialLink l = SerialLink.TryOpen(watchPort, 115200, out err))
                    {
                        if (l == null) { m = err; return PutResult.Failed; }
                        return l.PutFile(path, slot, name, f, delegate(long b) { SetProgress(b, size, "A enviar para o relógio"); }, ct, out m);
                    }
                });
                msg = m;
                if (res != PutResult.LinkProblem) break;
                MusLog(msg + " A tentar à velocidade normal...");
                fast = false;
            }
        }
        if (res == PutResult.Ok)
        {
            MusLog("Concluído: \"" + name + "\" está no slot " + slot + ".");
            musBar.Value = 1000;
        }
        else MusLog((res == PutResult.Cancelled ? "" : "Falhou: ") + msg);
    }

    // Switches the watch's mode over USB and waits for it (updates `watch`).
    async Task SwitchMode(bool program)
    {
        string port = watchPort;
        WatchStatus st = null;
        await UsbRun(delegate
        {
            string err;
            using (SerialLink l = SerialLink.TryOpen(port, 115200, out err))
            {
                if (l == null) return;
                if (l.SetMode(program) == "OK")
                    st = l.WaitStatus(delegate(WatchStatus s)
                    {
                        return program ? (s.InProgramMode && (s.HasIp || !s.WifiConfigured)) : !s.InProgramMode;
                    }, 60000);
            }
        });
        if (st != null)
        {
            watch = st;
            if (st.HasIp) { AppConfig.LastIp = st.Ip; AppConfig.Save(); }
        }
    }
}
