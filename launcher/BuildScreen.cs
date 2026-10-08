using System.Diagnostics;
using System.Numerics;
using ImGuiNET;

namespace BreakdownLauncher;

// Runs tools/build_from_iso.py and shows its progress. The script prints one
// line per event: STAGE <name> <text>, PROGRESS <pct> <detail>, DONE <exe>,
// ERROR <message> (followed by indented context). A later launch with nothing
// to do finishes in a fraction of a second and goes straight to the splash.
public sealed class BuildScreen
{
    public string? ExePath { get; private set; }
    public bool Failed { get; private set; }
    public bool ChooseAgain { get; private set; }

    static readonly (string Key, string Label)[] Stages =
    {
        ("extract", "Extract the game from your disc"),
        ("stage", "Prepare the recompiler"),
        ("analyse", "Read the game executable"),
        ("lift", "Recompile the game code"),
        ("fix", "Apply fixes"),
        ("compile", "Compile"),
        ("ready", "Ready"),
    };

    readonly object _lock = new();
    string _stage = "";
    string _stageText = "Starting";
    float _progress;
    string _detail = "";
    readonly List<string> _errors = [];
    readonly HashSet<string> _seen = [];
    Process? _proc;

    public void Start(string python, string bundle, string iso)
    {
        var psi = new ProcessStartInfo(python)
        {
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            UseShellExecute = false,
        };
        psi.ArgumentList.Add(Path.Combine(bundle, "project", "tools", "build_from_iso.py"));
        psi.ArgumentList.Add("--iso"); psi.ArgumentList.Add(iso);
        psi.ArgumentList.Add("--work"); psi.ArgumentList.Add(Settings.WorkDir);
        psi.ArgumentList.Add("--bundle"); psi.ArgumentList.Add(bundle);
        psi.Environment["PYTHONUNBUFFERED"] = "1";
        try
        {
            _proc = Process.Start(psi);
        }
        catch (Exception e)
        {
            lock (_lock) { Failed = true; _errors.Add($"Could not start the build: {e.Message}"); }
            return;
        }
        if (_proc == null) { Failed = true; return; }
        _proc.OutputDataReceived += (_, e) => { if (e.Data != null) OnLine(e.Data); };
        _proc.ErrorDataReceived += (_, e) => { if (e.Data != null) OnLine(e.Data); };
        _proc.EnableRaisingEvents = true;
        _proc.Exited += (_, _) =>
        {
            lock (_lock)
            {
                if (ExePath == null && !Failed)
                {
                    Failed = true;
                    _errors.Add($"The build stopped unexpectedly (exit {_proc.ExitCode}).");
                }
            }
        };
        _proc.BeginOutputReadLine();
        _proc.BeginErrorReadLine();
    }

    void OnLine(string line)
    {
        Console.WriteLine(line);   // keep the full log on the terminal
        lock (_lock)
        {
            if (line.StartsWith("STAGE "))
            {
                var parts = line.Split(' ', 3);
                _stage = parts.Length > 1 ? parts[1] : "";
                _stageText = parts.Length > 2 ? parts[2] : _stage;
                _seen.Add(_stage);
                _progress = 0f;
                _detail = "";
            }
            else if (line.StartsWith("PROGRESS "))
            {
                var parts = line.Split(' ', 3);
                if (parts.Length > 1 && float.TryParse(parts[1], out var p)) _progress = p / 100f;
                _detail = parts.Length > 2 ? parts[2] : "";
            }
            else if (line.StartsWith("DONE "))
            {
                ExePath = line[5..].Trim();
            }
            else if (line.StartsWith("ERROR "))
            {
                Failed = true;
                _errors.Add(line[6..]);
            }
            else if (Failed && line.StartsWith("  "))
            {
                _errors.Add(line.Trim());
                if (_errors.Count > 14) _errors.RemoveAt(1);
            }
        }
    }

    public void Draw()
    {
        if (!Theme.BeginPage("##build")) { ImGui.End(); return; }
        lock (_lock)
        {
            float w = ImGui.GetWindowWidth();
            float col = MathF.Min(620f, w - 80f);
            float x = (w - col) * 0.5f;
            ImGui.SetCursorPosY(ImGui.GetWindowHeight() * 0.14f);
            Theme.TrimmedText("BREAKDOWN", Theme.Text, Theme.Trim, Theme.Title, 3f);
            Theme.CentredText(Failed ? "The build did not finish" : "Building the game from your disc",
                              Failed ? Theme.Error : Theme.Dim);
            Theme.CentredText("This happens once. Later launches go straight to the menu.", Theme.Dim, Theme.Small);
            ImGui.Dummy(new Vector2(0, 26));

            foreach (var (key, label) in Stages)
            {
                bool current = key == _stage && !Failed && ExePath == null;
                bool done = _seen.Contains(key) && !current;
                ImGui.SetCursorPosX(x);
                var colour = current ? Theme.Impact : done ? Theme.Text : Theme.Dim;
                ImGui.TextColored(colour, (done ? "+  " : current ? ">  " : "-  ") + label);   // ASCII: ImGui loads Latin glyphs only
            }

            ImGui.Dummy(new Vector2(0, 18));
            ImGui.SetCursorPosX(x);
            if (!Failed)
            {
                ImGui.ProgressBar(ExePath != null ? 1f : _progress, new Vector2(col, 0f),
                                  ExePath != null ? "Done" : _stageText);
                ImGui.SetCursorPosX(x);
                ImGui.PushFont(Theme.Small);
                ImGui.TextColored(Theme.Dim, _detail.Length > 90 ? _detail[..90] + "..." : _detail);
                ImGui.PopFont();
            }
            else
            {
                ImGui.PushFont(Theme.Small);
                foreach (var e in _errors)
                {
                    ImGui.SetCursorPosX(x);
                    ImGui.PushTextWrapPos(x + col);
                    ImGui.TextColored(Theme.Error, e);
                    ImGui.PopTextWrapPos();
                }
                ImGui.PopFont();
                ImGui.Dummy(new Vector2(0, 10));
                ImGui.SetCursorPosX(x);
                ImGui.TextColored(Theme.Dim, "The full log is in the terminal the launcher was started from.");
                ImGui.Dummy(new Vector2(0, 14));
                if (Theme.CentredButton("Choose a different disc image", 340f)) ChooseAgain = true;
            }
        }
        ImGui.End();
    }
}
