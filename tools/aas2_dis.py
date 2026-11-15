"""aas2_dis.py - read Breakdown's behaviour scripts (BHEV files with AAS2 bytecode) as text.

Format notes: docs/formats/aas2.md. The opcode names come from the game itself (table at 0x24D270 in default.xbe).
Game-command names: config/aas2_natives.csv.

Usage:
  python tools/aas2_dis.py game_files/romdata/common/base.pak bhb_mo_hero          one script -> stdout
  python tools/aas2_dis.py game_files/romdata/common/base.pak bhb_mo_hero -o out.txt
  python tools/aas2_dis.py script.bhev                                              a script saved as a file
  python tools/aas2_dis.py game_files/romdata --check                               read every script, report counts
  python tools/aas2_dis.py game_files/romdata --dump outdir                          every script -> outdir/<pack>__<name>.txt
  python tools/aas2_dis.py game_files/romdata/common/base.pak bhb_mo_hero --moves   attack states as a markdown table
  python tools/aas2_dis.py --natives [word]                                         list the game commands (name, args, uses)
"""
import csv
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bdpk  # noqa: E402

# (name, total length in bytes incl. the opcode byte). Copied from the game's table at 0x24D270 (12-byte rows:
# opcode, name pointer, length) - handlers at 0x24D640. Opcodes >= 0x41 stop the interpreter with error 8.
OPS = [
    ('NOP', 1), ('POP', 2), ('PSHI', 2), ('PSHIN', 5), ('PSHF', 2), ('PSHFN', 3), ('PSHS', 3), ('PSHID', 3),
    ('CAL', 3), ('LBL', 1), ('JMP', 5), ('JPZ', 5), ('JPNZ', 5), ('CMP_I1_IN', 5), ('MV_I1_I0', 1), ('END', 1),
    ('PSHCIN', 5), ('POP1', 1), ('POP2', 1), ('POP3', 1), ('POP4', 1), ('POP5', 1), ('POP6', 1), ('POP7', 1),
    ('POP8', 1), ('PSHI0', 1), ('PSHI1', 1), ('PSHI2', 1), ('PSHI3', 1), ('PSHI4', 1), ('PSHI5', 1), ('PSHI6', 1),
    ('PSHI7', 1), ('PSHF0', 1), ('PSHF1', 1), ('PSHF2', 1), ('PSHF3', 1), ('PSHF4', 1), ('PSHF5', 1), ('PSHF6', 1),
    ('PSHF7', 1), ('PSHIN0', 1), ('PSHIN1', 1), ('PSHINC', 2), ('PSHINS', 3), ('PSHFN0', 1), ('PSHFNP1', 1),
    ('PSHFNM1', 1), ('PSHFNC', 2), ('PSHSC', 2), ('PSHIDC', 2), ('CMP_I1_INC', 2), ('CMP_I1_INS', 3), ('JMPC', 2),
    ('JMPS', 3), ('JPZC', 2), ('JPZS', 3), ('JPNZC', 2), ('JPNZS', 3), ('BRA', 3), ('BRAC', 2), ('BRZ', 3),
    ('BRZC', 2), ('BRNZ', 3), ('BRNZC', 2),
]
ABS_JUMPS = {'JMP', 'JPZ', 'JPNZ', 'JMPC', 'JMPS', 'JPZC', 'JPZS', 'JPNZC', 'JPNZS'}
REL_JUMPS = {'BRA', 'BRAC', 'BRZ', 'BRZC', 'BRNZ', 'BRNZC'}

# CAL operand < 0x30: a built-in operation on the stack (switch in the CAL handler 0xAB030). With a = second from
# the top and b = the top: integer ops write I0 = a op b, float ops (f...) write F0. 'set' stores b into the
# variable whose number is a (pushed with PSHID), 'preinc'..'postdec' change the variable numbered b.
CAL_OPS = {
    0: 'nop', 1: 'add', 2: 'sub', 3: 'mul', 4: 'div', 5: 'neg', 6: 'eq', 7: 'lt', 8: 'le', 9: 'gt', 10: 'ge',
    11: 'ne', 12: 'set', 13: 'preinc', 14: 'postinc', 15: 'predec', 16: 'postdec', 17: 'mod', 18: 'and',
    19: 'or', 20: 'xor', 21: 'shl', 22: 'shr', 23: 'not', 24: 'lnot', 25: 'land', 26: 'lor', 27: 'ftoi',
    28: 'itof', 29: 'fadd', 30: 'fsub', 31: 'fmul', 32: 'fdiv', 33: 'fneg', 34: 'fset', 35: 'fpreinc',
    36: 'fpostinc', 37: 'fpredec', 38: 'fpostdec', 39: 'feq', 40: 'flt', 41: 'fle', 42: 'fgt', 43: 'fge',
    44: 'fne', 45: 'fland', 46: 'flor', 47: 'flnot',
}
BINOPS = {'add': '+', 'sub': '-', 'mul': '*', 'div': '/', 'mod': '%', 'eq': '==', 'lt': '<', 'le': '<=',
          'gt': '>', 'ge': '>=', 'ne': '!=', 'and': '&', 'or': '|', 'xor': '^', 'shl': '<<', 'shr': '>>',
          'land': '&&', 'lor': '||', 'fadd': '+', 'fsub': '-', 'fmul': '*', 'fdiv': '/', 'feq': '==', 'flt': '<',
          'fle': '<=', 'fgt': '>', 'fge': '>=', 'fne': '!=', 'fland': '&&', 'flor': '||'}
NATIVE_BASE = 0x30           # CAL n with n >= 0x30 calls game command n - 0x30
NATIVE_TABLE = 0x24B478      # 1839 function pointers in default.xbe

# Game commands worked out so far: index -> (name, arg names, note). From config/aas2_natives.csv.
NATIVES = {}
ARGTYPES = {}
_names = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'config', 'aas2_natives.csv')
if os.path.exists(_names):
    for _row in csv.reader(line for line in open(_names, encoding='utf-8') if not line.startswith('#')):
        if not _row or not _row[0].isdigit():
            continue
        ARGTYPES[int(_row[0])] = _row[6] if len(_row) > 6 else ''
        if _row[1]:
            NATIVES[int(_row[0])] = (_row[1], _row[2], _row[3])

# Arguments that name something in the same script (shown by name) or an animation id (shown by its pack name).
SYMBOL_ARGS = {'state': 'STAT', 'sub': 'SUB ', 'table': 'DATA'}
ANIM_ARGS = {'anim', 'animL', 'animR'}   # and every other argument name that starts with 'anim' (animFace ...)
ANIM_NAMES = {}   # anim id -> name, filled by load_anim_names()
SCRIPT_NAMES = {}   # script id -> name (pack kind 3), for arguments named 'script'


def load_anim_names(root):
    """Animation ids are global over all packs (kind 0 entries)."""
    for r, _, fs in os.walk(root):
        for f in fs:
            if f.endswith('.pak'):
                for k, i, _sz, _off, name in bdpk.entries(os.path.join(r, f)):
                    if k == 0:
                        ANIM_NAMES.setdefault(i, name)
                    elif k == 3:
                        SCRIPT_NAMES.setdefault(i, name)


class Chunk:
    __slots__ = ('tag', 'off', 'data', 'kids')

    def __init__(self, tag, off, data):
        self.tag, self.off, self.data, self.kids = tag, off, data, []

    def find(self, tag):
        for k in self.kids:
            if k.tag == tag:
                return k
        return None

    def name(self):
        n = self.find('NAME')
        return n.data.split(b'\0')[0].decode('latin-1') if n else ''


# Chunks: 4-char tag, u32 length; bit 0 of the length set = "holds more chunks". Data is padded to 8 bytes.
# PROG has bit 0 clear but always holds one AAS2 chunk.
def parse(b, off=0, end=None):
    end = len(b) if end is None else end
    out = []
    while off + 8 <= end:
        tag = b[off:off + 4].decode('latin-1')
        (ln,) = struct.unpack_from('<I', b, off + 4)
        cont = ln & 1 or tag == 'PROG'
        ln &= ~1
        if off + 8 + ln > end:
            raise ValueError('chunk %s at 0x%x runs past its parent' % (tag, off))
        c = Chunk(tag, off, b[off + 8:off + 8 + ln])
        if cont:
            c.kids = parse(b, off + 8, off + 8 + ln)
        out.append(c)
        off += 8 + ((ln + 7) & ~7)
    return out


def decode(code):
    """[(offset, opname, operand, length)] for one CODE block. Raises ValueError on a bad opcode."""
    out, pc = [], 0
    while pc < len(code):
        op = code[pc]
        if op >= len(OPS):
            raise ValueError('bad opcode 0x%02x at 0x%x' % (op, pc))
        name, ln = OPS[op]
        if pc + ln > len(code):
            raise ValueError('%s at 0x%x cut off' % (name, pc))
        arg = None
        if ln > 1:
            arg = int.from_bytes(code[pc + 1:pc + ln], 'little', signed=True)   # last byte sign-extends
        out.append((pc, name, arg, ln))
        pc += ln
        if name == 'END' and all(x == 0 for x in code[pc:]):   # padding after the last END
            break
    return out


def cstr(tbl, off):
    if tbl is None or off < 0 or off >= len(tbl):
        return None
    end = tbl.find(b'\0', off)
    return tbl[off:end if end >= 0 else len(tbl)].decode('latin-1')


def short_name(i):
    nm = NATIVES.get(i)
    return nm[0] if nm else 'cmd_%d' % i


def arg_names(i):
    """Names of the arguments of command i, in push order."""
    nm = NATIVES.get(i)
    if nm and nm[1]:
        return nm[1].split()
    return ['?'] * len(ARGTYPES.get(i, ''))


class Syms:
    """Names inside one script: state, sub and DATA table numbers -> names."""

    def __init__(self, root=None):
        self.names = {'STAT': [], 'SUB ': [], 'DATA': []}
        if root is not None:
            for c in root.kids:
                if c.tag in self.names:
                    self.names[c.tag].append(c.name())

    def get(self, tag, n):
        lst = self.names.get(tag, [])
        return lst[n] if isinstance(n, int) and 0 <= n < len(lst) else None


# Values on the followed stack: int/float constant, ('var', n) from PSHID, ('str', s), ('x', text) an expression,
# or None (unknown).
def show_val(v, argname='', syms=None):
    if v is None:
        return '?'
    if isinstance(v, tuple):
        return {'var': '&I%s', 'str': '%r', 'x': '%s'}[v[0]] % (v[1],)
    if isinstance(v, float):
        return '%g' % v
    tag = SYMBOL_ARGS.get(argname)
    if tag and syms:
        nm = syms.get(tag, v)
        if nm:
            return nm
    if (argname in ANIM_ARGS or argname.startswith('anim')) and v in ANIM_NAMES:
        return '%s(%d)' % (ANIM_NAMES[v], v)
    if argname == 'script' and v in SCRIPT_NAMES:
        return '%s(%d)' % (SCRIPT_NAMES[v], v)
    if argname == 'mask' and v > 0:
        return '0x%X' % v
    return str(v)


def walk(aas2, syms=None):
    """Decode one AAS2 program and follow the stack: yields (pc, op, operand, length, extra).
    extra = the constant for pushes; for CAL of a game command the list of argument values. Arguments are the top
    N stack slots at the CAL (the interpreter does not pop for a call; the POPn after it does). I0/F0 are followed as
    expressions so 'PSHI0' after a call pushes e.g. CharVar_Get(43)."""
    code = aas2.find('CODE')
    if code is None:
        return
    ftbl = aas2.find('FTBL')
    ctbl = aas2.find('CTBL')
    fl = struct.unpack('<%df' % (len(ftbl.data) // 4), ftbl.data[:len(ftbl.data) // 4 * 4]) if ftbl else ()
    ins = decode(code.data)
    labels = set()
    for pc, name, arg, ln in ins:
        if name in ABS_JUMPS:
            labels.add(arg)
        elif name in REL_JUMPS:
            labels.add(pc + ln + arg)
    st = []
    i0 = f0 = None

    def txt(v):
        return show_val(v, '', None) if not isinstance(v, int) or isinstance(v, bool) else str(v)

    for pc, name, arg, ln in ins:
        if pc in labels:
            i0 = f0 = None
        extra = None
        if name in ('PSHIN', 'PSHINC', 'PSHINS'):
            extra = arg
        elif name in ('PSHIN0', 'PSHIN1'):
            extra = int(name[-1])
        elif name in ('PSHFN', 'PSHFNC'):
            extra = fl[arg] if 0 <= arg < len(fl) else None
        elif name == 'PSHFN0':
            extra = 0.0
        elif name == 'PSHFNP1':
            extra = 1.0
        elif name == 'PSHFNM1':
            extra = -1.0
        elif name in ('PSHS', 'PSHSC'):
            s_ = cstr(ctbl.data if ctbl else None, arg)
            extra = ('str', s_) if s_ is not None else None
        elif name in ('PSHID', 'PSHIDC'):
            extra = ('var', arg)
        elif name == 'PSHI0':
            extra = i0
        elif name == 'PSHF0':
            extra = f0
        elif name in ('PSHI', 'PSHF'):
            extra = ('x', '%s%d' % (name[-1], arg))
        elif re.match(r'PSH[IF][1-7]$', name):
            extra = ('x', name[-2:])
        if name.startswith('PSH') and name != 'PSHCIN':
            st.append(extra)
        elif name == 'POP':
            del st[len(st) - max(arg, 0):]
        elif name.startswith('POP'):
            del st[len(st) - int(name[3:]):]
        elif name == 'CAL' and arg >= NATIVE_BASE:
            n = arg - NATIVE_BASE
            an = arg_names(n)
            k = len(an)
            extra = ([None] * (k - len(st)) + st[len(st) - k:]) if k else []
            call = ('x', '%s(%s)' % (short_name(n), ', '.join(show_val(v, a_, syms) for v, a_ in zip(extra, an))))
            i0 = f0 = call
        elif name == 'CAL':
            op = CAL_OPS.get(arg, '')
            a_ = st[-2] if len(st) >= 2 else None
            b_ = st[-1] if st else None
            if op in BINOPS:
                v = None
                if isinstance(a_, int) and isinstance(b_, int) and not isinstance(a_, bool):
                    try:
                        v = int(eval('%d %s %d' % (a_, {'/': '//', '&&': 'and', '||': 'or'}.get(BINOPS[op], BINOPS[op]), b_)))
                    except Exception:
                        v = None
                if v is None:
                    v = ('x', '(%s %s %s)' % (txt(a_), BINOPS[op], txt(b_)))
                if op.startswith('f') and op not in ('feq', 'flt', 'fle', 'fgt', 'fge', 'fne', 'fland', 'flor'):
                    f0 = v
                else:
                    i0 = v
            elif op in ('neg', 'fneg'):
                v = -b_ if isinstance(b_, (int, float)) and not isinstance(b_, bool) else ('x', '-%s' % txt(b_))
                if op == 'neg':
                    i0 = v
                else:
                    f0 = v
            elif op == 'itof':
                f0 = float(b_) if isinstance(b_, int) else b_
            elif op == 'ftoi':
                i0 = int(b_) if isinstance(b_, float) else b_
            elif op in ('set', 'fset'):
                i0 = f0 = b_
            else:
                i0 = None
        elif name in ('CMP_I1_IN', 'CMP_I1_INC', 'CMP_I1_INS', 'MV_I1_I0'):
            i0 = None
        yield pc, name, arg, ln, extra


def listing(aas2, syms, indent='    '):
    """Readable text for one AAS2 program chunk."""
    info = aas2.find('INFO')
    lines = []
    if info and len(info.data) >= 16:
        a, b_, c, d = struct.unpack_from('<iIII', info.data)
        if (a, b_, c, d) != (-1, 0, 0, 0):
            lines.append(indent + '; INFO %d %d %d %d' % (a, b_, c, d))
    ins = list(walk(aas2, syms))
    targets = set()
    for pc, name, arg, ln, _ in ins:
        if name in ABS_JUMPS:
            targets.add(arg)
        elif name in REL_JUMPS:
            targets.add(pc + ln + arg)
    for pc, name, arg, ln, extra in ins:
        if pc in targets:
            lines.append(indent[:-2] + 'L%04x:' % pc)
        txt = name
        note = ''
        if name == 'CAL':
            if arg >= NATIVE_BASE:
                n = arg - NATIVE_BASE
                an = arg_names(n)
                txt = 'CAL  %s' % short_name(n)
                note = '#%d' % n
                if an:
                    note += '  %s(%s)' % (short_name(n), ', '.join(show_val(v, a_, syms) for v, a_ in zip(extra, an)))
            else:
                txt = 'CAL  .%s' % CAL_OPS.get(arg, arg)
        elif name in ABS_JUMPS:
            txt = '%-9s L%04x' % (name, arg)
        elif name in REL_JUMPS:
            txt = '%-9s L%04x' % (name, pc + ln + arg)
        elif name in ('PSHS', 'PSHSC'):
            txt = '%-9s %d' % (name, arg)
            note = repr(extra[1]) if extra else ''
        elif name in ('PSHFN', 'PSHFNC'):
            txt = '%-9s %d' % (name, arg)
            note = ('%g' % extra) if extra is not None else '?'
        elif arg is not None:
            txt = '%-9s %d' % (name, arg)
        lines.append('%s%04x  %-34s%s' % (indent, pc, txt, ('; ' + note) if note else ''))
    return lines


def data_values(prog):
    """DATA blocks: AAS2 holding IFDT (u32 type, u32 number of dimensions, u32 sizes...) and DATA (the values).
    Bytes after the values are left-over build-time data and are ignored."""
    aas2 = prog.find('AAS2')
    ifdt = aas2.find('IFDT') if aas2 else None
    dat = aas2.find('DATA') if aas2 else None
    if not ifdt or not dat:
        return None
    w = struct.unpack('<%dI' % (len(ifdt.data) // 4), ifdt.data)
    typ, nd, dims = w[0], w[1], list(w[2:2 + w[1]])
    count = 1
    for x in dims:
        count *= x
    count = min(count, len(dat.data) // 4)
    return typ, dims, struct.unpack('<%d%s' % (count, 'f' if typ == 1 else 'i'), dat.data[:count * 4])


def data_table(prog):
    r = data_values(prog)
    if r is None:
        return []
    typ, dims, vals = r
    out = ['    ; type %d, size %s' % (typ, ' x '.join(map(str, dims)))]
    cols = max(dims[-1] if len(dims) > 1 else len(vals), 1)
    for i in range(0, len(vals), cols):
        row = vals[i:i + cols]
        out.append('    [%2d] %s' % (i // cols, ' '.join(('%g' % v) if typ == 1 else str(v) for v in row)))
    return out


def programs(root):
    """(tag, index, name, part, aas2) for every program block of a script; part is start/run/stop/sub."""
    idx = {'STAT': 0, 'SUB ': 0}
    for c in root.kids:
        if c.tag not in idx:
            continue
        i = idx[c.tag]
        idx[c.tag] += 1
        for part in c.kids:
            if part.tag in ('STRT', 'RUN ', 'STOP'):
                p = part.find('PROG')
                kind = {'STRT': 'start', 'RUN ': 'run', 'STOP': 'stop'}[part.tag]
            elif part.tag == 'PROG':
                p, kind = part, 'sub'
            else:
                continue
            a = p.find('AAS2') if p else None
            if a:
                yield c.tag, i, c.name(), kind, a


def text(b, name=''):
    top = parse(b)
    if not top or top[0].tag != 'BHEV':
        raise ValueError('not a BHEV script')
    root = top[0]
    syms = Syms(root)
    out = []
    upd = root.find('UPDT')
    out.append('; script %s  (%s)  built %s' % (root.name(), name, upd.data.split(b'\0')[0].decode() if upd else '?'))
    atr = root.find('ATR ')
    if atr:
        out.append('; ATR %s' % atr.data.hex())
    out.append('; %d states, %d subs, %d data tables' % (len(syms.names['STAT']), len(syms.names['SUB ']),
                                                       len(syms.names['DATA'])))
    sidx = uidx = didx = 0
    for c in root.kids:
        if c.tag == 'DATA':
            out.append('')
            out.append('DATA %d %s' % (didx, c.name()))
            didx += 1
            p = c.find('PROG')
            if p:
                out += data_table(p)
        elif c.tag in ('STAT', 'SUB '):
            out.append('')
            if c.tag == 'STAT':
                out.append('STATE %d %s' % (sidx, c.name()))
                sidx += 1
            else:
                out.append('SUB %d %s' % (uidx, c.name()))
                uidx += 1
            for part in c.kids:
                if part.tag in ('STRT', 'RUN ', 'STOP'):
                    out.append('  %s:' % {'STRT': 'start', 'RUN ': 'run', 'STOP': 'stop'}[part.tag])
                    p = part.find('PROG')
                    a = p.find('AAS2') if p else None
                    if a:
                        out += listing(a, syms)
                elif part.tag == 'PROG':
                    a = part.find('AAS2')
                    if a:
                        out += listing(a, syms)
        elif c.tag == 'STOP':
            out.append('')
            out.append('STOP (top level)')
            p = c.find('PROG')
            a = p.find('AAS2') if p else None
            if a:
                out += listing(a, syms)
    return '\n'.join(out) + '\n'


# ---- move table ------------------------------------------------------------------------------------------------
MOTION_CMDS = {'Motion_SetB', 'Motion_SetIfNew', 'Motion_PlayLayer'}
PAD_CMDS = {'Pad_Pressed', 'Pad_Held', 'Pad_Released'}
MOVE_RE = re.compile(r'Attack|Nage|Counter|Sliding|rider|Throw|Uraken|Keri|genki', re.I)


def moves(b, row=0, pattern=MOVE_RE):
    """Attack-like states: animations, hit windows (Attack_Set), and the states/buttons that lead into them.
    row = which character type's row to read from Motion_* DATA tables (the hero is type 0)."""
    root = parse(b)[0]
    syms = Syms(root)
    tables = {}
    di = 0
    for c in root.kids:
        if c.tag == 'DATA':
            p = c.find('PROG')
            r = data_values(p) if p else None
            tables[di] = r
            di += 1
    info = {}
    into = {}

    def anim_of(v):
        if isinstance(v, int):
            return ANIM_NAMES.get(v, 'id %d (no animation with this id)' % v)
        return None

    for tag, i, name, part, a in programs(root):
        last_pad = None
        last_table = None
        for pc, op, arg, ln, extra in walk(a, syms):
            if op != 'CAL' or arg < NATIVE_BASE:
                continue
            cmd = short_name(arg - NATIVE_BASE)
            if cmd in PAD_CMDS and extra and isinstance(extra[0], int):
                last_pad = '%s 0x%X' % (cmd.split('_')[1].lower(), extra[0])
            elif cmd == 'Data_Get' and extra and isinstance(extra[0], int):
                last_table = extra[0]
            elif tag == 'STAT':
                d = info.setdefault(name, {'anims': [], 'hits': [], 'idx': i})
                if cmd in MOTION_CMDS and len(extra) >= 2:
                    an = anim_of(extra[1])
                    if an is None and last_table is not None and tables.get(last_table):
                        typ, dims, vals = tables[last_table]
                        cols = dims[-1] if len(dims) > 1 else 1
                        if row * cols < len(vals):
                            an = '%s (%s)' % (ANIM_NAMES.get(vals[row * cols], vals[row * cols]),
                                              syms.get('DATA', last_table))
                    if an and an not in d['anims'] and 'Hand' not in an:
                        d['anims'].append(an)
                elif cmd == 'Attack_Set' and len(extra) == 9:
                    dmg = show_val(extra[2])
                    m = re.match(r'\(\(([\d.]+) \* CharVar_Get\(43\)\) / 10\)$', dmg)
                    dmg = (m.group(1) + 'P') if m else dmg       # P = power / 10 (char var 43, normally 10)
                    h = '%s f%s-%s' % (dmg, show_val(extra[7]), show_val(extra[8]))
                    if h not in d['hits']:
                        d['hits'].append(h)
            if cmd == 'State_Change' and extra and isinstance(extra[0], int):
                tgt = syms.get('STAT', extra[0])
                if tgt:
                    src = name if tag == 'STAT' else 'SUB ' + name
                    into.setdefault(tgt, set()).add('%s%s' % (src, (' [%s]' % last_pad) if last_pad else ''))
    rows = []
    for name, d in sorted(info.items(), key=lambda x: x[1]['idx']):
        if not pattern.search(name):
            continue
        rows.append((d['idx'], name, d['anims'], d['hits'], sorted(into.get(name, ()))))
    return rows


def moves_markdown(rows):
    out = ['Damage: "8P" = 8 x power / 10 (power = character variable 43, normally 10, so 8). "f20-50" = the hit is',
           'live from animation frame 20 to 50. Several values = different branches (charge level, power-up, type).',
           '',
           '| # | State | Animation(s) | Damage, live frames | Entered from [button check] |',
           '|---|---|---|---|---|']
    for idx, name, anims, hits, src in rows:
        out.append('| %d | %s | %s | %s | %s |' % (idx, name, '<br>'.join(anims) or '-', ', '.join(hits) or '-',
                                                  '<br>'.join(src[:6]) + (' ...' if len(src) > 6 else '') or '-'))
    return '\n'.join(out) + '\n'


# ---- whole-pack tools ------------------------------------------------------------------------------------------
def scripts(root):
    """Every script in every pack: (pack path, name, bytes)."""
    for r, _, fs in sorted(os.walk(root)):
        for f in sorted(fs):
            if not f.endswith('.pak'):
                continue
            p = os.path.join(r, f)
            d = open(p, 'rb').read()
            for k, i, sz, off, name in bdpk.entries(p):
                if k == 3:
                    yield p, name, d[off:off + sz]


def check(root):
    import collections
    n = bad = states = subs = blocks = instrs = 0
    ops = collections.Counter()
    nat = collections.Counter()
    for p, name, b in scripts(root):
        n += 1
        try:
            top = parse(b)
            for c in top[0].kids:
                if c.tag == 'STAT':
                    states += 1
                if c.tag == 'SUB ':
                    subs += 1
            stack = list(top)
            while stack:
                c = stack.pop()
                stack += c.kids
                if c.tag == 'CODE':
                    blocks += 1
                    for pc, op, arg, ln in decode(c.data):
                        instrs += 1
                        ops[op] += 1
                        if op == 'CAL' and arg >= NATIVE_BASE:
                            nat[arg - NATIVE_BASE] += 1
            text(b, name)
        except Exception as e:  # report and go on
            bad += 1
            print('FAIL %s %s: %s' % (os.path.basename(p), name, e))
    print('scripts %d, failed %d, states %d, subs %d, code blocks %d, instructions %d' %
          (n, bad, states, subs, blocks, instrs))
    print('opcodes used %d of %d; game commands used %d of 1839 (highest #%d); named %d' %
          (len(ops), len(OPS), len(nat), max(nat) if nat else -1, len(NATIVES)))
    return ops, nat, bad


def romdata_root(path):
    """The romdata folder above a pack (for animation names), or None."""
    p = os.path.abspath(path)
    while True:
        if os.path.basename(p).lower() == 'romdata':
            return p
        q = os.path.dirname(p)
        if q == p:
            return None
        p = q


def natives_table(word=None):
    """The game commands from config/aas2_natives.csv, most used first (optionally only names containing word)."""
    rows = []
    for row in csv.reader(line for line in open(_names, encoding='utf-8') if not line.startswith('#')):
        if row and row[0].isdigit():
            rows.append(row)
    rows.sort(key=lambda r: -int(r[8] or 0))
    out = []
    for r in rows:
        if word and word.lower() not in (r[1] + ' ' + r[3]).lower():
            continue
        if not word and not r[1]:
            continue
        out.append('#%-5s %-26s %-36s uses %-6s %-6s %s' % (r[0], r[1] or '-', r[2], r[8], r[9], r[3]))
    return ''.join(line + '\n' for line in out)


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 1
    if a[0] == '--natives':
        sys.stdout.write(natives_table(a[1] if len(a) > 1 else None))
        return 0
    if '--check' in a:
        _, _, bad = check(a[0])
        return 1 if bad else 0
    if '--dump' in a:
        outdir = a[a.index('--dump') + 1]
        load_anim_names(a[0])
        os.makedirs(outdir, exist_ok=True)
        for p, name, b in scripts(a[0]):
            sub = os.path.splitext(os.path.basename(p))[0]
            open(os.path.join(outdir, '%s__%s.txt' % (sub, name)), 'w', encoding='utf-8').write(text(b, name))
        return 0
    rd = romdata_root(a[0])
    if rd:
        load_anim_names(rd)
    out = a[a.index('-o') + 1] if '-o' in a else None
    if os.path.isfile(a[0]) and a[0].endswith('.pak'):
        d = open(a[0], 'rb').read()
        hit = [(off, sz) for k, i, sz, off, nm in bdpk.entries(a[0]) if k == 3 and nm == a[1]]
        if not hit:
            print('no script %s in %s' % (a[1], a[0]))
            return 1
        b = d[hit[0][0]:hit[0][0] + hit[0][1]]
        name = a[1]
    else:
        b = open(a[0], 'rb').read()
        name = os.path.basename(a[0])
    t = moves_markdown(moves(b)) if '--moves' in a else text(b, name)
    if out:
        open(out, 'w', encoding='utf-8').write(t)
    else:
        sys.stdout.write(t)
    return 0


if __name__ == '__main__':
    sys.exit(main())
