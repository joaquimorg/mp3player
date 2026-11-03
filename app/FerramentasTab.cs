// "Ferramentas" tab: yt-dlp and ffmpeg (needed to turn links and other audio formats into MP3).
using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

public partial class MainForm
{
    Label ferYt, ferFf;
    Button ferGet, ferUpdate;
    TextBox ferLog;

    void BuildFerramentasTab()
    {
        TabPage page = new TabPage("Ferramentas");
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

        GroupBox g = new GroupBox();
        g.Text = "Programas usados para converter links e ficheiros em MP3";
        g.Dock = DockStyle.Top;
        g.AutoSize = true;
        g.Padding = new Padding(10);
        ferYt = new Label { AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(0, 4, 0, 4) };
        ferFf = new Label { AutoSize = true, Dock = DockStyle.Top, Padding = new Padding(0, 4, 0, 4) };
        FlowLayoutPanel fb = new FlowLayoutPanel();
        fb.Dock = DockStyle.Top;
        fb.AutoSize = true;
        ferGet = new Button { Text = "Descarregar o que falta", AutoSize = true };
        ferGet.Click += delegate { FerramentasDownload(true, true, false); };
        ferUpdate = new Button { Text = "Atualizar o yt-dlp", AutoSize = true };
        ferUpdate.Click += delegate { FerramentasDownload(false, false, true); };
        Button re = new Button { Text = "Verificar", AutoSize = true };
        re.Click += delegate { FerramentasRefresh(); };
        fb.Controls.Add(ferGet);
        fb.Controls.Add(ferUpdate);
        fb.Controls.Add(re);
        g.Controls.Add(fb);
        g.Controls.Add(ferFf);
        g.Controls.Add(ferYt);
        t.Controls.Add(g, 0, 0);

        Label hint = new Label();
        hint.AutoSize = true;
        hint.MaximumSize = new Size(700, 0);
        hint.ForeColor = Color.DimGray;
        hint.Text = "Os programas são descarregados das páginas oficiais (GitHub do yt-dlp e gyan.dev para o ffmpeg) para a pasta da app. " +
                    "O YouTube muda com frequência: se um link deixar de funcionar, usa \"Atualizar o yt-dlp\".";
        t.Controls.Add(hint, 0, 1);

        ferLog = NewLogBox();
        ferLog.Dock = DockStyle.Fill;
        t.Controls.Add(ferLog, 0, 2);
    }

    void FerLog(string s) { AppendLog(ferLog, s); }

    async void FerramentasRefresh()
    {
        ferYt.Text = "yt-dlp: a verificar...";
        ferFf.Text = "ffmpeg: a verificar...";
        string y = null, f = null;
        await Task.Run(delegate { y = Tools.Version(Tools.YtDlpExe); f = Tools.Version(Tools.FfmpegExe); });
        ferYt.Text = "yt-dlp: " + (y != null ? "versão " + y : (Tools.Have(Tools.YtDlpExe) ? "não responde" : "em falta"));
        ferFf.Text = "ffmpeg: " + (f != null ? f : (Tools.Have(Tools.FfmpegExe) ? "não responde" : "em falta"));
        ferYt.ForeColor = y != null ? Color.Black : Color.Firebrick;
        ferFf.ForeColor = f != null ? Color.Black : Color.Firebrick;
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
            if (!getYt && !getFf) FerLog("Não há nada a descarregar.");
        }
        finally { ferGet.Enabled = true; ferUpdate.Enabled = true; }
        return ok;
    }

    // Asks to download what is missing; true when the needed tools are available.
    async Task<bool> EnsureTools(bool needYt, bool needFf)
    {
        bool missYt = needYt && !Tools.Have(Tools.YtDlpExe);
        bool missFf = needFf && !Tools.Have(Tools.FfmpegExe);
        if (!missYt && !missFf) return true;
        string what = (missYt ? "yt-dlp" : "") + (missYt && missFf ? " e " : "") + (missFf ? "ffmpeg" : "");
        if (MessageBox.Show(this, "Para esta música faltam os programas: " + what + ".\nDescarregar agora das páginas oficiais (até cerca de 100 MB)?",
                            "Ferramentas", MessageBoxButtons.YesNo, MessageBoxIcon.Question) != DialogResult.Yes) return false;
        tabs.SelectedIndex = 2;
        bool ok = await DownloadTools(missYt, missFf);
        FerramentasRefresh();
        return ok && (!needYt || Tools.Have(Tools.YtDlpExe)) && (!needFf || Tools.Have(Tools.FfmpegExe));
    }
}
