using System.Diagnostics;
using System.Numerics;
using ImGuiNET;

namespace BreakdownLauncher;

public enum SplashChoice { None, Play, Exit }

// The front screen: the honeycomb backdrop, the title, Play, Options, About
// and Exit. Shown once the game is built; Play hands over to GameRunner, and
// the launcher returns here when the game exits. Same pages as the Windows
// launcher (launcher-win/src/main.cpp); keep the two in step.
public sealed class SplashScreen
{
    public SplashChoice Choice { get; set; } = SplashChoice.None;

    // Kept in step with BreakdownLauncher.csproj <Version>; bump both together.
    const string AppVersion = "0.2.2";

    enum Page { Main, Options, About }

    const float ButtonW = 320f, ButtonH = 58f;

    readonly HoneycombBackground _honeycomb = new();
    bool _focusPending = true;
    Page _page = Page.Main;

    public void Draw(bool gameRunning, string? error, Settings settings)
    {
        if (!Theme.BeginPage("##splash")) { ImGui.End(); return; }
        var vp = ImGui.GetMainViewport();
        var dl = ImGui.GetWindowDrawList();
        _honeycomb.Draw(dl, vp.WorkPos, vp.WorkSize);
        float h = ImGui.GetWindowHeight();
        float w = ImGui.GetWindowWidth();

        if (_page != Page.Main)
        {
            // A dark plate over the whole honeycomb: these pages are for reading.
            dl.AddRectFilled(vp.WorkPos, vp.WorkPos + vp.WorkSize, Theme.Col(Theme.Bg, 0.90f));
            ImGui.SetCursorPosY(40f);
            if (_page == Page.Options) DrawOptions(settings);
            else DrawAbout();
            // B on the pad, or Escape, goes back -- unless a slider or text
            // field is being edited, where the same key means "stop editing".
            if (!ImGui.IsAnyItemActive() &&
                (ImGui.IsKeyPressed(ImGuiKey.GamepadFaceRight, false) || ImGui.IsKeyPressed(ImGuiKey.Escape, false)))
                GoBack();
            ImGui.End();
            return;
        }

        ImGui.SetCursorPosY(h * 0.14f);
        // A dark plate behind the title so the honeycomb never fights the text.
        var p = ImGui.GetCursorScreenPos();
        dl.AddRectFilled(new Vector2(vp.WorkPos.X, p.Y - 16f),
            new Vector2(vp.WorkPos.X + w, p.Y + 170f), Theme.Col(Theme.Bg, 0.82f));
        Theme.Logo(Theme.Impact);
        ImGui.Dummy(new Vector2(0, 30));

        if (gameRunning)
        {
            Theme.CentredText("Breakdown is running.", Theme.Text);
            Theme.CentredText("Close the game window to come back here.", Theme.Dim, Theme.Small);
        }
        else
        {
            // Sized and spaced as in the PS1 recomp launchers.
            ImGui.PushFont(Theme.Ui);
            if (Theme.CentredButton("Play Game", ButtonW, ButtonH)) Choice = SplashChoice.Play;
            // Something is selected on arrival, so a pad user need not press a
            // direction first to find out the menu is there.
            if (_focusPending) { ImGui.SetItemDefaultFocus(); ImGui.SetKeyboardFocusHere(-1); _focusPending = false; }
            ImGui.Dummy(new Vector2(0, 8));
            if (Theme.CentredButton("Options", ButtonW, ButtonH)) _page = Page.Options;
            ImGui.Dummy(new Vector2(0, 8));
            if (Theme.CentredButton("About", ButtonW, ButtonH)) _page = Page.About;
            ImGui.Dummy(new Vector2(0, 8));
            if (Theme.CentredButton("Exit", ButtonW, ButtonH)) Choice = SplashChoice.Exit;
            ImGui.PopFont();
            if (error != null)
            {
                ImGui.Dummy(new Vector2(0, 10));
                Theme.CentredText(error, Theme.Error, Theme.Small);
            }
        }

        // Footer: version and the known issue.
        ImGui.SetCursorPosY(h - 64f);
        Theme.CentredText($"Known issue in {AppVersion}: in some rooms the background music plays a short repeating fragment.",
                          Theme.Warn, Theme.Small);
        Theme.CentredText($"v{AppVersion}  -  rebuilt from your own disc; no game data is included.",
                          Theme.Dim, Theme.Small);
        ImGui.End();
    }

    void GoBack() { _page = Page.Main; _focusPending = true; }

    void BackButton()
    {
        ImGui.Dummy(new Vector2(0, 18));
        ImGui.PushFont(Theme.Ui);
        if (Theme.CentredButton("Back", 200f, 46f)) GoBack();
        ImGui.PopFont();
    }

    void DrawOptions(Settings settings)
    {
        float w = MathF.Min(640f, ImGui.GetWindowWidth() - 80f);
        float x = (ImGui.GetWindowWidth() - w) * 0.5f;

        Theme.CentredText("Options", Theme.Impact);
        ImGui.Dummy(new Vector2(0, 18));

        ImGui.SetCursorPosX(x);
        ImGui.TextColored(Theme.Text, "Brightness");
        ImGui.SetCursorPosX(x);
        ImGui.SetNextItemWidth(w);
        int b = settings.Brightness;
        if (ImGui.SliderInt("##brightness", ref b, -50, 50, b == 0 ? "Original" : "%+d"))
        {
            settings.Brightness = b;
            settings.Save();
        }
        Wrapped(x, w, Theme.Dim, "Original, in the middle, matches the original game. Move left for a darker "
                                 + "picture, right to brighten the dark parts.");

        ImGui.Dummy(new Vector2(0, 26));
        ImGui.SetCursorPosX(x);
        ImGui.TextColored(Theme.Text, "Saved games folder");
        bool custom = !string.IsNullOrEmpty(settings.SaveDir);
        Wrapped(x, w, custom ? Theme.Text : Theme.Dim,
                settings.EffectiveSaveDir + (custom ? "" : "  (default)"));
        ImGui.Dummy(new Vector2(0, 6));
        ImGui.SetCursorPosX(x);
        if (ImGui.Button("Choose folder..."))
        {
            var r = NativeFileDialogSharp.Dialog.FolderPicker(settings.EffectiveSaveDir);
            if (r.IsOk && !string.IsNullOrEmpty(r.Path))
            {
                settings.SaveDir = Path.GetFullPath(r.Path);
                settings.Save();
            }
        }
        ImGui.SameLine();
        if (ImGui.Button("Open folder")) OpenFolder(settings.EffectiveSaveDir);
        if (custom)
        {
            ImGui.SameLine();
            if (ImGui.Button("Use default")) { settings.SaveDir = null; settings.Save(); }
        }
        Wrapped(x, w, Theme.Dim, "Saves already made are not moved. To keep them, copy the contents of the old "
                                 + "folder into the new one.");

        BackButton();
    }

    static void OpenFolder(string dir)
    {
        try
        {
            Directory.CreateDirectory(dir);   // the default exists only after a first game
            Process.Start(new ProcessStartInfo("xdg-open") { ArgumentList = { dir }, UseShellExecute = false });
        }
        catch (Exception e)
        {
            Console.Error.WriteLine($"[launcher] cannot open {dir}: {e.Message}");
        }
    }

    static void Wrapped(float x, float w, Vector4 colour, string text)
    {
        ImGui.SetCursorPosX(x);
        ImGui.PushTextWrapPos(x + w);
        ImGui.TextColored(colour, text);
        ImGui.PopTextWrapPos();
    }

    static void Section(string title)
    {
        ImGui.Dummy(new Vector2(0, 10));
        ImGui.TextColored(Theme.Impact, title);
        ImGui.Separator();
    }

    static void Item(string name, string text)
    {
        ImGui.TextColored(Theme.Text, name);
        ImGui.Indent(18f);
        ImGui.TextColored(Theme.Dim, text);
        ImGui.Unindent(18f);
        ImGui.Dummy(new Vector2(0, 4));
    }

    void DrawAbout()
    {
        Theme.CentredText("About Recompilation", Theme.Impact);
        Theme.CentredText($"v{AppVersion}", Theme.Dim, Theme.Small);
        ImGui.Dummy(new Vector2(0, 10));

        float w = MathF.Min(760f, ImGui.GetWindowWidth() - 60f);
        ImGui.SetCursorPosX((ImGui.GetWindowWidth() - w) * 0.5f);
        float h = ImGui.GetContentRegionAvail().Y - 90f;
        if (ImGui.BeginChild("##about", new Vector2(w, h)))
        {
            ImGui.PushTextWrapPos(w - 24f);

            // First, not last: a warning nobody scrolls to is a warning nobody reads.
            Section("Work in progress");
            ImGui.TextColored(Theme.Warn, "This is an early recompilation and it has not been played through. "
                                          + "Expect problems, including ones that can block progress. Save often, "
                                          + "and keep more than one save.");

            Section("Recompilation");
            Item("xboxrecomp", "by sp00nznet -- the Xbox static recompiler and runtime this is built on (MIT). "
                               + "This project uses a fork of it. Not affiliated with or endorsed by them.");
            Item("xemu", "the open-source Xbox emulator (GPL-2.0-or-later). The runtime's graphics and audio "
                         + "hardware code is derived from it, and it is the reference this port is checked against.");

            Section("Original game");
            Item("Breakdown", "developed and published by Namco (2004). All game code, assets and trademarks "
                              + "belong to their respective owners. This launcher distributes none of them -- it "
                              + "rebuilds the game from the disc you already own, on your own computer.");

            Section("Libraries and tools");
            Item("Dear ImGui", "by Omar Cornut and contributors (MIT), through ImGui.NET -- this launcher's interface");
            Item("Silk.NET", "by the .NET Foundation and contributors (MIT)");
            Item("NativeFileDialogSharp", "bindings for nativefiledialog (zlib)");
            Item(".NET", "by Microsoft (MIT)");
            Item("Wine", "by the Wine project (LGPL-2.1-or-later) -- runs the game on Linux");
            Item("Capstone", "disassembly framework (BSD) -- reads the game's code during the build");
            Item("Python", "by the Python Software Foundation (PSF License) -- runs the build");
            Item("Zig", "by the Zig Software Foundation (MIT) -- the C compiler for the build");
            Item("CMake and Ninja", "by Kitware (BSD-3-Clause) and the Ninja authors (Apache-2.0)");

            ImGui.Dummy(new Vector2(0, 8));
            ImGui.TextColored(Theme.Dim, "Licenses differ between components. See each project's own license for the "
                                         + "terms that apply to it.");
            ImGui.PopTextWrapPos();
        }
        ImGui.EndChild();

        BackButton();
    }
}
