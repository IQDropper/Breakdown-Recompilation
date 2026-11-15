"""patch_data_spots.py - toolkit fix (P3.3): stop translating bytes that are proven to be data.

The disassembler decodes some data as code: switch jump tables placed after a function's last ret, the XMV
video codec tables and XPP strings in the XDK library sections. The translator then emits that garbage, and
every garbage call or jump that lands outside a known function becomes an "unresolved" stub
(src/recomp/gen/recomp_stubs_unresolved.c). 19 of the 20 stubs were this; config/data_spots.json lists each
data range with what reads it.

This patch makes tools.recomp read those ranges (entries with "data"/"end") from its --seeds files
(game.toml [pipeline] seeds lists config/data_spots.json) and:
  - drops a function whose start lies inside a data range (it was data from its first byte), and
  - never translates an instruction that starts inside one; decoding restarts at the range's end, so real
    code after a table in the middle of a function (the CRT memcpy) is still translated.
  - always writes recomp_stubs_unresolved.c, also with 0 stubs: the stock code skips it then, and the old file
    (with a stub for 0x1B2C2E, now a real function) would stay in gen/ and clash at link time.
tools.disasm reads the same file as a seed file and finds no "start" in it, so analysis is unchanged.
Idempotent. Run from the game root after `breakdown setup`; then `breakdown recomp`."""
import os, pathlib
import pathlib
import sys

TK = pathlib.Path(os.environ.get("BREAKDOWN_ROOT") or pathlib.Path(__file__).resolve().parent) / 'external' / 'xboxrecomp' / 'tools' / 'recomp'
TAG = 'patch_data_spots.py'


def patch(name, edits):
    p = TK / name
    s = p.read_text(encoding='utf-8')
    todo = [(old, new) for old, new in edits if new not in s]
    if not todo:
        print(f'{name}: already patched')
        return
    for old, new in todo:
        if s.count(old) != 1:
            sys.exit(f'{name}: toolkit changed, patch by hand (anchor not found once): {old[:60]!r}')
        s = s.replace(old, new)
    p.write_text(s, encoding='utf-8')
    print(f'{name}: patched ({len(todo)} of {len(edits)} edits)')


# 1. disasm.py: the data ranges, and the decoder skips them.
patch('disasm.py', [
    ('from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM\n',
     '''from capstone import Cs, CS_ARCH_X86, CS_MODE_32, CS_OP_IMM, CS_OP_MEM

# Byte ranges proven to be data (patch_data_spots.py): [(start, end), ...], end exclusive. Loaded from the
# entries with "data" and "end" in the --seeds files (config/data_spots.json).
DATA_SPOTS = []


def load_data_spots(paths):
    """Read the "data"/"end" entries of the seed files into DATA_SPOTS; returns how many."""
    import json
    DATA_SPOTS.clear()
    for path in paths or ():
        with open(path, "r", encoding="utf-8") as fh:
            for entry in json.load(fh):
                if isinstance(entry, dict) and "data" in entry:
                    DATA_SPOTS.append((int(str(entry["data"]), 16), int(str(entry["end"]), 16)))
    DATA_SPOTS.sort()
    return len(DATA_SPOTS)


def in_data_spot(addr):
    return any(s <= addr < e for s, e in DATA_SPOTS)
'''),
    ('''        size = end_va - start_va
        if size <= 0 or size > len(raw_bytes):
            return []

        decoded = {}
        for cs_insn in self._cs.disasm(raw_bytes[:size], start_va):''',
     '''        size = end_va - start_va
        if size <= 0 or size > len(raw_bytes):
            return []
        # patch_data_spots.py: decoding restarts where a data range inside the function ends.
        resync = set(resync or ()) | {e for s, e in DATA_SPOTS if start_va < e < end_va}

        decoded = {}
        for cs_insn in self._cs.disasm(raw_bytes[:size], start_va):'''),
    ('''                decoded[cs_insn.address] = self._decode_instruction(cs_insn)

        return [decoded[a] for a in sorted(decoded)]''',
     '''                decoded[cs_insn.address] = self._decode_instruction(cs_insn)

        # patch_data_spots.py: what was decoded from proven data is not translated.
        return [decoded[a] for a in sorted(decoded) if not in_data_spot(a)]'''),
])

# 2. translator.py: a function that starts inside data is not a function.
patch('translator.py', [
    ('from .disasm import Disassembler\n',
     'from .disasm import Disassembler, in_data_spot  # in_data_spot: patch_data_spots.py\n'),
    ('''        self.func_db = {}
        for func in func_list:
            addr = int(func["start"], 16)
            func["_addr"] = addr''',
     '''        self.func_db = {}
        self.data_spot_functions = []  # patch_data_spots.py
        for func in func_list:
            addr = int(func["start"], 16)
            if in_data_spot(addr):
                self.data_spot_functions.append(addr)
                continue
            func["_addr"] = addr'''),
    # With no stubs left the stock code writes no stub file, and the last run's file stays in gen/: its stale
    # stubs (sub_001B2C2E) would then clash with the real functions at link time. Always write it.
    ('''        if unresolved:
            stub_path = os.path.join(output_dir, f"{prefix}_stubs_unresolved.c")''',
     '''        if True:  # patch_data_spots.py: also with 0 stubs, so a stale file never survives
            stub_path = os.path.join(output_dir, f"{prefix}_stubs_unresolved.c")'''),
])

# 3. __main__.py: load the ranges before the translator reads functions.json, and say what was dropped.
patch('__main__.py', [
    ('''    print(f"Loading data files...", file=sys.stderr)
    t0 = time.time()
''',
     '''    print(f"Loading data files...", file=sys.stderr)
    t0 = time.time()
    from .disasm import load_data_spots  # patch_data_spots.py
    n_spots = load_data_spots(args.seeds)
'''),
    ('''    translator.observed_seeds = _load_observed_seeds(args.seeds)
''',
     '''    translator.observed_seeds = _load_observed_seeds(args.seeds)
    print(f"Data spots: {n_spots} ranges; {len(translator.data_spot_functions)} functions inside them "
          f"dropped (patch_data_spots.py)", file=sys.stderr)
'''),
])
