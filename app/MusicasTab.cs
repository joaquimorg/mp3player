// "Músicas" tab: the 4 slots, delete, and sending a song by Wi-Fi (when configured) or by USB.
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Forms;

public partial class MainForm
{
    ListView musList;
    Label musFree, musSource, musStatus;
    TextBox musFile, musLog;
    ComboBox musSlot;
    CheckBox musForceUsb;
    Button musSend, musCancel, musPick, musDelete, musRefresh;
    ProgressBar musBar;
    CancellationTokenSource musCts;
    SlotInfo[] musSlots;
    long musFreeBytes;
    bool musBusy;

    void BuildMusicasTab()
    {
        TabPage page = new TabPage("Músicas");
        page.Padding = new Padding(12);
        tabs.TabPages.Add(page);

        TableLayoutPanel t = new TableLayoutPanel();
        t.Dock = DockStyle.Fill;
        t.ColumnCount = 1;
        t.RowCount = 3;
        t.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        t.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        t.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        page.Controls.Add(t);

        // Slots.
        GroupBox gSlots = new GroupBox();
        gSlots.Text = "Músicas no relógio";
        gSlots.Dock = DockStyle.Top;
        gSlots.Height = 214;
        gSlots.Padding = new Padding(10);
        musList = new ListView();
        musList.View = View.Details;
        musList.FullRowSelect = true;
        musList.MultiSelect = false;
        musList.HideSelection = false;
        musList.Columns.Add("Slot", 50);
        musList.Columns.Add("Nome", 420);
        musList.Columns.Add("Tamanho", 90);
        musList.Dock = DockStyle.Top;
        musList.Height = 110;
        FlowLayoutPanel fb = new FlowLayoutPanel();
        fb.Dock = DockStyle.Top;
        fb.AutoSize = true;
        musRefresh = new Button { Text = "Atualizar", AutoSize = true };
        musRefresh.Click += delegate { MusicasRefresh(); };
        musDelete = new Button { Text = "Apagar a selecionada", AutoSize = true };
        musDelete.Click += MusicasDelete;
        musFree = new Label { Text = "", AutoSize = true, Padding = new Padding(10, 6, 0, 0) };
        musSource = new Label { Text = "", AutoSize = true, ForeColor = Color.DimGray, Padding = new Padding(10, 6, 0, 0) };
        fb.Controls.Add(musRefresh);
        fb.Controls.Add(musDelete);
        fb.Controls.Add(musFree);
        fb.Controls.Add(musSource);
        gSlots.Controls.Add(fb);
        gSlots.Controls.Add(musList);
        t.Controls.Add(gSlots, 0, 0);

        // Send.
        GroupBox gSend = new GroupBox();
        gSend.Text = "Enviar uma música";
        gSend.Dock = DockStyle.Top;
        gSend.AutoSize = true;
        gSend.Padding = new Padding(10);
        TableLayoutPanel s = new TableLayoutPanel();
        s.Dock = DockStyle.Top;
        s.AutoSize = true;
        s.ColumnCount = 3;
        s.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        s.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        s.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        s.Controls.Add(new Label { Text = "Link ou ficheiro:", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 0);
        musFile = new TextBox { Dock = DockStyle.Fill };
        s.Controls.Add(musFile, 1, 0);
        musPick = new Button { Text = "Escolher ficheiro...", AutoSize = true };
        musPick.Click += MusicasPick;
        s.Controls.Add(musPick, 2, 0);
        s.Controls.Add(new Label { Text = "Slot:", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 1);
        musSlot = new ComboBox { DropDownStyle = ComboBoxStyle.DropDownList, Width = 70 };
        musSlot.Items.AddRange(new object[] { "1", "2", "3", "4" });
        musSlot.SelectedIndex = 0;
        s.Controls.Add(musSlot, 1, 1);
        musForceUsb = new CheckBox { Text = "Forçar USB (não usar o Wi-Fi)", AutoSize = true, Checked = AppConfig.ForceUsb };
        musForceUsb.CheckedChanged += delegate { AppConfig.ForceUsb = musForceUsb.Checked; AppConfig.Save(); };
        s.Controls.Add(musForceUsb, 1, 2);
        FlowLayoutPanel fs = new FlowLayoutPanel();
        fs.AutoSize = true;
        musSend = new Button { Text = "Enviar para o relógio", AutoSize = true };
        musSend.Click += MusicasSend;
        musCancel = new Button { Text = "Cancelar", AutoSize = true, Enabled = false };
        musCancel.Click += delegate { if (musCts != null) musCts.Cancel(); };
        fs.Controls.Add(musSend);
        fs.Controls.Add(musCancel);
        s.Controls.Add(fs, 1, 3);
        musBar = new ProgressBar { Dock = DockStyle.Fill, Minimum = 0, Maximum = 1000, Height = 18 };
        s.Controls.Add(musBar, 1, 4);
        musStatus = new Label { Text = "", AutoSize = true, MaximumSize = new Size(560, 0) };
        s.Controls.Add(musStatus, 1, 5);
        gSend.Controls.Add(s);
        t.Controls.Add(gSend, 0, 1);

        musLog = NewLogBox();
        musLog.Dock = DockStyle.Fill;
        t.Controls.Add(musLog, 0, 2);
    }

    // Stable names so UI automation (tests) can find the controls.
    void NameControls()
    {
        relLog.Name = "relLog"; relStatus.Name = "relStatus"; relRefresh.Name = "relRefresh";
        musFile.Name = "musFile"; musSlot.Name = "musSlot"; musSend.Name = "musSend"; musStatus.Name = "musStatus";
        ferLog.Name = "ferLog"; musLog.Name = "musLog"; musList.Name = "musList"; musForceUsb.Name = "musForceUsb";
    }

    void MusLog(string s) { AppendLog(musLog, s); }

    void WatchChanged() { MusicasRefresh(); }

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

    void FillSlots()
    {
        musList.Items.Clear();
        if (musSlots == null) { musFree.Text = ""; return; }
        foreach (SlotInfo si in musSlots)
        {
            ListViewItem it = new ListViewItem(si.N.ToString());
            it.SubItems.Add(si.Empty ? "(vazio)" : si.Name);
            it.SubItems.Add(si.Empty ? "" : AppLogic.FormatMB(si.Size));
            if (si.Empty) it.ForeColor = Color.Gray;
            musList.Items.Add(it);
        }
        musFree.Text = "Espaço livre: " + AppLogic.FormatMB(musFreeBytes);
    }

    // Reads the slots over USB when the watch is plugged in, else over Wi-Fi.
    async Task<bool> LoadSlotsAsync()
    {
        SlotInfo[] slots = null;
        long free = 0;
        string source = "";
        string port = watchPort;
        await UsbRun(delegate
        {
            if (port != null)
            {
                string err;
                using (SerialLink l = SerialLink.TryOpen(port, 115200, out err))
                {
                    if (l != null) { slots = l.ListSlots(out free); if (slots != null) source = "lido por USB"; }
                }
            }
            if (slots == null)
            {
                WatchApi api = FindApi();
                if (api != null)
                {
                    string e;
                    slots = api.GetSlots(out free, out e);
                    if (slots != null) source = "lido por Wi-Fi";
                }
            }
        });
        musSlots = slots;
        musFreeBytes = free;
        musSource.Text = source;
        FillSlots();
        return slots != null;
    }

    async void MusicasRefresh()
    {
        if (musBusy) return;
        bool ok = await LoadSlotsAsync();
        if (!ok) musSource.Text = "relógio não encontrado (USB ou Wi-Fi)";
    }

    void MusicasPick(object sender, EventArgs e)
    {
        OpenFileDialog d = new OpenFileDialog();
        d.Title = "Escolhe um ficheiro de áudio";
        d.Filter = "Áudio (*.mp3;*.m4a;*.wav;*.flac;*.ogg;*.opus;*.mp4;*.webm)|*.mp3;*.m4a;*.wav;*.flac;*.ogg;*.opus;*.mp4;*.webm|Todos os ficheiros|*.*";
        if (d.ShowDialog(this) == DialogResult.OK) musFile.Text = d.FileName;
    }

    async void MusicasDelete(object sender, EventArgs e)
    {
        if (musBusy || musList.SelectedItems.Count == 0) return;
        int n = musList.SelectedItems[0].Index + 1;
        if (musSlots == null || musSlots[n - 1].Empty) return;
        if (MessageBox.Show(this, "Apagar a música do slot " + n + " (" + musSlots[n - 1].Name + ")?", "Apagar",
                            MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
        SetBusy(true);
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
            if (api == null) { result = "O relógio não está acessível por USB nem por Wi-Fi."; return; }
            string e2;
            if (!api.Delete(n, out e2)) result = e2;
        });
        if (result != null) MusLog("Não foi possível apagar: " + result);
        else MusLog("Slot " + n + " apagado.");
        SetBusy(false);
        await LoadSlotsAsync();
    }

    void SetBusy(bool busy)
    {
        musBusy = busy;
        musSend.Enabled = !busy;
        musDelete.Enabled = !busy;
        musRefresh.Enabled = !busy;
        musPick.Enabled = !busy;
        musCancel.Enabled = busy && musCts != null;
        if (!busy) musBar.Value = 0;
    }

    void SetProgress(long done, long total, string what)
    {
        BeginInvoke((Action)delegate
        {
            int v = total <= 0 ? 0 : (int)Math.Min(1000, done * 1000 / total);
            musBar.Value = v;
            musStatus.Text = what + " " + AppLogic.FormatMB(done) + " de " + AppLogic.FormatMB(total) + " (" + (v / 10) + "%)";
        });
    }

    async void MusicasSend(object sender, EventArgs e)
    {
        await MusicasSendTask();
    }

    // Used by the test mode: --autosend <file> <slot> [usb] runs the same flow as the Enviar button and
    // writes the logs to twatch-autosend.log next to the exe, then closes.
    async Task AutoSend()
    {
        await RelogioRefreshTask();
        musFile.Text = autoArgs[1];
        musSlot.SelectedIndex = int.Parse(autoArgs[2]) - 1;
        musForceUsb.Checked = autoArgs.Length > 3 && autoArgs[3] == "usb";
        await MusicasSendTask();
        File.WriteAllText(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "twatch-autosend.log"),
                          "RELOGIO" + Environment.NewLine + relLog.Text + Environment.NewLine + "MUSICAS" +
                          Environment.NewLine + musLog.Text + Environment.NewLine + "ESTADO " + musStatus.Text);
        Close();
    }

    void SetStatus(string s)
    {
        BeginInvoke((Action)delegate { musStatus.Text = s; });
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
                        string r = cv.FromUrl(url, delegate(string m) { BeginInvoke((Action)delegate { MusLog(m); }); }, SetStatus, ct, out t, out e2);
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
                    MessageBox.Show(this, "Não consigo falar com o relógio. Liga-o por USB (separador Relógio) ou põe-no em Programação na mesma rede Wi-Fi.",
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
                    SetStatus("A recodificar a 64 kbps mono...");
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
                        MessageBox.Show(this, "A música é demasiado grande (mais de " + AppLogic.FormatMB(AppLogic.MaxSane) + ") mesmo a 64 kbps mono.",
                                        "Enviar", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                        return;
                    }
                    if (p2 == EncodePlan.DoesNotFit)
                    {
                        MessageBox.Show(this, "Não cabe, mesmo a 64 kbps mono: faltam " + AppLogic.FormatMB(AppLogic.MissingBytes(size2, freeEff)) +
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
            MessageBox.Show(this, "Não consigo falar com o relógio. Liga-o por USB (separador Relógio) ou põe-no em Programação na mesma rede Wi-Fi.",
                            "Enviar", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        SlotInfo cur = musSlots[slot - 1];
        bool deleteFirst = false;
        if (!AppLogic.Fits(size, musFreeBytes))
        {
            if (!cur.Empty && AppLogic.Fits(size, musFreeBytes + cur.Size))
            {
                if (MessageBox.Show(this, "Para caber, a música do slot " + slot + " (" + cur.Name + ") tem de ser apagada antes. Apagar e enviar?",
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
                MusLog("A pôr o relógio em Programação (Wi-Fi)...");
                musStatus.Text = "A ligar o relógio ao Wi-Fi...";
                await SwitchMode(true);
            }
            musStatus.Text = "A procurar o relógio na rede...";
            api = await Task.Run(delegate { return FindApi(); });
        }
        Transport tr = AppLogic.ChooseTransport(wifiConfigured, api != null, forceUsb);
        if (tr == Transport.Usb && wifiConfigured && !forceUsb)
        {
            if (MessageBox.Show(this, "O relógio não respondeu por Wi-Fi. Enviar por USB?", "Enviar",
                                MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return;
        }
        if (tr == Transport.Usb && watchPort == null)
        {
            MessageBox.Show(this, "Para enviar por USB liga o relógio ao computador (separador Relógio, \"Atualizar\").", "Enviar",
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
                    return api.Upload(path, slot, name, delegate(long b) { SetProgress(b, size, "A enviar por Wi-Fi:"); }, ct, out m);
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
                        return l.PutFile(path, slot, name, f, delegate(long b) { SetProgress(b, size, "A enviar por USB:"); }, ct, out m);
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
