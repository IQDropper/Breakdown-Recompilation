using System.Numerics;
using ImGuiNET;

namespace BreakdownLauncher;

// The splash screen's backdrop: a faint honeycomb, and every so often a cell
// is "punched". The struck cell flashes hot and dents inward; the shock runs
// outward ring by ring, each ring later and weaker; everything fades back.
//
// Drawn with ImGui's draw list only (no textures, no shaders), in the role
// RadarBackground plays for the Alien Resurrection launcher. It keeps its own
// time off ImGui's DeltaTime, so it animates whenever frames are pumped.
public sealed class HoneycombBackground
{
    // An impact: where, when, and how hard.
    struct Hit { public int Q; public int R; public float Age; public float Strength; }

    readonly List<Hit> _hits = [];
    readonly Random _rng = new();
    float _nextHit = 0.4f;

    const float CellRadius = 34f;       // centre to corner, logical pixels
    const float HitMinGap = 0.55f;      // seconds between punches
    const float HitMaxGap = 1.6f;
    const int ShockRings = 3;           // how far a punch carries
    const float RingDelay = 0.07f;      // seconds per ring for the shock to arrive
    const float RingFalloff = 0.45f;    // strength kept per ring outward
    const float Rise = 0.05f;           // flash attack
    const float Fade = 0.9f;            // flash decay
    const float GridAlpha = 0.10f;

    static readonly float Sqrt3 = MathF.Sqrt(3f);

    public void Draw(ImDrawListPtr dl, Vector2 origin, Vector2 size)
    {
        float dt = Math.Clamp(ImGui.GetIO().DeltaTime, 0f, 0.1f);
        float w = Sqrt3 * CellRadius;           // pointy-top hexes
        float h = 1.5f * CellRadius;
        int cols = (int)(size.X / w) + 2;
        int rows = (int)(size.Y / h) + 2;

        Advance(dt, cols, rows);

        // Per-cell glow from every live impact.
        var glow = new Dictionary<(int, int), float>();
        foreach (var hit in _hits)
        {
            for (int dq = -ShockRings; dq <= ShockRings; dq++)
            for (int dr = -ShockRings; dr <= ShockRings; dr++)
            {
                int q = hit.Q + dq, r = hit.R + dr;
                int d = HexDistance(hit.Q, hit.R, q, r);
                if (d > ShockRings) continue;
                float t = hit.Age - d * RingDelay;
                if (t <= 0f) continue;
                float env = t < Rise ? t / Rise : MathF.Exp(-(t - Rise) / Fade * 3f);
                float v = hit.Strength * MathF.Pow(RingFalloff, d) * env;
                glow[(q, r)] = MathF.Min(1f, glow.GetValueOrDefault((q, r)) + v);
            }
        }

        uint grid = Theme.Col(Theme.Steel, GridAlpha);
        for (int r = -1; r < rows; r++)
        for (int q = -1; q < cols; q++)
        {
            var c = Centre(origin, q, r, w, h);
            Hexagon(dl, c, CellRadius * 0.94f, grid, 1.4f);
            if (glow.TryGetValue((q, r), out float g) && g > 0.01f)
            {
                // Fill fades from white-hot to amber as the glow drops; a
                // smaller inner hexagon reads as the cell denting in.
                var fill = Vector4.Lerp(Theme.Impact, Theme.ImpactHot, MathF.Min(1f, g * 1.3f));
                HexagonFilled(dl, c, CellRadius * 0.90f, Theme.Col(fill, g * 0.55f));
                HexagonFilled(dl, c, CellRadius * (0.90f - 0.25f * g), Theme.Col(Theme.Bg, g * 0.45f));
                Hexagon(dl, c, CellRadius * 0.94f, Theme.Col(Theme.Impact, MathF.Min(1f, g * 1.4f)), 2.2f);
            }
        }
    }

    void Advance(float dt, int cols, int rows)
    {
        for (int i = _hits.Count - 1; i >= 0; i--)
        {
            var hit = _hits[i];
            hit.Age += dt;
            if (hit.Age > ShockRings * RingDelay + Rise + Fade * 2f) _hits.RemoveAt(i);
            else _hits[i] = hit;
        }
        _nextHit -= dt;
        if (_nextHit <= 0f && cols > 0 && rows > 0)
        {
            _hits.Add(new Hit
            {
                Q = _rng.Next(cols), R = _rng.Next(rows),
                Strength = 0.75f + (float)_rng.NextDouble() * 0.25f,
            });
            _nextHit = HitMinGap + (float)_rng.NextDouble() * (HitMaxGap - HitMinGap);
        }
    }

    // Offset coordinates ("odd-r": odd rows shifted half a cell right).
    static Vector2 Centre(Vector2 origin, int q, int r, float w, float h) =>
        origin + new Vector2(q * w + ((r & 1) != 0 ? w * 0.5f : 0f), r * h);

    static int HexDistance(int q1, int r1, int q2, int r2)
    {
        // odd-r offset -> cube coordinates
        int x1 = q1 - (r1 - (r1 & 1)) / 2, z1 = r1, y1 = -x1 - z1;
        int x2 = q2 - (r2 - (r2 & 1)) / 2, z2 = r2, y2 = -x2 - z2;
        return (Math.Abs(x1 - x2) + Math.Abs(y1 - y2) + Math.Abs(z1 - z2)) / 2;
    }

    static Vector2 Corner(Vector2 c, float radius, int i)
    {
        float a = MathF.PI / 180f * (60f * i - 30f);
        return c + new Vector2(radius * MathF.Cos(a), radius * MathF.Sin(a));
    }

    static void Hexagon(ImDrawListPtr dl, Vector2 c, float radius, uint col, float thickness)
    {
        for (int i = 0; i < 6; i++)
            dl.AddLine(Corner(c, radius, i), Corner(c, radius, (i + 1) % 6), col, thickness);
    }

    static void HexagonFilled(ImDrawListPtr dl, Vector2 c, float radius, uint col)
    {
        for (int i = 0; i < 6; i++)
            dl.AddTriangleFilled(c, Corner(c, radius, i), Corner(c, radius, (i + 1) % 6), col);
    }
}
