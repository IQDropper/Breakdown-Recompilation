using System.Numerics;
using ImGuiNET;

namespace BreakdownLauncher;

// Palette and fonts shared by every screen.
//
// Breakdown is a first-person brawler in white-and-steel corridors; the
// launcher keeps to a dark steel field with a hot amber impact colour, the
// colour the honeycomb cells flash when they are "punched". The title is a
// serif in white with a red trim, like the game's own logo.
public static class Theme
{
    public static readonly Vector4 Bg      = new(0.07f, 0.08f, 0.10f, 1f);
    public static readonly Vector4 Text    = new(0.93f, 0.94f, 0.96f, 1f);
    public static readonly Vector4 Dim     = new(0.62f, 0.66f, 0.72f, 1f);
    public static readonly Vector4 Steel   = new(0.40f, 0.48f, 0.58f, 1f);
    public static readonly Vector4 Impact  = new(1.00f, 0.58f, 0.18f, 1f);
    public static readonly Vector4 ImpactHot = new(1.00f, 0.92f, 0.72f, 1f);
    public static readonly Vector4 Warn    = new(1.00f, 0.76f, 0.30f, 1f);
    public static readonly Vector4 Error   = new(1.00f, 0.42f, 0.38f, 1f);
    public static readonly Vector4 Trim    = new(0.86f, 0.08f, 0.08f, 1f);
    // Buttons as in the PS1 recomp launchers: lighter than the field, so they
    // read at a glance across a room.
    public static readonly Vector4 Btn     = new(0.22f, 0.24f, 0.27f, 1f);
    public static readonly Vector4 BtnHov  = new(0.34f, 0.37f, 0.41f, 1f);
    public static readonly Vector4 BtnAct  = new(0.48f, 0.52f, 0.57f, 1f);
    public static readonly Vector4 Border  = new(0.45f, 0.48f, 0.52f, 1f);

    public static ImFontPtr Body;
    public static ImFontPtr Title;
    public static ImFontPtr Small;
    public static ImFontPtr Subtitle;   // "Recompilation"
    public static ImFontPtr Ui;         // menu buttons
    public static bool HaveFonts;

    // No font ships with the launcher (fonts are not ours to redistribute).
    // Use an installed face; ImGui's built-in bitmap font is the last resort.
    static readonly string[] Candidates =
    {
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
    };

    // The title face: Times New Roman where it is installed (msttcorefonts),
    // otherwise a metric-compatible clone of it.
    static readonly string[] SerifCandidates =
    {
        "/usr/share/fonts/TTF/timesbd.ttf",
        "/usr/share/fonts/TTF/Times_New_Roman_Bold.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/timesbd.ttf",
        "/usr/share/fonts/truetype/msttcorefonts/Times_New_Roman_Bold.ttf",
        "/usr/share/fonts/msttcore/timesbd.ttf",
        "/usr/share/fonts/liberation/LiberationSerif-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSerif-Bold.ttf",
        "/usr/share/fonts/liberation-serif/LiberationSerif-Bold.ttf",
        "/usr/share/fonts/truetype/croscore/Tinos-Bold.ttf",
    };

    // Called from the ImGui controller's configure hook, before the font atlas is built.
    public static void LoadFonts()
    {
        var io = ImGui.GetIO();
        var path = Candidates.FirstOrDefault(File.Exists);
        if (path == null)
        {
            Body = Title = Small = Subtitle = Ui = io.Fonts.AddFontDefault();
            return;
        }
        Body = io.Fonts.AddFontFromFileTTF(path, 20f);
        Small = io.Fonts.AddFontFromFileTTF(path, 15f);
        Subtitle = io.Fonts.AddFontFromFileTTF(path, 40f);
        Ui = io.Fonts.AddFontFromFileTTF(path, 26f);
        var serif = SerifCandidates.FirstOrDefault(File.Exists);
        Title = io.Fonts.AddFontFromFileTTF(serif ?? path, 104f);
        HaveFonts = true;
    }

    public static uint Col(Vector4 c, float alpha = 1f) =>
        ImGui.GetColorU32(c with { W = c.W * alpha });

    public static void Push()
    {
        ImGui.PushStyleColor(ImGuiCol.WindowBg, Bg);
        ImGui.PushStyleColor(ImGuiCol.Text, Text);
        ImGui.PushStyleColor(ImGuiCol.Button, Btn);
        ImGui.PushStyleColor(ImGuiCol.ButtonHovered, BtnHov);
        ImGui.PushStyleColor(ImGuiCol.ButtonActive, BtnAct);
        ImGui.PushStyleColor(ImGuiCol.Border, Border);
        ImGui.PushStyleColor(ImGuiCol.FrameBg, Btn);
        ImGui.PushStyleColor(ImGuiCol.PlotHistogram, Impact);
        ImGui.PushStyleColor(ImGuiCol.NavHighlight, Trim);
        ImGui.PushStyleVar(ImGuiStyleVar.FrameBorderSize, 1f);
        ImGui.PushStyleVar(ImGuiStyleVar.FrameRounding, 3f);
        ImGui.PushStyleVar(ImGuiStyleVar.FramePadding, new Vector2(14f, 9f));
    }

    public static void Pop()
    {
        ImGui.PopStyleVar(3);
        ImGui.PopStyleColor(9);
    }

    // A full-window, undecorated page to draw a screen into.
    public static bool BeginPage(string id)
    {
        var vp = ImGui.GetMainViewport();
        ImGui.SetNextWindowPos(vp.WorkPos);
        ImGui.SetNextWindowSize(vp.WorkSize);
        return ImGui.Begin(id, ImGuiWindowFlags.NoDecoration | ImGuiWindowFlags.NoMove |
                               ImGuiWindowFlags.NoResize | ImGuiWindowFlags.NoSavedSettings |
                               ImGuiWindowFlags.NoBringToFrontOnFocus);
    }

    public static void CentredText(string text, Vector4 colour, ImFontPtr? font = null)
    {
        if (font.HasValue) ImGui.PushFont(font.Value);
        float w = ImGui.CalcTextSize(text).X;
        ImGui.SetCursorPosX(MathF.Max(0f, (ImGui.GetWindowWidth() - w) * 0.5f));
        ImGui.TextColored(colour, text);
        if (font.HasValue) ImGui.PopFont();
    }

    public static bool CentredButton(string label, float width, float height = 0f)
    {
        ImGui.SetCursorPosX((ImGui.GetWindowWidth() - width) * 0.5f);
        return ImGui.Button(label, new Vector2(width, height));
    }

    // BREAKDOWN in white with the red trim, and "Recompilation" under it.
    // The serif's capitals sit high in a tall line; pull the second line up so
    // the two read as one logo.
    public static void Logo(Vector4 subtitle)
    {
        TrimmedText("BREAKDOWN", Text, Trim, Title, 3f);
        ImGui.SetCursorPosY(ImGui.GetCursorPosY() - 14f);
        CentredText("Recompilation", subtitle, Subtitle);
    }

    // Centred text with a coloured trim: the text drawn in the trim colour at
    // eight points around it, then the text itself on top.
    public static void TrimmedText(string text, Vector4 core, Vector4 trim, ImFontPtr font, float thickness)
    {
        ImGui.PushFont(font);
        var size = ImGui.CalcTextSize(text);
        ImGui.SetCursorPosX(MathF.Max(0f, (ImGui.GetWindowWidth() - size.X) * 0.5f));
        var origin = ImGui.GetCursorScreenPos();
        var dl = ImGui.GetWindowDrawList();
        uint t = ImGui.GetColorU32(trim);
        for (float r = 1f; r <= thickness; r += 1f)
            for (int i = 0; i < 16; i++)
            {
                float a = MathF.PI * 2f * i / 16f;
                dl.AddText(origin + new Vector2(MathF.Cos(a) * r, MathF.Sin(a) * r), t, text);
            }
        dl.AddText(origin, ImGui.GetColorU32(core), text);
        ImGui.Dummy(size);   // the draw list does not move the cursor
        ImGui.PopFont();
    }
}
