"""patch_mono_audio.py - idempotent. A11Y-11 mono audio: the toolkit's XAudio2 output mixes left and right into
both channels when xa2_set_mono(1) is set (the PC options row 'Mono audio', RECOMP_MONO_AUDIO=1). For one-sided
hearing or a single earbud: sounds placed hard left or right are heard in both ears. Applied by
APPLY_RENDER_PATCHES.cmd."""
import io

P = r'external\xboxrecomp\src\apu\apu_xaudio2.c'
s = io.open(P, encoding='utf-8', newline='').read()
if '[mono-audio]' in s:
    print('already patched')
    raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'


def rep(old, new):
    global s
    old = old.replace('\n', nl)
    new = new.replace('\n', nl)
    assert s.count(old) == 1, 'site not found: ' + old[:60]
    s = s.replace(old, new, 1)


rep('''int xa2_submit_samples(const int16_t *samples, int num_samples)
{''', '''/* [mono-audio] A11Y-11: 1 = left and right averaged into both channels before XAudio2 gets them. */
static volatile int g_xa2_mono;
void xa2_set_mono(int on)
{
    g_xa2_mono = on ? 1 : 0;
}

int xa2_submit_samples(const int16_t *samples, int num_samples)
{''')

rep('''        memcpy(g_xa2_bufs[idx][g_xa2_fill], samples,
               n * XA2_CHANNELS * sizeof(int16_t));''', '''        memcpy(g_xa2_bufs[idx][g_xa2_fill], samples,
               n * XA2_CHANNELS * sizeof(int16_t));
        if (g_xa2_mono) {   /* [mono-audio] */
            int k;
            for (k = 0; k < n; k++) {
                int16_t *f = g_xa2_bufs[idx][g_xa2_fill + k];
                int16_t m = (int16_t)(((int)f[0] + (int)f[1]) / 2);
                f[0] = m;
                f[1] = m;
            }
        }''')

io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched: [mono-audio] xa2_set_mono')
