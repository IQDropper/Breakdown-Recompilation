using System.Text.Json;

namespace BreakdownLauncher;

// Where the launcher keeps its state. The ISO path is asked for once, on first
// run, and remembered here; nothing asks for it again unless the file is gone.
//
//   config  $XDG_CONFIG_HOME/breakdown-recomp/settings.json   (~/.config/...)
//   data    $XDG_DATA_HOME/breakdown-recomp/                  (~/.local/share/...)
//             work/      extracted game, staged recompiler, build output
//             prefix/    the Wine prefix the game runs in (saves live here unless
//                        a folder is chosen in Options)
public sealed class Settings
{
    public string? IsoPath { get; set; }

    // Options. Brightness is a slider from -50 to +50 whose middle, "Original",
    // matches the original game (GameRunner.RuntimeBrightness maps it for the
    // runtime). SaveDir null = the runtime's default, inside the Wine prefix
    // (see DefaultSaveDir).
    public int Brightness { get; set; }
    public string? SaveDir { get; set; }

    // Where the game keeps saves when no folder is chosen: the runtime's
    // %LOCALAPPDATA%\xboxrecomp, inside the launcher's Wine prefix.
    public static string DefaultSaveDir => Path.Combine(WinePrefix, "drive_c", "users",
        Environment.UserName, "AppData", "Local", "xboxrecomp");
    public string EffectiveSaveDir => string.IsNullOrEmpty(SaveDir) ? DefaultSaveDir : SaveDir;

    static string Xdg(string variable, string fallback)
    {
        var v = Environment.GetEnvironmentVariable(variable);
        return string.IsNullOrEmpty(v)
            ? Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), fallback)
            : v;
    }

    public static string ConfigDir => Path.Combine(Xdg("XDG_CONFIG_HOME", ".config"), "breakdown-recomp");
    public static string DataDir => Path.Combine(Xdg("XDG_DATA_HOME", ".local/share"), "breakdown-recomp");
    public static string WorkDir => Path.Combine(DataDir, "work");
    public static string WinePrefix => Path.Combine(DataDir, "prefix");
    static string FilePath => Path.Combine(ConfigDir, "settings.json");

    public static Settings Load()
    {
        try
        {
            if (File.Exists(FilePath))
                return JsonSerializer.Deserialize<Settings>(File.ReadAllText(FilePath)) ?? new Settings();
        }
        catch (Exception e) when (e is IOException or JsonException or UnauthorizedAccessException)
        {
            Console.Error.WriteLine($"[launcher] settings unreadable, starting fresh: {e.Message}");
        }
        return new Settings();
    }

    public void Save()
    {
        Directory.CreateDirectory(ConfigDir);
        File.WriteAllText(FilePath, JsonSerializer.Serialize(this, new JsonSerializerOptions { WriteIndented = true }));
    }
}
