// Look and feel: colours, fonts, flat buttons, rounded cards and the slot row control. C# 5.
using System;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Windows.Forms;

public static class Theme
{
    public static readonly Color Bg = ColorTranslator.FromHtml("#F3F5F9");
    public static readonly Color Card = Color.White;
    public static readonly Color Accent = ColorTranslator.FromHtml("#2563EB");
    public static readonly Color AccentHover = ColorTranslator.FromHtml("#1D4ED8");
    public static readonly Color AccentSoft = ColorTranslator.FromHtml("#EAF1FF");
    public static readonly Color Text = ColorTranslator.FromHtml("#1F2937");
    public static readonly Color Muted = ColorTranslator.FromHtml("#6B7280");
    public static readonly Color Line = ColorTranslator.FromHtml("#E5E7EB");
    public static readonly Color Ok = ColorTranslator.FromHtml("#16A34A");
    public static readonly Color Warn = ColorTranslator.FromHtml("#D97706");
    public static readonly Color Err = ColorTranslator.FromHtml("#DC2626");

    public static readonly Font Normal = new Font("Segoe UI", 10.5F);
    public static readonly Font Small = new Font("Segoe UI", 9.5F);
    public static readonly Font Semi = new Font("Segoe UI Semibold", 10.5F);
    public static readonly Font Big = new Font("Segoe UI Semibold", 13F);
    public static readonly Font Title = new Font("Segoe UI Semibold", 17F);

    public static GraphicsPath Round(Rectangle r, int radius)
    {
        int d = radius * 2;
        GraphicsPath p = new GraphicsPath();
        p.AddArc(r.X, r.Y, d, d, 180, 90);
        p.AddArc(r.Right - d, r.Y, d, d, 270, 90);
        p.AddArc(r.Right - d, r.Bottom - d, d, d, 0, 90);
        p.AddArc(r.X, r.Bottom - d, d, d, 90, 90);
        p.CloseFigure();
        return p;
    }

    public static Button PrimaryButton(string text)
    {
        Button b = Flat(text, Accent, Color.White);
        b.Font = Semi;
        b.Height = 44;
        b.MouseEnter += delegate { if (b.Enabled) b.BackColor = AccentHover; };
        b.MouseLeave += delegate { b.BackColor = Accent; };
        b.EnabledChanged += delegate { b.BackColor = b.Enabled ? Accent : ColorTranslator.FromHtml("#9CB7F5"); };
        return b;
    }

    public static Button SecondaryButton(string text)
    {
        Button b = Flat(text, Color.White, Text);
        b.FlatAppearance.BorderSize = 1;
        b.FlatAppearance.BorderColor = Line;
        b.Height = 38;
        b.MouseEnter += delegate { if (b.Enabled) b.BackColor = ColorTranslator.FromHtml("#F3F4F6"); };
        b.MouseLeave += delegate { b.BackColor = Color.White; };
        return b;
    }

    public static Button LinkButton(string text, Color color)
    {
        Button b = Flat(text, Color.Transparent, color);
        b.FlatAppearance.MouseOverBackColor = ColorTranslator.FromHtml("#F3F4F6");
        b.Height = 32;
        b.TextAlign = ContentAlignment.MiddleLeft;
        b.Padding = new Padding(0);
        return b;
    }

    static Button Flat(string text, Color back, Color fore)
    {
        Button b = new Button();
        b.Text = text;
        b.FlatStyle = FlatStyle.Flat;
        b.FlatAppearance.BorderSize = 0;
        b.BackColor = back;
        b.ForeColor = fore;
        b.Font = Normal;
        b.Cursor = Cursors.Hand;
        b.UseVisualStyleBackColor = false;
        b.AutoSize = false;
        b.Padding = new Padding(14, 0, 14, 0);
        b.Width = Math.Max(90, TextRenderer.MeasureText(text, Semi).Width + 36);
        return b;
    }

    public static Label Label(string text, Font font, Color color)
    {
        Label l = new Label();
        l.Text = text;
        l.Font = font;
        l.ForeColor = color;
        l.AutoSize = true;
        l.BackColor = Color.Transparent;
        return l;
    }
}


// A navigation button ("Músicas" / "Definições") whose colours follow its selected state, also after the
// mouse enters and leaves (a plain button resets its back colour on MouseLeave and hid the white text).
public class Pill : Button
{
    bool selected, hover;

    public Pill(string text)
    {
        Text = text;
        FlatStyle = FlatStyle.Flat;
        Font = Theme.Semi;
        Cursor = Cursors.Hand;
        Width = 130;
        Height = 40;
        UseVisualStyleBackColor = false;
        FlatAppearance.BorderColor = Theme.Line;
        Apply();
    }

    public bool Selected
    {
        get { return selected; }
        set { selected = value; Apply(); }
    }

    protected override void OnMouseEnter(EventArgs e) { hover = true; Apply(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { hover = false; Apply(); base.OnMouseLeave(e); }

    void Apply()
    {
        Color back = selected ? (hover ? Theme.AccentHover : Theme.Accent)
                              : (hover ? ColorTranslator.FromHtml("#F3F4F6") : Color.White);
        BackColor = back;
        ForeColor = selected ? Color.White : Theme.Text;
        FlatAppearance.BorderSize = selected ? 0 : 1;
        FlatAppearance.MouseOverBackColor = back;
        FlatAppearance.MouseDownBackColor = back;
    }
}

// A white rounded card on the grey page background. Children keep a white back colour.
public class Card : Panel
{
    public Card()
    {
        DoubleBuffered = true;
        ResizeRedraw = true;
        BackColor = Theme.Bg;
        Padding = new Padding(20, 16, 20, 18);
        AutoSize = true;
        AutoSizeMode = AutoSizeMode.GrowAndShrink;
        Margin = new Padding(0, 0, 0, 16);
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        using (GraphicsPath p = Theme.Round(new Rectangle(0, 0, Width - 1, Height - 1), 12))
        using (Brush b = new SolidBrush(Theme.Card))
        using (Pen pen = new Pen(Theme.Line))
        {
            e.Graphics.FillPath(b, p);
            e.Graphics.DrawPath(pen, p);
        }
    }
}

// One of the 4 slots: number badge, song name and size, with a "Apagar" button. Click selects it as the
// destination; files and links can be dropped on it.
public class SlotRow : Panel
{
    public int N;
    public SlotInfo Info;       // null until the list is read
    public bool Selected;
    public Button DeleteButton;
    public event Action<int> Picked;
    public event Action<int> DeleteClicked;
    public event Action<int, IDataObject> Dropped;
    bool hover;

    public SlotRow(int n)
    {
        N = n;
        DoubleBuffered = true;
        Height = 58;
        Margin = new Padding(0, 0, 0, 8);
        BackColor = Theme.Card;
        Cursor = Cursors.Hand;
        AllowDrop = true;
        DeleteButton = Theme.LinkButton("Apagar", Theme.Err);
        DeleteButton.Width = 96;
        DeleteButton.Padding = new Padding(0);
        DeleteButton.Height = 34;
        DeleteButton.Anchor = AnchorStyles.Right | AnchorStyles.Top;
        DeleteButton.Click += delegate { if (DeleteClicked != null) DeleteClicked(N); };
        Controls.Add(DeleteButton);
        Resize += delegate { PlaceButton(); };
        MouseEnter += delegate { hover = true; Invalidate(); };
        MouseLeave += delegate { hover = false; Invalidate(); };
        Click += delegate { if (Picked != null) Picked(N); };
        DragEnter += delegate(object s, DragEventArgs e)
        {
            e.Effect = (e.Data.GetDataPresent(DataFormats.FileDrop) || e.Data.GetDataPresent(DataFormats.Text))
                ? DragDropEffects.Copy : DragDropEffects.None;
        };
        DragDrop += delegate(object s, DragEventArgs e) { if (Dropped != null) Dropped(N, e.Data); };
        PlaceButton();
    }

    void PlaceButton()
    {
        DeleteButton.Location = new Point(Width - DeleteButton.Width - 12, (Height - DeleteButton.Height) / 2);
    }

    public void SetInfo(SlotInfo info)
    {
        Info = info;
        DeleteButton.Visible = info != null && !info.Empty;
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        Graphics g = e.Graphics;
        g.SmoothingMode = SmoothingMode.AntiAlias;
        Rectangle r = new Rectangle(0, 0, Width - 1, Height - 1);
        Color fill = Selected ? Theme.AccentSoft : (hover ? ColorTranslator.FromHtml("#F8FAFC") : Color.White);
        using (GraphicsPath p = Theme.Round(r, 10))
        using (Brush b = new SolidBrush(fill))
        using (Pen pen = new Pen(Selected ? Theme.Accent : Theme.Line, Selected ? 2F : 1F))
        {
            g.FillPath(b, p);
            g.DrawPath(pen, p);
        }
        bool empty = Info == null || Info.Empty;
        Rectangle badge = new Rectangle(14, (Height - 34) / 2, 34, 34);
        using (Brush b = new SolidBrush(empty ? ColorTranslator.FromHtml("#E5E7EB") : Theme.Accent))
            g.FillEllipse(b, badge);
        TextRenderer.DrawText(g, N.ToString(), Theme.Semi, badge, empty ? Theme.Muted : Color.White,
                              TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter);
        int right = Width - (DeleteButton.Visible ? DeleteButton.Width + 24 : 16);
        if (empty)
        {
            TextRenderer.DrawText(g, "Vazio", Theme.Semi, new Rectangle(60, 8, 200, 24), Theme.Muted, TextFormatFlags.Left);
            TextRenderer.DrawText(g, "Solta aqui um ficheiro ou um link, ou carrega para escolher este lugar",
                                  Theme.Small, new Rectangle(60, 30, Math.Max(40, right - 60), 20), Theme.Muted,
                                  TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
        }
        else
        {
            TextRenderer.DrawText(g, Info.Name, Theme.Semi, new Rectangle(60, 8, Math.Max(40, right - 60), 24), Theme.Text,
                                  TextFormatFlags.Left | TextFormatFlags.EndEllipsis);
            TextRenderer.DrawText(g, AppLogic.FormatMB(Info.Size), Theme.Small, new Rectangle(60, 30, 200, 20), Theme.Muted,
                                  TextFormatFlags.Left);
        }
    }
}
