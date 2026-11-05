"""patch_section_tail.py - toolkit fix (XBOXRECOMP_NOTES.md item 20): a section's last instruction can straddle the
end of its FILE data. The XBE stores raw_size bytes and the loader zero-fills up to virtual_size. Breakdown's D3DX
section: raw data ends at 0x1D0EAC, virtual at 0x1D0EB0, and D3DXMatrixLookAtRH ends `ret 0x10` = C2 10 00 at
0x1D0EAA - its 00 is not in the file. The disassembler saw 2 bytes, could not decode the ret, ended the function
before it, and the lifted body fell off its end without popping 16 bytes of args + return address: esp 20 low in
every caller -> the "wake up" camera code read the stack cookie as an object pointer and crashed (2026-11-03).
Fix: get_section_data() appends the loader's zero fill, capped at 15 bytes (max x86 instruction length) so a
straddling instruction decodes but big zero-filled data tails are not swept as code. Idempotent."""
import pathlib
P = pathlib.Path(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\external\xboxrecomp\tools\disasm\loader.py')
s = P.read_text(encoding='utf-8')
old = '''        return self.raw_data[section.raw_addr:section.raw_addr + section.raw_size]'''
new = '''        data = self.raw_data[section.raw_addr:section.raw_addr + section.raw_size]
        # The loader zero-fills raw_size..virtual_size. Add up to 15 bytes of that fill (patch_section_tail.py)
        # so a last instruction whose trailing 00 byte is not in the file (D3DX `ret 0x10` in Breakdown) decodes.
        pad = max(0, min(section.virtual_size - section.raw_size, 15))
        return data + b"\\x00" * pad'''
if 'patch_section_tail.py' in s:
    print('already patched')
else:
    assert s.count(old) == 1, 'toolkit changed - patch by hand'
    P.write_text(s.replace(old, new), encoding='utf-8'); print('patched tools/disasm/loader.py: section tail zero-fill')
