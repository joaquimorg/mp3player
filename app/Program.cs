// T-Watch MP3: Windows app entry point (placeholder window; the tabs arrive in later tasks).
using System;
using System.Windows.Forms;

public static class Program
{
    [STAThread]
    public static void Main()
    {
        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);
        Form f = new Form();
        f.Text = "T-Watch MP3";
        f.Width = 640;
        f.Height = 480;
        Application.Run(f);
    }
}
