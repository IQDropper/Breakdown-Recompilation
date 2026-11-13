"""patch_keyboard_remap.py - idempotent. Rebindable keyboard rows and live mouse settings (A11Y-1).

Adds to external/xboxrecomp/src/input/keyboard.c (after patch_mouse.py):
  int  xbox_InputKeyBind(const char *guest, int vk0, int vk1)   rebind the row whose guest name matches
                                                                  ("Left trigger", "Left stick up", ...); vk0 <= 0
                                                                  puts its default keys back. Returns 1 if found.
  int  xbox_InputKeyBound(const char *guest, int out[2])         the keys that row uses now (0 if not found).
  void xbox_InputMouseSet(float sens, int invert)                mouse look speed / invert, live (sens <= 0 keeps it).
The game side (src/keymap.c, src/pc_options_host.c) turns RECOMP_KEYMAP and the PC page's Controls rows into calls.
Nothing changes until something calls them. Anchors must each occur once; refuses otherwise."""
import io
import os
import pathlib

ROOT = pathlib.Path(__file__).resolve().parent
K = pathlib.Path(os.environ.get("BREAKDOWN_KEYBOARD_FILE") or ROOT / "external" / "xboxrecomp" / "src" / "input" / "keyboard.c")
H = K.with_name("keyboard.h")
MARK = "[key-remap]"

ROW_OLD = """static int row_held(const xbox_key_row *r)
{
    return xbox_FramebufferKeyDown(r->vk[0])
        || (r->vk[1] && xbox_FramebufferKeyDown(r->vk[1]));
}"""

ROW_NEW = """/* {mark} Per-row key overrides (A11Y-1): set by xbox_InputKeyBind, read by the poll. Byte stores, so a bind
 * from the window thread is safe against the game's poll. */
#define KEY_ROWS_MAX 64
static volatile uint8_t s_key_over[KEY_ROWS_MAX][2];
static volatile uint8_t s_key_has[KEY_ROWS_MAX];
static int key_row(const char *guest)
{
    size_t i;
    if (!guest)
        return -1;
    for (i = 0; i < xbox_key_map_count && i < KEY_ROWS_MAX; i++)
        if (strcmp(xbox_key_map[i].guest, guest) == 0)
            return (int)i;
    return -1;
}
int xbox_InputKeyBind(const char *guest, int vk0, int vk1)
{
    int i = key_row(guest);
    if (i < 0)
        return 0;
    if (vk0 <= 0 || vk0 > 255) {
        s_key_has[i] = 0;
        return 1;
    }
    s_key_over[i][0] = (uint8_t)vk0;
    s_key_over[i][1] = (uint8_t)(vk1 > 0 && vk1 <= 255 ? vk1 : 0);
    s_key_has[i] = 1;
    return 1;
}
int xbox_InputKeyBound(const char *guest, int out[2])
{
    int i = key_row(guest);
    if (i < 0 || !out)
        return 0;
    out[0] = s_key_has[i] ? s_key_over[i][0] : xbox_key_map[i].vk[0];
    out[1] = s_key_has[i] ? s_key_over[i][1] : xbox_key_map[i].vk[1];
    return 1;
}
static int row_held(const xbox_key_row *r)
{
    size_t i = (size_t)(r - xbox_key_map);
    if (i < KEY_ROWS_MAX && s_key_has[i])
        return xbox_FramebufferKeyDown(s_key_over[i][0])
            || (s_key_over[i][1] && xbox_FramebufferKeyDown(s_key_over[i][1]));
    return xbox_FramebufferKeyDown(r->vk[0])
        || (r->vk[1] && xbox_FramebufferKeyDown(r->vk[1]));
}"""

MOUSE_OLD = "/* Counts this poll -> a stick value. Past the title's dead zone as soon as it moves at all. */"
MOUSE_NEW = """/* {mark} Live mouse settings from the PC page. */
void xbox_InputMouseSet(float sens, int invert)
{
    mouse_config();
    if (sens > 0)
        s_mouse_sens = sens;
    s_mouse_inv = invert != 0;
}
/* Counts this poll -> a stick value. Past the title's dead zone as soon as it moves at all. */"""

H_OLD = "int  xbox_InputKeyboardPoll(XBOX_INPUT_STATE *out);"
H_NEW = """int  xbox_InputKeyboardPoll(XBOX_INPUT_STATE *out);
/* {mark} Rebind one row of the map by its guest name ("Left trigger"); vk0 <= 0 restores its default keys.
 * Returns 1 if the row exists. KeyBound gives the keys it uses now. MouseSet: look speed (> 0) and invert, live. */
int  xbox_InputKeyBind(const char *guest, int vk0, int vk1);
int  xbox_InputKeyBound(const char *guest, int out[2]);
void xbox_InputMouseSet(float sens, int invert);"""


def patch(path, edits):
    s = io.open(path, encoding="utf-8", newline="").read()
    if MARK in s:
        return "already patched"
    nl = "\r\n" if "\r\n" in s else "\n"
    for old, new in edits:
        o = old.replace("\n", nl)
        if s.count(o) != 1:
            raise SystemExit("REFUSED: %s: anchor not found exactly once: %r" % (path.name, old[:60]))
        s = s.replace(o, new.replace("{mark}", MARK).replace("\n", nl), 1)
    io.open(path, "w", encoding="utf-8", newline="").write(s)
    return "patched"


def main():
    src = io.open(K, encoding="utf-8", newline="").read()
    if "[mouse]" not in src:
        raise SystemExit("REFUSED: run patch_mouse.py first (keyboard.c has no [mouse] block)")
    print("keyboard.c:", patch(K, [(ROW_OLD, ROW_NEW), (MOUSE_OLD, MOUSE_NEW)]))
    print("keyboard.h:", patch(H, [(H_OLD, H_NEW)]))


if __name__ == "__main__":
    main()
