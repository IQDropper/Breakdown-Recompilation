using System.Numerics;
using ImGuiNET;
using Silk.NET.Input;
using Silk.NET.Maths;
using Silk.NET.OpenGL;
using Silk.NET.OpenGL.Extensions.ImGui;
using Silk.NET.Windowing;

namespace BreakdownLauncher;

// Flow, one window start to finish:
//   1. no remembered disc image (first run)  -> IsoScreen; the path is saved
//   2. BuildScreen runs tools/build_from_iso.py (seconds when already built)
//   3. SplashScreen: Play runs the game through Wine; the launcher waits on
//      the splash and comes back to it when the game exits
//
// Where things come from (an AppImage sets these; a checkout sets them by hand):
//   BREAKDOWN_BUNDLE   toolkit/, project/, zig/, VERSION  (default <app>/../bundle)
//   BREAKDOWN_PYTHON   Python with capstone, cmake, ninja (default <app>/../python/bin/python3, then python3)
//   BREAKDOWN_PREVIEW  1 = open straight on the menu, no disc or build (for checking the look)
static class Program
{
    enum Screen { Iso, Build, Splash }

    static IWindow? _window;
    static GL? _gl;
    static IInputContext? _input;
    static ImGuiController? _imgui;

    static readonly Settings _settings = Settings.Load();
    static Screen _screen;
    static IsoScreen? _iso;
    static BuildScreen? _build;
    static readonly SplashScreen _splash = new();
    static readonly GameRunner _game = new();
    static string? _exe;
    static string? _splashError;
    static bool _focused = true;
    static readonly bool _preview = Environment.GetEnvironmentVariable("BREAKDOWN_PREVIEW") == "1";

    static string Bundle =>
        Environment.GetEnvironmentVariable("BREAKDOWN_BUNDLE")
        ?? Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "bundle"));

    static string Python
    {
        get
        {
            var env = Environment.GetEnvironmentVariable("BREAKDOWN_PYTHON");
            if (!string.IsNullOrEmpty(env)) return env;
            var bundled = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "..", "python", "bin", "python3"));
            return File.Exists(bundled) ? bundled : "python3";
        }
    }

    static void Main()
    {
        var options = WindowOptions.Default with
        {
            Title = "Breakdown Recompilation",
            Size = new Vector2D<int>(1280, 720),
            VSync = true,
        };
        _window = Window.Create(options);
        _window.Load += OnLoad;
        _window.Render += OnRender;
        _window.FramebufferResize += s => _gl?.Viewport(s);
        _window.FocusChanged += f => _focused = f;
        _window.Closing += () => { _imgui?.Dispose(); _input?.Dispose(); _gl?.Dispose(); };
        _window.Run();
    }

    static void OnLoad()
    {
        var window = _window!;
        _gl = window.CreateOpenGL();
        _input = window.CreateInput();
        _imgui = new ImGuiController(_gl, window, _input, () =>
        {
            Theme.LoadFonts();
            var io = ImGui.GetIO();
            io.ConfigFlags |= ImGuiConfigFlags.NavEnableKeyboard | ImGuiConfigFlags.NavEnableGamepad;
            // Silk.NET's controller does not feed pads itself (FeedGamepad does),
            // and ImGui ignores pad navigation unless the backend says it has one.
            io.BackendFlags |= ImGuiBackendFlags.HasGamepad;
            unsafe { io.NativePtr->IniFilename = null; }   // no imgui.ini: nothing to remember
        });

        if (_preview)
            _screen = Screen.Splash;   // look at the menu without a disc or a build
        else if (_settings.IsoPath != null && IsoScreen.Validate(_settings.IsoPath) == null)
            StartBuild();
        else
        {
            _iso = new IsoScreen();
            _screen = Screen.Iso;
        }
    }

    static void StartBuild()
    {
        _build = new BuildScreen();
        _build.Start(Python, Bundle, _settings.IsoPath!);
        _screen = Screen.Build;
    }

    // Silk.NET's ImGuiController never refreshes DisplayFramebufferScale, so on a
    // fractionally scaled Wayland output (1.5x here) ImGui drew into a corner of
    // the window. Same fix as RecompOne-fork 9ec325a.
    static void SyncFramebufferScale()
    {
        var size = _window!.Size;
        var fb = _window.FramebufferSize;
        if (size.X <= 0 || size.Y <= 0 || fb.X <= 0 || fb.Y <= 0) return;
        ImGui.GetIO().DisplayFramebufferScale = new Vector2((float)fb.X / size.X, (float)fb.Y / size.Y);
    }

    // The pad drives the menus, as in the PS1 recomp launchers: D-pad or left
    // stick to move, A to choose, B to go back. Fed only while this window has
    // focus and no game is running -- the pad belongs to the game then.
    const float StickThreshold = 0.5f;
    static void FeedGamepad()
    {
        var io = ImGui.GetIO();
        var pad = _focused && !_game.Running ? _input!.Gamepads.FirstOrDefault(g => g.IsConnected) : null;
        bool Btn(ButtonName n) => pad != null && pad.Buttons.Any(b => b.Name == n && b.Pressed);
        float sx = 0f, sy = 0f;
        if (pad != null && pad.Thumbsticks.Count > 0) { sx = pad.Thumbsticks[0].X; sy = pad.Thumbsticks[0].Y; }

        io.AddKeyEvent(ImGuiKey.GamepadDpadUp,    Btn(ButtonName.DPadUp));
        io.AddKeyEvent(ImGuiKey.GamepadDpadDown,  Btn(ButtonName.DPadDown));
        io.AddKeyEvent(ImGuiKey.GamepadDpadLeft,  Btn(ButtonName.DPadLeft));
        io.AddKeyEvent(ImGuiKey.GamepadDpadRight, Btn(ButtonName.DPadRight));
        io.AddKeyEvent(ImGuiKey.GamepadFaceDown,  Btn(ButtonName.A));
        io.AddKeyEvent(ImGuiKey.GamepadFaceRight, Btn(ButtonName.B));
        io.AddKeyEvent(ImGuiKey.GamepadLStickUp,    sy < -StickThreshold);
        io.AddKeyEvent(ImGuiKey.GamepadLStickDown,  sy >  StickThreshold);
        io.AddKeyEvent(ImGuiKey.GamepadLStickLeft,  sx < -StickThreshold);
        io.AddKeyEvent(ImGuiKey.GamepadLStickRight, sx >  StickThreshold);
    }

    static void OnRender(double dt)
    {
        FeedGamepad();
        _imgui!.Update((float)dt);
        SyncFramebufferScale();
        _gl!.ClearColor(Theme.Bg.X, Theme.Bg.Y, Theme.Bg.Z, 1f);
        _gl.Clear(ClearBufferMask.ColorBufferBit);

        Theme.Push();
        ImGui.PushFont(Theme.Body);
        switch (_screen)
        {
            case Screen.Iso:
                _iso!.Draw();
                if (_iso.Accepted != null)
                {
                    _settings.IsoPath = _iso.Accepted;
                    _settings.Save();
                    StartBuild();
                }
                break;

            case Screen.Build:
                _build!.Draw();
                if (_build.ExePath != null)
                {
                    _exe = _build.ExePath;
                    _screen = Screen.Splash;
                }
                else if (_build.ChooseAgain)
                {
                    _settings.IsoPath = null;
                    _settings.Save();
                    _iso = new IsoScreen();
                    _screen = Screen.Iso;
                }
                break;

            case Screen.Splash:
                _splash.Draw(_game.Running, _splashError, _settings);
                if (_splash.Choice == SplashChoice.Play && _preview)
                {
                    _splashError = "Preview: no game is built, so Play does nothing here.";
                }
                else if (_splash.Choice == SplashChoice.Play && !_game.Running)
                {
                    _splashError = _game.Start(_exe!, _settings) ? null : _game.LastError;
                }
                else if (_splash.Choice == SplashChoice.Exit)
                {
                    _window!.Close();
                }
                _splash.Choice = SplashChoice.None;
                break;
        }
        ImGui.PopFont();
        Theme.Pop();

        _imgui.Render();
        // No need to spin while idle: the window is VSync'd, and a launcher
        // waiting on a running game has nothing to animate quickly.
        if (_game.Running) Thread.Sleep(30);
    }
}
