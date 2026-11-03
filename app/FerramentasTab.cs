// "Ferramentas" tab (implemented in Task 9).
using System;
using System.Windows.Forms;

public partial class MainForm
{
    void BuildFerramentasTab()
    {
        TabPage page = new TabPage("Ferramentas");
        page.Controls.Add(new Label { Text = "Em construção.", AutoSize = true, Left = 12, Top = 12 });
        tabs.TabPages.Add(page);
    }

    void FerramentasRefresh() { }
}
