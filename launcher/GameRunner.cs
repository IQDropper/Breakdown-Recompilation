using System.Diagnostics;

namespace BreakdownLauncher;

// Runs the built game. It is a Windows executable, so on Linux it runs through
// the user's Wine, in a prefix of the launcher's own (its saves live there).
//
//   cwd   <work>/project, which holds game_files/ (the exe opens
//         game_files\default.xbe relative to it)
//   env   XBOXRECOMP_PFIFO_ASYNC=1 (the GPU front end every tested build uses),
//         XBOXRECOMP_BRIGHTNESS / XBOXRECOMP_SAVE_DIR (Options),
//         WINEPREFIX (BREAKDOWN_WINEPREFIX overrides), WINEDEBUG=-all
//   log   <data>/logs/game-<time>.log, the runtime's stderr, for bug reports
public sealed class GameRunner
{
    Process? _proc;
    public bool Running => _proc is { HasExited: false };
    public string? LastError { get; private set; }
    public string? LogPath { get; private set; }

    public static string? FindWine()
    {
        foreach (var dir in (Environment.GetEnvironmentVariable("PATH") ?? "").Split(':'))
        {
            var p = Path.Combine(dir, "wine");
            if (File.Exists(p)) return p;
        }
        return null;
    }

    public bool Start(string exe, Settings settings)
    {
        LastError = null;
        var wine = FindWine();
        if (wine == null)
        {
            LastError = "Wine is not installed. Install it with your distribution's package manager, then press Play again.";
            return false;
        }
        var project = Path.Combine(Settings.WorkDir, "project");
        var prefix = Environment.GetEnvironmentVariable("BREAKDOWN_WINEPREFIX") ?? Settings.WinePrefix;
        Directory.CreateDirectory(prefix);
        var logs = Path.Combine(Settings.DataDir, "logs");
        Directory.CreateDirectory(logs);
        LogPath = Path.Combine(logs, $"game-{DateTime.Now:yyyyMMdd-HHmmss}.log");

        var psi = new ProcessStartInfo(wine)
        {
            WorkingDirectory = project,
            UseShellExecute = false,
            RedirectStandardError = true,
            RedirectStandardOutput = true,
        };
        psi.ArgumentList.Add(exe);
        psi.Environment["WINEPREFIX"] = prefix;
        psi.Environment["WINEDEBUG"] = "-all";
        psi.Environment["XBOXRECOMP_PFIFO_ASYNC"] = "1";
        // Options. The save folder goes over as a Windows path: Wine maps Z:
        // to the Linux root.
        psi.Environment["XBOXRECOMP_BRIGHTNESS"] = RuntimeBrightness(settings.Brightness).ToString();
        if (!string.IsNullOrEmpty(settings.SaveDir))
            psi.Environment["XBOXRECOMP_SAVE_DIR"] = "Z:" + Path.GetFullPath(settings.SaveDir).Replace('/', '\\');
        try
        {
            _proc = Process.Start(psi);
        }
        catch (Exception e)
        {
            LastError = $"Could not start the game: {e.Message}";
            return false;
        }
        if (_proc == null) { LastError = "Could not start the game."; return false; }

        var log = new StreamWriter(LogPath) { AutoFlush = true };
        var sync = new object();
        void Write(string? line) { if (line != null) lock (sync) log.WriteLine(line); }
        _proc.ErrorDataReceived += (_, e) => Write(e.Data);
        _proc.OutputDataReceived += (_, e) => Write(e.Data);
        _proc.EnableRaisingEvents = true;
        _proc.Exited += (_, _) => { lock (sync) log.Dispose(); };
        _proc.BeginErrorReadLine();
        _proc.BeginOutputReadLine();
        return true;
    }

    // Same mapping as the Windows launcher (launcher-win/src/main.cpp): the
    // slider's middle is runtime -43, matched side by side with the original
    // game; each 50 steps halves or doubles the curve's exponent.
    public static int RuntimeBrightness(int slider)
    {
        const double middle = 1.0 / (1.0 - 0.43);
        double exponent = middle * Math.Pow(2.0, -Math.Clamp(slider, -50, 50) / 50.0);
        return (int)Math.Round(100.0 * (1.0 / exponent - 1.0));
    }
}
