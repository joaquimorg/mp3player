// "Músicas" tab (implemented in Task 8).
using System;
using System.Windows.Forms;

public partial class MainForm
{
    void BuildMusicasTab()
    {
        TabPage page = new TabPage("Músicas");
        page.Controls.Add(new Label { Text = "Em construção.", AutoSize = true, Left = 12, Top = 12 });
        tabs.TabPages.Add(page);
    }

    void MusicasRefresh() { }

    // Called when the watch state changed (the Músicas tab will use it).
    void WatchChanged() { }
}
