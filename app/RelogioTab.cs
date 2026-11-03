// "Relógio" tab: finds the watch on the USB ports, shows its state, sends the Wi-Fi settings and
// switches between Reprodução and Programação.
using System;
using System.Drawing;
using System.Threading.Tasks;
using System.Windows.Forms;

public partial class MainForm
{
    Label relStatus;
    Button relRefresh, relMode, relSendWifi;
    TextBox relSsid, relPass, relLog;
    CheckBox relShow;

    // Last known state (set by RelogioRefresh); other tabs read it.
    WatchStatus watch;
    string watchPort;

    void BuildRelogioTab()
    {
        TabPage page = new TabPage("Relógio");
        page.Padding = new Padding(12);
        tabs.TabPages.Add(page);

        TableLayoutPanel t = new TableLayoutPanel();
        t.Dock = DockStyle.Fill;
        t.ColumnCount = 1;
        t.RowCount = 4;
        t.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        t.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        t.RowStyles.Add(new RowStyle(SizeType.AutoSize));
        t.RowStyles.Add(new RowStyle(SizeType.Percent, 100F));
        page.Controls.Add(t);

        // State.
        GroupBox gState = new GroupBox();
        gState.Text = "Estado do relógio (ligado por USB)";
        gState.Dock = DockStyle.Top;
        gState.AutoSize = true;
        gState.Padding = new Padding(10);
        relStatus = new Label();
        relStatus.AutoSize = false;
        relStatus.Height = 54;
        relStatus.Dock = DockStyle.Top;
        relStatus.Text = "A procurar o relógio...";
        FlowLayoutPanel fp = new FlowLayoutPanel();
        fp.Dock = DockStyle.Top;
        fp.AutoSize = true;
        relRefresh = new Button();
        relRefresh.Text = "Atualizar";
        relRefresh.AutoSize = true;
        relRefresh.Click += delegate { RelogioRefresh(); };
        relMode = new Button();
        relMode.Text = "Ir para Programação";
        relMode.AutoSize = true;
        relMode.Enabled = false;
        relMode.Click += RelogioToggleMode;
        fp.Controls.Add(relRefresh);
        fp.Controls.Add(relMode);
        gState.Controls.Add(fp);
        gState.Controls.Add(relStatus);
        t.Controls.Add(gState, 0, 0);

        // Wi-Fi.
        GroupBox gWifi = new GroupBox();
        gWifi.Text = "Wi-Fi do relógio (rede de 2,4 GHz)";
        gWifi.Dock = DockStyle.Top;
        gWifi.AutoSize = true;
        gWifi.Padding = new Padding(10);
        TableLayoutPanel w = new TableLayoutPanel();
        w.Dock = DockStyle.Top;
        w.AutoSize = true;
        w.ColumnCount = 2;
        w.ColumnStyles.Add(new ColumnStyle(SizeType.AutoSize));
        w.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100F));
        w.Controls.Add(new Label { Text = "Nome da rede:", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 0);
        relSsid = new TextBox { Dock = DockStyle.Fill };
        w.Controls.Add(relSsid, 1, 0);
        w.Controls.Add(new Label { Text = "Palavra-passe:", AutoSize = true, Anchor = AnchorStyles.Left }, 0, 1);
        relPass = new TextBox { Dock = DockStyle.Fill, UseSystemPasswordChar = true };
        w.Controls.Add(relPass, 1, 1);
        relShow = new CheckBox { Text = "Mostrar palavra-passe", AutoSize = true };
        relShow.CheckedChanged += delegate { relPass.UseSystemPasswordChar = !relShow.Checked; };
        w.Controls.Add(relShow, 1, 2);
        relSendWifi = new Button { Text = "Enviar Wi-Fi para o relógio", AutoSize = true, Enabled = false };
        relSendWifi.Click += RelogioSendWifi;
        w.Controls.Add(relSendWifi, 1, 3);
        gWifi.Controls.Add(w);
        t.Controls.Add(gWifi, 0, 1);

        Label hint = new Label();
        hint.AutoSize = true;
        hint.MaximumSize = new Size(700, 0);
        hint.Text = "O relógio guarda a rede e usa-a no modo Programação. A palavra-passe só viaja pelo cabo USB e nunca aparece neste registo.";
        hint.ForeColor = Color.DimGray;
        t.Controls.Add(hint, 0, 2);

        relLog = NewLogBox();
        relLog.Dock = DockStyle.Fill;
        t.Controls.Add(relLog, 0, 3);
    }

    void RelLog(string s) { AppendLog(relLog, s); }

    string DescribeWatch(WatchStatus st, string port)
    {
        string wifi = st.WifiConfigured ? "Wi-Fi: " + st.Wifi : "Wi-Fi: não configurado";
        string ip = st.HasIp ? "  ·  endereço " + st.Ip : "";
        return "Relógio em " + port + "  ·  " + ModeName(st.Mode) + "  ·  " + wifi + ip +
               "  ·  livre " + AppLogic.FormatMB(st.FreeBytes) + "  ·  versão " + st.Version;
    }

    async void RelogioRefresh()
    {
        await RelogioRefreshTask();
    }

    async Task RelogioRefreshTask()
    {
        relRefresh.Enabled = false;
        relMode.Enabled = false;
        relSendWifi.Enabled = false;
        relStatus.Text = "A procurar o relógio nas portas USB...";
        RelLog("A procurar o relógio...");
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
            relStatus.Text = "Relógio não encontrado. Liga-o por USB (cabo de dados), fecha outros programas que usem a porta e carrega em Atualizar.";
            relStatus.ForeColor = Color.Firebrick;
        }
        else
        {
            AppConfig.LastPort = port;
            AppConfig.Save();
            relStatus.Text = DescribeWatch(st, port);
            relStatus.ForeColor = Color.Black;
            relMode.Text = st.InProgramMode ? "Ir para Reprodução" : "Ir para Programação";
            relMode.Enabled = true;
            relSendWifi.Enabled = true;
            if (st.HasIp) AppConfig.LastIp = st.Ip;
        }
        relRefresh.Enabled = true;
        WatchChanged();
    }

    async void RelogioToggleMode(object sender, EventArgs e)
    {
        if (watch == null) return;
        bool toProgram = !watch.InProgramMode;
        string port = watchPort;
        relMode.Enabled = false;
        relRefresh.Enabled = false;
        RelLog(toProgram ? "A pedir o modo Programação (pode demorar até 45 s)..." : "A pedir o modo Reprodução...");
        string reply = null;
        WatchStatus st = null;
        await UsbRun(delegate
        {
            string err;
            using (SerialLink l = SerialLink.TryOpen(port, 115200, out err))
            {
                if (l == null) { reply = err; return; }
                reply = l.SetMode(toProgram);
                if (reply == "OK")
                    st = l.WaitStatus(delegate(WatchStatus s)
                    {
                        return toProgram ? (s.InProgramMode && (s.HasIp || !s.WifiConfigured)) : !s.InProgramMode;
                    }, 60000);
            }
        });
        if (reply != "OK") RelLog("Não foi possível mudar de modo: " + SerialLink.DescribeError(reply));
        else if (st == null) RelLog("O relógio não respondeu depois do pedido.");
        else RelLog("Modo agora: " + ModeName(st.Mode) + (st.HasIp ? ", endereço " + st.Ip : ""));
        relRefresh.Enabled = true;
        RelogioRefresh();
    }

    async void RelogioSendWifi(object sender, EventArgs e)
    {
        string ssid = relSsid.Text;
        string pass = relPass.Text;
        string erro;
        if (!AppLogic.ValidWifi(ssid, pass, out erro))
        {
            MessageBox.Show(this, erro, "Wi-Fi", MessageBoxButtons.OK, MessageBoxIcon.Warning);
            return;
        }
        string aviso = watch != null && watch.WifiConfigured
            ? "Isto substitui a rede guardada no relógio (" + watch.Wifi + ") por \"" + ssid + "\". Continuar?"
            : "Guardar a rede \"" + ssid + "\" no relógio?";
        if (MessageBox.Show(this, aviso, "Wi-Fi", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
        string port = watchPort;
        relSendWifi.Enabled = false;
        RelLog("A enviar o Wi-Fi \"" + ssid + "\" ***...");
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
            RelLog("Wi-Fi guardado no relógio. Usa \"Ir para Programação\" para ele se ligar à rede.");
            relPass.Text = "";
        }
        else RelLog("O relógio não guardou o Wi-Fi: " + SerialLink.DescribeError(reply));
        RelogioRefresh();
    }
}
