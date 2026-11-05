"""patch_trace_regs.py - toolkit improvement #5 (see XBOXRECOMP_NOTES.md).
recomp_trace_enter() printed eax ecx esi edi ebx esp but not edx. (ebp is a per-function local in this register model - g_ebp is declared only in the game's generated header - so it is not printed.) MSVC x86 code
passes values in edx (fastcall, 64-bit returns, compare operands), and ebp is the
caller's frame - both needed to compare a native entry against xemu. Idempotent."""
import json, pathlib
p = pathlib.Path(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\external\xboxrecomp\src\kernel\recomp_trace.c')
s = p.read_text(encoding='utf-8')
old_fmt = '"ecx=%08X esi=%08X edi=%08X ebx=%08X\\n",'
new_fmt = '"ecx=%08X edx=%08X ebx=%08X esi=%08X edi=%08X\\n",'
old_arg = 'g_esp, g_eax, g_ecx, g_esi, g_edi, g_ebx);'
new_arg = 'g_esp, g_eax, g_ecx, g_edx, g_ebx, g_esi, g_edi);'
if new_fmt in s:
    print('already patched')
else:
    i = s.find(old_fmt); j = s.find(old_arg, i)            # the args line also appears in the exit trace: patch only the one after the entry format
    assert i > 0 and 0 < j - i < 200, 'toolkit changed - patch by hand'
    s = s[:i] + new_fmt + s[i+len(old_fmt):j] + new_arg + s[j+len(old_arg):]
    p.write_text(s, encoding='utf-8')
    print('patched recomp_trace.c: +edx')
# Which functions get an entry trace (tools.recomp --trace-functions, JSON {addr: name}).
json.dump({'0x0017DDF0': 'VsHeap_New', '0x00012400': 'FUN_00012400', '0x000304D0': 'FUN_000304D0'},
          open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\config\trace_functions.json', 'w'), indent=1)
print('wrote config\\trace_functions.json')
