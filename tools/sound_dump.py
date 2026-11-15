"""sound_dump.py - list every sound in Breakdown's streamed sound banks (game_files/stream/*.sth + .stw).

Layout (worked out 2026-11-14 from bgm/se/voice; see docs/formats/sound.md):
  .sth (header, little-endian)
    0x00 'strmpk\\0'          magic
    0x07 .. 0x27              bank file name ('bgm.stw') and 5 leftover pointers from the tool's memory (ignore)
    0x28 u32 count            sounds in the bank
    0x2c u32 group_count      name groups ('ds_bg0', 'ds_se3', 'ds_s01' ...)
    0x30 u32 group_off        group table: group_count x 16 bytes
                              { u32 first_sound, u32 first_name_off, char prefix[8] }
    0x34 u32 names_size
    0x38 u32 names_off        NUL-separated names, sound i = i-th name
    0x3c u32 offs_off         u32[count]: start of sound i in .stw (it ends where the next starts; the
                              last ends at the end of the file)
  .stw (data)   back-to-back RIFF/WAVE files, each 4-byte aligned:
    fmt  (0x14 bytes) codec 0x69 = Xbox ADPCM (36-byte blocks of 64 samples per channel) or 0x01 = PCM
    NXBF (0x34 bytes) Namco stream info: loop point, volume, mixer bus - see parse_nxbf()
    data

Usage:
  python tools/sound_dump.py game_files/stream                 summary of all three banks
  python tools/sound_dump.py game_files/stream/voice.sth       every sound in one bank (index, name, codec, rate, ch, secs, volume, bus, loop)
  python tools/sound_dump.py game_files/stream --csv out.csv   every sound of every bank as CSV
  python tools/sound_dump.py game_files/stream/se.sth --wav 12 out.wav   copy one sound out (still Xbox ADPCM)
  python tools/sound_dump.py game_files/romdata --packs        in-pack sound banks (pak kind 4, 'XHDp')
  python tools/sound_dump.py game_files/Default.xbe --names     the game's built-in bank/tone name table
                                                                (NUD_TONE_<BANK><TONE>, e.g. QUAKE + _L)

In-pack banks (BDPK kind 4 'snd_...', the SOUND_BANK + SOUND_TONE pairs in .evd files point here):
    0x00 'XHDp'  0x04 u8 1, u8 1   0x06 u16 bank id (the number .evd SOUND_BANK uses; NOT the pak entry id)
    0x08 u16 tone count   0x0a u16 header bytes (wave data starts here)   0x0c u32 wave data bytes
    0x10 u32 tone table offset (0x20)   0x14 u32 tone table bytes   0x18 u32 wave table offset   0x1c u32 wave table bytes
    tone  (0x58 bytes, tone i plays wave i): +0x04 pan 0x40, +0x05/+0x07 volume 0x7f, +0x0b mixer bus (28-31),
          +0x2c.. floats incl. 2000.0 / 30.0 / 1000.0 (3D distance settings)
    wave  (0x114 bytes): WAVEFORMATEX (codec 0x69, channels, rate ...) then u32 data offset, u32 data bytes,
          u32 loop flag, u32 loop start, u32 loop end / samples, u32 rate
"""
import csv
import os
import struct
import sys


def read_sth(path):
    d = open(path, 'rb').read()
    if d[:6] != b'strmpk':
        raise ValueError('%s: not a strmpk header' % path)
    count, ngroups, goff, nsize, noff, ooff = struct.unpack_from('<6I', d, 0x28)
    names = d[noff:noff + nsize].split(b'\0')
    names = [n.decode('latin-1') for n in names if n][:count]
    offs = list(struct.unpack_from('<%dI' % count, d, ooff))
    offs.append(os.path.getsize(path[:-4] + '.stw'))
    groups = []
    for g in range(ngroups):
        first, noff_g = struct.unpack_from('<II', d, goff + g * 16)
        prefix = d[goff + g * 16 + 8:goff + g * 16 + 16].split(b'\0')[0].decode('latin-1')
        groups.append((first, prefix))
    return names, offs, groups


def parse_nxbf(b):
    """NXBF chunk body: 'NXBF' + 12 u32. Meanings checked against all 1,743 sounds:
    [1] 0x1000 stream buffer size   [2] data bytes   [3] channels   [4] 0
    [5] loop start in samples, 0xFFFFFFFF = plays once   [6] sample rate
    [7] volume x1000 (1000 = full; 629 ~ -4 dB)   [8] mixer bus (28 BGM, 29 VOICE, 30 SE, 31 SYSTEM SE;
    see common/nus_config_x.txt)   [9] 1 on most sounds, 0 on 87 voice lines (meaning open)
    [10] 0 on music, 127 on SE/voice (likely effect-send / priority, open)   [11] 0   [12] samples per block (64)."""
    v = struct.unpack_from('<13I', b, 0)
    return {'bytes': v[2], 'ch': v[3], 'loop_start': None if v[5] == 0xFFFFFFFF else v[5],
            'rate': v[6], 'volume': v[7], 'bus': v[8], 'flag9': v[9], 'send': v[10], 'raw': v}


def parse_wav(w):
    if w[:4] != b'RIFF' or w[8:12] != b'WAVE':
        return None
    info = {}
    p = 12
    while p + 8 <= len(w):
        cid, size = w[p:p + 4], struct.unpack_from('<I', w, p + 4)[0]
        body = w[p + 8:p + 8 + size]
        if cid == b'fmt ':
            info['codec'], info['ch'], info['rate'], _, info['align'], info['bits'] = struct.unpack_from('<HHIIHH', body)
        elif cid == b'NXBF':
            info['nxbf'] = parse_nxbf(body)
        elif cid == b'data':
            info['data'] = size
            break
        p += 8 + size + (size & 1)
    return info


def samples(info):
    if info.get('codec') == 0x69:
        return info['data'] // info['align'] * 64
    if info.get('codec') == 1:
        return info['data'] // (info['ch'] * info['bits'] // 8)
    return 0


def list_bank(sth):
    stw = sth[:-4] + '.stw'
    names, offs, groups = read_sth(sth)
    rows = []
    with open(stw, 'rb') as f:
        for i in range(len(offs) - 1):
            f.seek(offs[i])
            w = f.read(min(0x80, offs[i + 1] - offs[i]))
            info = parse_wav(w) or {}
            n = samples(info) if info else 0
            nx = info.get('nxbf', {})
            loop = '' if nx.get('loop_start') is None else 'loop@%d' % nx['loop_start']
            rows.append({'bank': os.path.basename(sth)[:-4], 'index': i,
                         'name': names[i] if i < len(names) else '',
                         'offset': offs[i], 'size': offs[i + 1] - offs[i],
                         'codec': '0x%x' % info.get('codec', 0), 'rate': info.get('rate', 0),
                         'ch': info.get('ch', 0), 'secs': round(n / info['rate'], 2) if info.get('rate') else 0,
                         'loop': loop, 'volume': nx.get('volume', ''), 'bus': nx.get('bus', '')})
    return rows, groups


def pack_banks(root):
    """Every 'XHDp' sound bank inside every .pak under root: [(pak, entry name, bank id, tones, rates)]."""
    import glob
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import bdpk
    out = []
    for pak in sorted(glob.glob(os.path.join(root, '**', '*.pak'), recursive=True)):
        ents = [e for e in bdpk.entries(pak) if e[0] == 4]
        if not ents:
            continue
        with open(pak, 'rb') as f:
            for _, _, size, off, name in ents:
                f.seek(off)
                h = f.read(size)
                if h[:4] != b'XHDp':
                    continue
                bid, nt, hs, ds, to, ts, wo, ws = struct.unpack_from('<HHHIIIII', h, 6)
                waves = []
                for w in range(ws // 0x114):
                    codec, ch, rate = struct.unpack_from('<HHI', h, wo + w * 0x114)
                    dbytes, loop = struct.unpack_from('<II', h, wo + w * 0x114 + 0x18)
                    secs = dbytes // 36 * 64 / rate if codec == 0x69 and rate else 0
                    waves.append((codec, ch, rate, round(secs, 2), loop))
                buses = sorted({h[to + t * 0x58 + 0x0b] for t in range(nt)})
                out.append((os.path.relpath(pak, root), name, bid, nt, waves, buses))
    return out


def xbe_names(xbe):
    """Bank/tone name table compiled into default.xbe at VA 0x227848 (282 banks, 1,904 tones):
    u16 bank count, u16 tone count, u16 pad, then banks {char name[0x2e], u16 first tone} and
    tones {char name[0x2e], u16 bank}, 0x30 bytes each. Read by SndBank_PlayTone (0x23DA0)."""
    x = open(xbe, 'rb').read()
    base = struct.unpack_from('<I', x, 0x104)[0]
    nsec, shdr = struct.unpack_from('<I', x, 0x11C)[0], struct.unpack_from('<I', x, 0x120)[0] - base
    secs = [struct.unpack_from('<4I', x, shdr + i * 0x38 + 4) for i in range(nsec)]

    def off(va):
        return next(ra + va - sva for sva, vs, ra, rs in secs if sva <= va < sva + rs)

    def cstr(o):
        return x[o:x.index(bytes(1), o)].decode('latin-1')

    nb, nt = struct.unpack_from('<HH', x, off(0x227848))
    tbl = off(0x227850)
    banks = [(cstr(tbl + i * 0x30), struct.unpack_from('<h', x, tbl + i * 0x30 + 0x2e)[0]) for i in range(nb)]
    tones = [(cstr(tbl + (nb + j) * 0x30), struct.unpack_from('<h', x, tbl + (nb + j) * 0x30 + 0x2e)[0])
             for j in range(nt)]
    return banks, tones


def main(argv):
    if not argv:
        print(__doc__)
        return 1
    target = argv[0]
    if '--names' in argv:
        banks, tones = xbe_names(target)
        for b, (name, first) in enumerate(banks):
            own = [t for t in range(first, len(tones)) if tones[t][1] == b]
            print('bank %3d  %-22s %s' % (b, name, ' '.join('%d=%s' % (t - first, tones[t][0]) for t in own)))
        return 0
    if '--packs' in argv:
        from collections import Counter
        banks = pack_banks(target)
        ids = {}
        for pak, name, bid, nt, waves, buses in banks:
            ids.setdefault(bid, set()).add(name)
        print('%d bank copies in packs, %d distinct bank ids, %d sounds (tones)' % (
            len(banks), len(ids), sum(len(next(b for b in banks if b[2] == i)[4]) for i in ids)))
        print('rates', dict(Counter(w[2] for b in banks for w in b[4])))
        print('codecs', dict(Counter('0x%x' % w[0] for b in banks for w in b[4])))
        seen = set()
        for pak, name, bid, nt, waves, buses in sorted(banks, key=lambda b: b[2]):
            if bid in seen:
                continue
            seen.add(bid)
            print('bank %3d  %-28s %3d tones  rates %-18s bus %-12s %s' % (
                bid, name, nt, ','.join(str(r) for r in sorted({w[2] for w in waves})),
                ','.join(map(str, buses)), pak))
        return 0
    if '--wav' in argv:
        i = int(argv[argv.index('--wav') + 1])
        out = argv[argv.index('--wav') + 2]
        _, offs, _ = read_sth(target)
        with open(target[:-4] + '.stw', 'rb') as f:
            f.seek(offs[i])
            open(out, 'wb').write(f.read(offs[i + 1] - offs[i]))
        print('wrote', out)
        return 0
    banks = [os.path.join(target, b + '.sth') for b in ('bgm', 'se', 'voice')] if os.path.isdir(target) else [target]
    allrows = []
    for sth in banks:
        rows, groups = list_bank(sth)
        allrows += rows
        if os.path.isdir(target):
            from collections import Counter
            mins = sum(r['secs'] for r in rows) / 60
            print('%-6s %5d sounds  %6.1f min  codecs %s  rates %s  channels %s  loops %d  groups %d' % (
                rows[0]['bank'], len(rows), mins, dict(Counter(r['codec'] for r in rows)),
                dict(Counter(r['rate'] for r in rows)), dict(Counter(r['ch'] for r in rows)),
                sum(1 for r in rows if r['loop']), len(groups)))
            print('       buses %s' % dict(Counter(r['bus'] for r in rows)))
        else:
            for r in rows:
                print('%4d  %-34s %-5s %6d Hz %d ch %8.2f s  vol %4s bus %2s  %s' % (
                    r['index'], r['name'], r['codec'], r['rate'], r['ch'], r['secs'], r['volume'], r['bus'], r['loop']))
    if '--csv' in argv:
        out = argv[argv.index('--csv') + 1]
        with open(out, 'w', newline='') as f:
            w = csv.DictWriter(f, fieldnames=list(allrows[0]))
            w.writeheader()
            w.writerows(allrows)
        print('wrote', out, len(allrows), 'rows')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
