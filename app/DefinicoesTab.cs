// "Definições" section: the watch connection state, the watch's Wi-Fi, and the advanced options
// (force USB, the yt-dlp/ffmpeg programs, technical details). C# 5.
using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

public partial class MainForm
{
    // Last known state of the watch over USB (set by RefreshWatchTask); other sections read it.
    WatchStatus watch;
    string watchPort;

    Label defTitle, defDetail, defHelp, defWifiSaved, defWifiMsg, ferYt, ferFf;
    Button defRetry, defSave, advToggle, ferGet, ferUpdate;
    TextBox defSsid, defPass;
    CheckBox defShow, musForceUsb;
    Panel advPanel;

    void BuildDefinicoesTab()
    {
        TableLayoutPanel col = NewColumn(pageSettings);

        // ---- the watch ----
        Card c1 = new Card();
        TableLayoutPanel t1 = Stack();
        AddRow(t1, Theme.Label("O relógio", Theme.Big, Theme.Text), 8);
        defTitle = Theme.Label("A procurar o relógio...", Theme.Semi, Theme.Muted);
        AddRow(t1, defTitle, 2);
        defDetail = Theme.Label("", Theme.Normal, Theme.Muted);
        AddRow(t1, defDetail, 2);
        defHelp = Theme.Label("", Theme.Small, Theme.Muted);
        defHelp.MaximumSize = new Size(700, 0);
        AddRow(t1, defHelp, 10);
        defRetry = Theme.SecondaryButton("Procurar outra vez");
        defRetry.Width = 190;
        defRetry.Click += delegate { RelogioRefresh(); };
        AddRow(t1, defRetry, 0);
        c1.Controls.Add(t1);
        AddCard(col, c1);

        // ---- Wi-Fi ----
        Card c2 = new Card();
        TableLayoutPanel t2 = Stack();
        AddRow(t2, Theme.Label("Wi-Fi do relógio", Theme.Big, Theme.Text), 4);
        Label why = Theme.Label("Com o Wi-Fi guardado, as músicas vão para o relógio sem cabo (o relógio e o computador têm de estar " +
                                "na mesma rede, de 2,4 GHz). Sem Wi-Fi, a app usa o cabo USB.", Theme.Small, Theme.Muted);
        why.MaximumSize = new Size(700, 0);
        AddRow(t2, why, 10);
        defWifiSaved = Theme.Label("", Theme.Normal, Theme.Text);
        AddRow(t2, defWifiSaved, 10);

        defSsid = WifiBox("Nome da rede Wi-Fi");
        AddRow(t2, defSsid, 8);
        defPass = WifiBox("Palavra-passe");
        defPass.UseSystemPasswordChar = true;
        AddRow(t2, defPass, 4);
        defShow = new CheckBox { Text = "Mostrar a palavra-passe", AutoSize = true, BackColor = Theme.Card, ForeColor = Theme.Muted, Font = Theme.Small };
        defShow.CheckedChanged += delegate { defPass.UseSystemPasswordChar = !defShow.Checked; };
        AddRow(t2, defShow, 10);
        defSave = Theme.PrimaryButton("Guardar no relógio");
        defSave.Height = 40;
        defSave.Width = 200;
        defSave.Enabled = false;
        defSave.Click += SaveWifi;
        AddRow(t2, defSave, 8);
        defWifiMsg = Theme.Label("", Theme.Normal, Theme.Muted);
        defWifiMsg.MaximumSize = new Size(700, 0);
        AddRow(t2, defWifiMsg, 0);
        c2.Controls.Add(t2);
        AddCard(col, c2);

        // ---- advanced ----
        Card c3 = new Card();
        TableLayoutPanel t3 = Stack();
        advToggle = Theme.LinkButton("Mostrar opções avançadas", Theme.Accent);
        advToggle.Width = 260;
        advToggle.Click += delegate { advPanel.Visible = !advPanel.Visible; advToggle.Text = advPanel.Visible ? "Esconder opções avançadas" : "Mostrar opções avançadas"; };
        AddRow(t3, advToggle, 0);

        advPanel = new Panel();
        advPanel.AutoSize = true;
        advPanel.Dock = DockStyle.Top;
        advPanel.BackColor = Theme.Card;
        advPanel.Visible = false;
        TableLayoutPanel ta = Stack();
        musForceUsb = new CheckBox { Text = "Enviar sempre pelo cabo USB (não usar o Wi-Fi)", AutoSize = true, BackColor = Theme.Card, Checked = AppConfig.ForceUsb };
        musForceUsb.CheckedChanged += delegate { AppConfig.ForceUsb = musForceUsb.Checked; AppConfig.Save(); };
        AddRow(ta, musForceUsb, 14);
        AddRow(ta, Theme.Label("Programas para links e outros formatos", Theme.Semi, Theme.Text), 4);
        Label ty = Theme.Label("Servem para descarregar áudio de links (YouTube) e converter ficheiros para MP3. A app instala-os quando for preciso.",
                               Theme.Small, Theme.Muted);
        ty.MaximumSize = new Size(700, 0);
        AddRow(ta, ty, 6);
        ferYt = Theme.Label("", Theme.Small, Theme.Text);
        ferFf = Theme.Label("", Theme.Small, Theme.Text);
        AddRow(ta, ferYt, 2);
        AddRow(ta, ferFf, 8);
        FlowLayoutPanel fb = new FlowLayoutPanel { AutoSize = true, BackColor = Theme.Card, WrapContents = true };
        ferGet = Theme.SecondaryButton("Instalar o que falta");
        ferGet.Width = 180;
        ferGet.Click += delegate { FerramentasDownload(true, true, false); };
        ferUpdate = Theme.SecondaryButton("Atualizar o yt-dlp");
        ferUpdate.Width = 170;
        ferUpdate.Click += delegate { FerramentasDownload(false, false, true); };
        fb.Controls.Add(ferGet);
        fb.Controls.Add(ferUpdate);
        AddRow(ta, fb, 14);
        AddRow(ta, Theme.Label("Detalhes técnicos", Theme.Semi, Theme.Text), 4);
        logBox.Height = 170;
        logBox.Dock = DockStyle.Top;
        AddRow(ta, logBox, 0);
        advPanel.Controls.Add(ta);
        AddRow(t3, advPanel, 0);
        c3.Controls.Add(t3);
        AddCard(col, c3);
    }

    static TextBox WifiBox(string hint)
    {
        TextBox t = new TextBox();
        t.Font = new Font("Segoe UI", 11F);
        t.BorderStyle = BorderStyle.FixedSingle;
        t.Dock = DockStyle.Top;
        t.Width = 380;
        Cue(t, hint);
        return t;
    }

    void RelLog(string s) { MusLog(s); }

    // ---- the watch over USB ------------------------------------------------------------
    async void RelogioRefresh()
    {
        await RefreshWatchTask();
    }

    async Task RefreshWatchTask()
    {
        defRetry.Enabled = false;
        SetConn(Conn.Searching);
        defTitle.Text = "A procurar o relógio...";
        defTitle.ForeColor = Theme.Muted;
        defDetail.Text = "";
        defHelp.Text = "";
        RelLog("A procurar o relógio nas portas USB...");
        string preferred = AppConfig.LastPort;
        string port = null;
        WatchStatus st = null;
        await UsbRun(delegate
        {
            st = SerialLink.FindWatch(preferred, delegate(string m) { BeginInvoke((Action)delegate { RelLog(m); }); }, out port);
        });
        watch = st;
        watchPort = port;
        if (st == null)
        {
            SetConn(Conn.None);
            defTitle.Text = "Não encontro o relógio";
            defTitle.ForeColor = Theme.Err;
            defDetail.Text = "";
            defHelp.Text = "Liga o relógio ao computador com o cabo USB (tem de ser um cabo de dados) e carrega em «Procurar outra vez». " +
                           "Se outro programa estiver a usar o relógio, fecha-o primeiro.";
            defWifiSaved.Text = "Liga o relógio por cabo para ver e mudar a rede Wi-Fi.";
            defSave.Enabled = false;
        }
        else
        {
            AppConfig.LastPort = port;
            AppConfig.Save();
            SetConn(Conn.Usb);
            defTitle.Text = "O relógio está ligado e pronto";
            defTitle.ForeColor = Theme.Ok;
            defDetail.Text = "Espaço livre: " + AppLogic.FormatMB(st.FreeBytes);
            defHelp.Text = "";
            defWifiSaved.Text = st.WifiConfigured ? "Rede guardada no relógio: " + st.Wifi : "Ainda não há nenhuma rede Wi-Fi guardada no relógio.";
            defSave.Enabled = true;
            if (st.HasIp) AppConfig.LastIp = st.Ip;
        }
        defRetry.Enabled = true;
        WatchChanged();
    }

    async void SaveWifi(object sender, EventArgs e)
    {
        string ssid = defSsid.Text;
        string pass = defPass.Text;
        string erro;
        if (!AppLogic.ValidWifi(ssid, pass, out erro))
        {
            MessageBox.Show(this, erro, "Wi-Fi", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        string aviso = watch != null && watch.WifiConfigured
            ? "Isto vai trocar a rede guardada no relógio («" + watch.Wifi + "») por «" + ssid + "». Continuar?"
            : "Guardar a rede «" + ssid + "» no relógio?";
        if (MessageBox.Show(this, aviso, "Wi-Fi", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
        string port = watchPort;
        defSave.Enabled = false;
        defWifiMsg.ForeColor = Theme.Muted;
        defWifiMsg.Text = "A guardar...";
        RelLog("A enviar o Wi-Fi «" + ssid + "» ***...");
        string reply = null;
        await UsbRun(delegate
        {
            string err;
            using (SerialLink l = SerialLink.TryOpen(port, 115200, out err))
            {
                reply = l == null ? err : l.SendWifi(ssid, pass);
            }
        });
        if (reply == "OK")
        {
            defWifiMsg.ForeColor = Theme.Ok;
            defWifiMsg.Text = "Rede guardada! A partir de agora, as músicas podem ir para o relógio sem cabo.";
            defPass.Text = "";
        }
        else
        {
            defWifiMsg.ForeColor = Theme.Err;
            defWifiMsg.Text = "Não consegui guardar o Wi-Fi: " + SerialLink.DescribeError(reply);
        }
        defSave.Enabled = true;
        await RefreshWatchTask();
    }

    // ---- programs (yt-dlp, ffmpeg) -------------------------------------------------------
    void FerLog(string s) { MusLog(s); }

    async void FerramentasRefresh()
    {
        ferYt.Text = "yt-dlp: a verificar...";
        ferFf.Text = "ffmpeg: a verificar...";
        string y = null, f = null;
        await Task.Run(delegate { y = Tools.Version(Tools.YtDlpExe); f = Tools.Version(Tools.FfmpegExe); });
        ferYt.Text = "yt-dlp (links do YouTube): " + (y != null ? "pronto (versão " + y + ")" : (Tools.Have(Tools.YtDlpExe) ? "não responde" : "ainda não instalado"));
        ferFf.Text = "ffmpeg (conversão de áudio): " + (f != null ? "pronto" : (Tools.Have(Tools.FfmpegExe) ? "não responde" : "ainda não instalado"));
        ferYt.ForeColor = y != null ? Theme.Text : Theme.Muted;
        ferFf.ForeColor = f != null ? Theme.Text : Theme.Muted;
    }

    async void FerramentasDownload(bool yt, bool ff, bool updateYt)
    {
        await DownloadTools(updateYt || (yt && !Tools.Have(Tools.YtDlpExe)), ff && !Tools.Have(Tools.FfmpegExe));
        FerramentasRefresh();
    }

    // Downloads what is asked for; returns true when everything asked is now present.
    async Task<bool> DownloadTools(bool getYt, bool getFf)
    {
        ferGet.Enabled = false;
        ferUpdate.Enabled = false;
        bool ok = true;
        Action<string> log = delegate(string m) { BeginInvoke((Action)delegate { FerLog(m); }); };
        try
        {
            if (getYt)
            {
                string e = await Task.Run(delegate { return Tools.DownloadYtDlp(log); });
                if (e != null) { FerLog(e); ok = false; } else FerLog("yt-dlp pronto.");
            }
            if (getFf)
            {
                string e = await Task.Run(delegate { return Tools.DownloadFfmpeg(log); });
                if (e != null) { FerLog(e); ok = false; } else FerLog("ffmpeg pronto.");
            }
            if (!getYt && !getFf) FerLog("Não há nada a instalar.");
        }
        finally { ferGet.Enabled = true; ferUpdate.Enabled = true; }
        return ok;
    }

    // Asks to install what is missing; true when the needed programs are available.
    async Task<bool> EnsureTools(bool needYt, bool needFf)
    {
        bool missYt = needYt && !Tools.Have(Tools.YtDlpExe);
        bool missFf = needFf && !Tools.Have(Tools.FfmpegExe);
        if (!missYt && !missFf) return true;
        if (MessageBox.Show(this, "Para esta música a app precisa de instalar uns programas auxiliares (só uma vez, cerca de 100 MB, " +
                            "descarregados dos sites oficiais). Instalar agora?", "Instalar programas",
                            MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return false;
        musStatus.Text = "A instalar os programas auxiliares... (pode demorar um pouco)";
        bool ok = await DownloadTools(missYt, missFf);
        FerramentasRefresh();
        musStatus.Text = "";
        return ok && (!needYt || Tools.Have(Tools.YtDlpExe)) && (!needFf || Tools.Have(Tools.FfmpegExe));
    }
}
