using System.Numerics;
using ImGuiNET;

namespace BreakdownLauncher;

// First run only: ask for the player's own Breakdown disc image, before any
// splash or menu. Once accepted, the path is saved and this never shows again.
public sealed class IsoScreen
{
    public string? Accepted { get; private set; }

    string _path = "";
    string? _problem;

    // The Xbox filesystem's volume descriptor, at sector 32 of the game
    // partition; the partition starts at one of these offsets (trimmed image,
    // or full XGD1/2/3 dump). Same check as tools/xiso_extract.py.
    static readonly long[] PartitionOffsets = { 0, 0x18300000, 0x0FD90000, 0x02080000 };
    static readonly byte[] Magic = "MICROSOFT*XBOX*MEDIA"u8.ToArray();

    public static string? Validate(string path)
    {
        if (string.IsNullOrWhiteSpace(path)) return "Choose a file.";
        if (!File.Exists(path)) return "That file does not exist.";
        try
        {
            using var f = File.OpenRead(path);
            var buf = new byte[Magic.Length];
            foreach (var off in PartitionOffsets)
            {
                if (off + 32 * 2048 + buf.Length > f.Length) continue;
                f.Seek(off + 32 * 2048, SeekOrigin.Begin);
                f.ReadExactly(buf);
                if (buf.AsSpan().SequenceEqual(Magic)) return null;
            }
            return "That is not an Xbox disc image.";
        }
        catch (Exception e) when (e is IOException or UnauthorizedAccessException)
        {
            return $"Cannot read the file: {e.Message}";
        }
    }

    public void Draw()
    {
        if (!Theme.BeginPage("##iso")) { ImGui.End(); return; }

        float h = ImGui.GetWindowHeight();
        ImGui.SetCursorPosY(h * 0.22f);
        Theme.Logo(Theme.Dim);
        ImGui.Dummy(new Vector2(0, 36));

        Theme.CentredText("To begin, choose your Breakdown (USA) Xbox disc image (.iso).", Theme.Text);
        Theme.CentredText("The game is rebuilt from your own disc on this computer. Nothing is downloaded.", Theme.Dim, Theme.Small);
        ImGui.Dummy(new Vector2(0, 20));

        float fieldW = MathF.Min(640f, ImGui.GetWindowWidth() - 80f);
        ImGui.SetCursorPosX((ImGui.GetWindowWidth() - fieldW) * 0.5f);
        ImGui.SetNextItemWidth(fieldW - 130f);
        ImGui.InputTextWithHint("##isopath", "/path/to/Breakdown (USA).iso", ref _path, 1024);
        ImGui.SameLine();
        if (ImGui.Button("Browse...", new Vector2(120f, 0f)))
        {
            var r = NativeFileDialogSharp.Dialog.FileOpen("iso");
            if (r.IsOk) { _path = r.Path; _problem = null; }
        }

        ImGui.Dummy(new Vector2(0, 14));
        if (Theme.CentredButton("Continue", 220f))
        {
            _problem = Validate(_path.Trim());
            if (_problem == null) Accepted = Path.GetFullPath(_path.Trim());
        }
        if (_problem != null)
        {
            ImGui.Dummy(new Vector2(0, 8));
            Theme.CentredText(_problem, Theme.Error);
        }
        ImGui.End();
    }
}
