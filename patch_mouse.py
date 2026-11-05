"""patch_mouse.py - idempotent. Mouse for the keyboard pad (port 0), so no controller is needed.
  Mouse move  -> right stick (raw input, cursor locked to the window while captured)
  Buttons     -> RECOMP_MOUSE_BUTTONS = left,right,middle,back,forward  (default "X,B,Y,LT,RT")
                 names: A B X Y WHITE BLACK LT RT START BACK LS RS UP DOWN LEFT RIGHT NONE
  Wheel       -> D-pad up / down (one poll per notch)
  Click the window to capture the mouse (that click is not sent); F1 or losing focus releases it.
  RECOMP_MOUSE=0 turns it off; RECOMP_MOUSE_SENS (default 1200 stick units per mouse count per poll),
  RECOMP_MOUSE_INVERT=1 inverts vertical look. Needs RECOMP_KEYBOARD=1 (the keyboard pad)."""
import io
K = r'external\xboxrecomp\src\input\keyboard.c'
D = r'external\xboxrecomp\src\d3d\nv2a_pb_d3d11.c'

# ---------------------------------------------------------------- keyboard.c
s = io.open(K, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '[mouse]' not in s:
    anchor = 'int xbox_FramebufferKeyDown(int vk)'
    assert s.count(anchor) == 1, 'keyboard anchor'
    block = r'''/* [mouse] Mouse deltas from the window thread, taken by the game's poll. */
#include <stdlib.h>
static volatile int s_mouse_dx, s_mouse_dy, s_mouse_wheel;
void xbox_InputMouseMove(int dx, int dy)
{
    __atomic_add_fetch(&s_mouse_dx, dx, __ATOMIC_RELAXED);
    __atomic_add_fetch(&s_mouse_dy, dy, __ATOMIC_RELAXED);
}
void xbox_InputMouseWheel(int notches)
{
    __atomic_add_fetch(&s_mouse_wheel, notches, __ATOMIC_RELAXED);
}
int xbox_InputMouseEnabled(void)
{
    const char *e = getenv("RECOMP_MOUSE");
    return !(e && *e == '0');
}
enum { MB_NONE, MB_DIGITAL, MB_ANALOG };
static struct { int kind, target; } s_mouse_btn[5];   /* left right middle back forward */
static float s_mouse_sens = 1200.0f;
static int s_mouse_inv, s_mouse_cfg;
static void mouse_name(const char *t, size_t n, int *kind, int *target)
{
    static const struct { const char *n; int kind, target; } tab[] = {
        { "A", MB_ANALOG, XBOX_BUTTON_A }, { "B", MB_ANALOG, XBOX_BUTTON_B },
        { "X", MB_ANALOG, XBOX_BUTTON_X }, { "Y", MB_ANALOG, XBOX_BUTTON_Y },
        { "WHITE", MB_ANALOG, XBOX_BUTTON_WHITE }, { "BLACK", MB_ANALOG, XBOX_BUTTON_BLACK },
        { "LT", MB_ANALOG, XBOX_BUTTON_LTRIGGER }, { "RT", MB_ANALOG, XBOX_BUTTON_RTRIGGER },
        { "START", MB_DIGITAL, XBOX_GAMEPAD_START }, { "BACK", MB_DIGITAL, XBOX_GAMEPAD_BACK },
        { "LS", MB_DIGITAL, XBOX_GAMEPAD_LEFT_THUMB }, { "RS", MB_DIGITAL, XBOX_GAMEPAD_RIGHT_THUMB },
        { "UP", MB_DIGITAL, XBOX_GAMEPAD_DPAD_UP }, { "DOWN", MB_DIGITAL, XBOX_GAMEPAD_DPAD_DOWN },
        { "LEFT", MB_DIGITAL, XBOX_GAMEPAD_DPAD_LEFT }, { "RIGHT", MB_DIGITAL, XBOX_GAMEPAD_DPAD_RIGHT },
    };
    size_t i;
    *kind = MB_NONE; *target = 0;
    for (i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strlen(tab[i].n) == n && !strncmp(tab[i].n, t, n)) { *kind = tab[i].kind; *target = tab[i].target; return; }
}
static void mouse_config(void)
{
    const char *b = getenv("RECOMP_MOUSE_BUTTONS"), *sv = getenv("RECOMP_MOUSE_SENS"), *iv = getenv("RECOMP_MOUSE_INVERT");
    int i = 0;
    if (s_mouse_cfg) return;
    s_mouse_cfg = 1;
    if (!b || !*b) b = "X,B,Y,LT,RT";
    while (*b && i < 5) {
        const char *e = strchr(b, ',');
        size_t n = e ? (size_t)(e - b) : strlen(b);
        mouse_name(b, n, &s_mouse_btn[i].kind, &s_mouse_btn[i].target);
        i++;
        b += n + (e ? 1 : 0);
    }
    if (sv && atof(sv) > 0) s_mouse_sens = (float)atof(sv);
    s_mouse_inv = iv && *iv == '1';
    fprintf(stderr, "[INPUT] mouse: click the game window to capture, F1 releases; sens %.0f%s\n",
            s_mouse_sens, s_mouse_inv ? ", inverted" : "");
}
/* Counts this poll -> a stick value. Past the title's dead zone as soon as it moves at all. */
static int mouse_stick(int counts)
{
    float v = (float)counts * s_mouse_sens;
    if (!counts) return 0;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32767.0f) v = -32767.0f;
    if (v > 0 && v < 9000.0f) v = 9000.0f;
    if (v < 0 && v > -9000.0f) v = -9000.0f;
    return (int)v;
}

'''.replace('\n', nl)
    s = s.replace(anchor, block + anchor, 1)
    old = '    for (int a = 0; a < 4; a++)' + nl + '        axis[a] = (pos[a] - neg[a]) * 32767;'
    assert s.count(old) == 1, 'axis anchor'
    new = (old + nl +
        '    /* [mouse] */' + nl +
        '    if (xbox_InputMouseEnabled()) {' + nl +
        '        static const int mvk[5] = { 0x01, 0x02, 0x04, 0x05, 0x06 };' + nl +
        '        int dx = __atomic_exchange_n(&s_mouse_dx, 0, __ATOMIC_RELAXED);' + nl +
        '        int dy = __atomic_exchange_n(&s_mouse_dy, 0, __ATOMIC_RELAXED);' + nl +
        '        int wh = __atomic_exchange_n(&s_mouse_wheel, 0, __ATOMIC_RELAXED), b, v;' + nl +
        '        mouse_config();' + nl +
        '        v = axis[XBOX_KEY_RX] + mouse_stick(dx);' + nl +
        '        axis[XBOX_KEY_RX] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;' + nl +
        '        v = axis[XBOX_KEY_RY] + mouse_stick(s_mouse_inv ? dy : -dy);' + nl +
        '        axis[XBOX_KEY_RY] = v > 32767 ? 32767 : v < -32767 ? -32767 : v;' + nl +
        '        for (b = 0; b < 5; b++) {' + nl +
        '            if (!xbox_FramebufferKeyDown(mvk[b])) continue;' + nl +
        '            if (s_mouse_btn[b].kind == MB_DIGITAL) out->Gamepad.wButtons |= (WORD)s_mouse_btn[b].target;' + nl +
        '            else if (s_mouse_btn[b].kind == MB_ANALOG) out->Gamepad.bAnalogButtons[s_mouse_btn[b].target] = 255;' + nl +
        '        }' + nl +
        '        if (wh > 0) out->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_UP;' + nl +
        '        else if (wh < 0) out->Gamepad.wButtons |= XBOX_GAMEPAD_DPAD_DOWN;' + nl +
        '    }')
    s = s.replace(old, new, 1)
    io.open(K, 'w', encoding='utf-8', newline='').write(s)
    print('keyboard.c patched')
else:
    print('keyboard.c already patched')

# ---------------------------------------------------------- nv2a_pb_d3d11.c
s = io.open(D, encoding='utf-8', newline='').read()
nl = '\r\n' if '\r\n' in s else '\n'
if '[mouse]' not in s:
    anchor = 'static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)'
    assert s.count(anchor) == 1, 'wnd_proc'
    helpers = r'''/* [mouse] capture: cursor hidden and locked to the window, relative motion from raw input. */
extern void xbox_InputMouseMove(int dx, int dy);
extern void xbox_InputMouseWheel(int notches);
extern int xbox_InputMouseEnabled(void);
static int s_mcap;
static void mouse_capture(HWND h, int on)
{
    if (on == s_mcap)
        return;
    if (on) {
        RECT r;
        POINT a, b;
        RAWINPUTDEVICE rid;
        GetClientRect(h, &r);
        a.x = r.left; a.y = r.top; b.x = r.right; b.y = r.bottom;
        ClientToScreen(h, &a); ClientToScreen(h, &b);
        r.left = a.x; r.top = a.y; r.right = b.x; r.bottom = b.y;
        ClipCursor(&r);
        ShowCursor(FALSE);
        rid.usUsagePage = 0x01; rid.usUsage = 0x02; rid.dwFlags = 0; rid.hwndTarget = h;
        RegisterRawInputDevices(&rid, 1, sizeof rid);
    } else {
        ClipCursor(NULL);
        ShowCursor(TRUE);
        xbox_InputKeySet(0x01, 0); xbox_InputKeySet(0x02, 0); xbox_InputKeySet(0x04, 0);
        xbox_InputKeySet(0x05, 0); xbox_InputKeySet(0x06, 0);
    }
    s_mcap = on;
}
static int mouse_on(void)
{
    static int on = -1;
    if (on < 0)
        on = recomp_env(RENV_KEYBOARD) != NULL && xbox_InputMouseEnabled();
    return on;
}

'''.replace('\n', nl)
    s = s.replace(anchor, helpers + anchor, 1)
    old = '    /* Alt-tabbing away with a key held would leave it held for ever. */' + nl + '    case WM_KILLFOCUS:' + nl + '        xbox_InputKeysClear();'
    assert s.count(old) == 1, 'killfocus'
    new = ('    /* [mouse] buttons, wheel, raw motion */' + nl +
        '    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDOWN: case WM_XBUTTONDOWN:' + nl +
        '    case WM_LBUTTONUP: case WM_RBUTTONUP: case WM_MBUTTONUP: case WM_XBUTTONUP:' + nl +
        '        if (mouse_on()) {' + nl +
        '            int down = m == WM_LBUTTONDOWN || m == WM_RBUTTONDOWN || m == WM_MBUTTONDOWN || m == WM_XBUTTONDOWN;' + nl +
        '            int vk = (m == WM_LBUTTONDOWN || m == WM_LBUTTONUP) ? 0x01' + nl +
        '                   : (m == WM_RBUTTONDOWN || m == WM_RBUTTONUP) ? 0x02' + nl +
        '                   : (m == WM_MBUTTONDOWN || m == WM_MBUTTONUP) ? 0x04' + nl +
        '                   : (HIWORD(w) == XBUTTON1 ? 0x05 : 0x06);' + nl +
        '            if (down && !s_mcap) { mouse_capture(h, 1); return 0; }   /* the grabbing click is not a press */' + nl +
        '            if (s_mcap) xbox_InputKeySet(vk, down);' + nl +
        '        }' + nl +
        '        return (m == WM_XBUTTONDOWN || m == WM_XBUTTONUP) ? TRUE : 0;' + nl +
        '    case WM_MOUSEWHEEL:' + nl +
        '        if (mouse_on() && s_mcap) xbox_InputMouseWheel(GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA);' + nl +
        '        return 0;' + nl +
        '    case WM_INPUT:' + nl +
        '        if (mouse_on() && s_mcap) {' + nl +
        '            RAWINPUT ri;' + nl +
        '            UINT sz = sizeof ri;' + nl +
        '            if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &ri, &sz, sizeof(RAWINPUTHEADER)) != (UINT)-1' + nl +
        '                    && ri.header.dwType == RIM_TYPEMOUSE && !(ri.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE))' + nl +
        '                xbox_InputMouseMove((int)ri.data.mouse.lLastX, (int)ri.data.mouse.lLastY);' + nl +
        '        }' + nl +
        '        return DefWindowProcA(h, m, w, l);' + nl +
        old.replace('        xbox_InputKeysClear();', '        mouse_capture(h, 0);' + nl + '        xbox_InputKeysClear();'))
    s = s.replace(old, new, 1)
    old = '    case WM_KEYDOWN:' + nl + '    case WM_SYSKEYDOWN:' + nl + '        xbox_InputKeySet((int)w, 1);'
    assert s.count(old) == 1, 'keydown'
    s = s.replace(old, '    case WM_KEYDOWN:' + nl + '    case WM_SYSKEYDOWN:' + nl +
                  '        if (w == VK_F1 && s_mcap) { mouse_capture(h, 0); return 0; }   /* [mouse] release */' + nl +
                  '        xbox_InputKeySet((int)w, 1);', 1)
    io.open(D, 'w', encoding='utf-8', newline='').write(s)
    print('d3d11 patched')
else:
    print('d3d11 already patched')
