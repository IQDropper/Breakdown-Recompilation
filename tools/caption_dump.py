"""caption_dump.py - read Breakdown's subtitle / caption files ('CAP', pak kind 5) without playing.

Layout (worked out 2026-11-14; see docs/formats/captions.md):
  0x00 'CAP\\0'  0x04 u32 entry count  0x08 u32 1  0x0c u32 1
  0x10 u32 glyph table offset (0x20)  0x14 u32 0x28  0x18 u32 index offset  0x1c u32 text offset
  0x20 u16 glyph count, u16 font texture id (a 'nut_font_...' texture), u8 cell w, u8 cell h, ...
       then glyph rects: u16 x, u16 y, u8 w, u8 h, u16 0  (where each letter sits in the font texture)
  index: entry count x { u32 text offset in u16 units, u16 caption id, u16 priority (50/100/150...) }
  text:  u16 codes
         0x01NN  glyph NN: 0-9 digits, 10-35 A-Z, 36-61 a-z, 62+ punctuation / accents (differs per font)
         0x0003  space        0x0001  new line          0x0002  clear (next page; ends a string in cap_syst)
         0x0006  + f32 secs   wait: keep what is on screen for this long (a wait before any text = delay)
         0x000C  + ...        insert a number (system text only)          0x0000  end of caption

Files: area packs carry cap_<scene>_<area>_<lang>, e.g. cap_s09_axis_e; common/<lang>.pak carries cap_syst_<lang>
(menus and system messages). Languages: e English (US), b English (UK), f French, g German, i Italian, s Spanish.
Level events (.evd EV_TYPE_STREAM) name the caption with SOUND_CAPTION = caption id.

Usage:
  python tools/caption_dump.py game_files/romdata/area/axis/axis.pak            English captions of one pack
  python tools/caption_dump.py game_files/romdata --lang g                       German, every pack
  python tools/caption_dump.py game_files/romdata --srt-ready out.csv           id, stream, area, text, seconds
"""
import csv
import glob
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bdpk  # noqa: E402

bdpk.KINDS.setdefault(5, 'caption')
LETTERS = '0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz'
# Punctuation of the English area-caption font, read from context (cap_s09_axis_e and others).
# Other fonts (cap_syst, other languages) number their extra glyphs differently; those print as {n}.
PUNCT_EN = {63: "'", 64: ',', 65: '.', 66: '!', 67: '?', 68: '-', 69: '"', 70: '#', 71: '(', 72: ')',
            74: ':', 75: '/'}   # 73 and 77 still unread (73 sits in times like 08{73}30)
# Extra glyphs are numbered per font texture, so the map is keyed by the font id in the file header.
PUNCT_BY_FONT = {
    2931: PUNCT_EN,                                                      # US English captions
    4528: PUNCT_EN,                                                      # UK English captions
    4531: {62: "'", 64: '(', 66: ')', 69: ',', 70: '.', 71: ':', 72: '?'},  # US English menus (cap_syst_e)
}


def decode(d, off, punct):
    """-> list of pages; each page = (text with '\\n' line breaks, seconds held, delay before it)."""
    pages, cur, delay, o = [], [], 0.0, off
    while o + 2 <= len(d):
        (c,) = struct.unpack_from('<H', d, o)
        o += 2
        if c >= 0x100:
            g = c & 0xff
            cur.append(LETTERS[g] if g < 62 else punct.get(g, '{%d}' % g))
        elif c == 3:
            cur.append(' ')
        elif c == 1:
            cur.append('\n')
        elif c == 6:
            (secs,) = struct.unpack_from('<f', d, o)
            o += 4
            if cur:
                pages.append((''.join(cur), round(secs, 2), round(delay, 2)))
                cur, delay = [], 0.0
            else:
                delay += secs
        elif c == 2:
            if cur:
                pages.append((''.join(cur), 0.0, round(delay, 2)))
                cur, delay = [], 0.0
        elif c == 0:
            break
        else:
            cur.append('<%d>' % c)
    if cur:
        pages.append((''.join(cur), 0.0, round(delay, 2)))
    return pages


def read_cap(d, punct=None):
    if d[:4] != b'CAP\0':
        raise ValueError('not a CAP block')
    n, _, _, goff, _, ioff, toff = struct.unpack_from('<7I', d, 4)
    _, font = struct.unpack_from('<HH', d, goff)
    if punct is None:
        punct = PUNCT_BY_FONT.get(font, {})
    out = []
    for j in range(n):
        t, cid, prio = struct.unpack_from('<IHH', d, ioff + j * 8)
        out.append((cid, prio, decode(d, toff + t * 2, punct)))
    return font, out


def caps_in(pak, lang):
    with open(pak, 'rb') as f:
        for k, _, size, off, name in bdpk.entries(pak):
            if k == 5 and name.endswith('_' + lang):
                f.seek(off)
                font, entries = read_cap(f.read(size))
                for cid, prio, pages in entries:
                    yield name, cid, prio, pages


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    lang = argv[argv.index('--lang') + 1] if '--lang' in argv else 'e'
    target = argv[0]
    paks = sorted(glob.glob(os.path.join(target, '**', '*.pak'), recursive=True)) if os.path.isdir(target) else [target]
    rows, seen = [], set()
    for pak in paks:
        for name, cid, prio, pages in caps_in(pak, lang):
            if (name, cid) in seen:
                continue
            seen.add((name, cid))
            rows.append((name, cid, prio, pages))
    if '--srt-ready' in argv:
        streams = {}
        try:
            from evd_dump import read_evd, get
            root = target if os.path.isdir(target) else os.path.dirname(os.path.dirname(os.path.dirname(target)))
            for p in glob.glob(os.path.join(root, 'area', '*', '*.evd')):
                for ev in read_evd(p):
                    if get(ev, 'TYPE') == 'EV_TYPE_STREAM' and get(ev, 'SOUND_CAPTION') is not None:
                        streams[get(ev, 'SOUND_CAPTION')] = get(ev, 'SOUND_STREAM')
        except Exception as e:  # .evd files are optional for this view
            print('no .evd stream names:', e)
        out = argv[argv.index('--srt-ready') + 1]
        with open(out, 'w', newline='', encoding='utf-8') as f:
            w = csv.writer(f)
            w.writerow(['caption_id', 'stream', 'file', 'page', 'delay_s', 'hold_s', 'text'])
            for name, cid, prio, pages in rows:
                for i, (text, hold, delay) in enumerate(pages):
                    w.writerow([cid, streams.get(cid, ''), name, i, delay, hold, text.replace('\n', ' / ')])
        print('wrote', out, len(rows), 'captions')
        return 0
    for name, cid, prio, pages in rows:
        body = '  ||  '.join('%s%s [%.2fs]' % ('(+%.2fs) ' % dl if dl else '', t.replace('\n', ' / '), h)
                            for t, h, dl in pages)
        print('%-22s %4d p%-3d %s' % (name, cid, prio, body))
    print('%d captions' % len(rows), file=sys.stderr)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
