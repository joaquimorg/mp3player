// T-Watch MP3: Windows app entry point.
using System;
using System.Windows.Forms;

public static class Program
{
    static void Report(Exception e)
    {
        string msg = e == null ? "erro desconhecido" : e.Message;
        try
        {
            System.IO.File.AppendAllText(System.IO.Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "twatch-erro.log"),
                                         DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss") + "  " + (e == null ? msg : e.ToString()) + Environment.NewLine);
        }
        catch (Exception) { }
        try
        {
            MessageBox.Show("Ocorreu um problema inesperado, mas a app continua a funcionar.\n\n" + msg +
                            "\n\nSe o relógio não responder, verifica o cabo USB e carrega em «Procurar outra vez» nas Definições.",
                            "T-Watch MP3", MessageBoxButtons.OK, MessageBoxIcon.Warning);
        }
        catch (Exception) { }
    }


    [STAThread]
    public static void Main(string[] args)
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        // Last line of defence: anything unexpected becomes a friendly message and a line in
        // twatch-erro.log, and the app keeps running (instead of the technical Windows crash dialog).
        Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
        Application.ThreadException += delegate(object s, System.Threading.ThreadExceptionEventArgs e) { Report(e.Exception); };
        AppDomain.CurrentDomain.UnhandledException += delegate(object s, UnhandledExceptionEventArgs e) { Report(e.ExceptionObject as Exception); };
        Application.Run(new MainForm(args));
    }
}
