"""compare_seeds.py - which addresses that Heromachine's Breakdown-Recompilation seeds by hand (tools/regenerate.py;
addresses are facts about the binary) are NOT function starts in OUR recompiler output? Those are crash suspects."""
import json
HM = ('0x00018930 0x0001b900 0x0001fd40 0x00020e30 0x00024cb0 0x0002565a 0x00026240 0x00026760 0x000267b7 0x00033070 0x00033130 '
      '0x00033170 0x000412b0 0x0007dc60 0x00096ce0 0x000ae2c0 0x000ae6c0 0x000af960 0x000c6330 0x000cf750 0x000cf7e9 0x000cf895 '
      '0x000cf8c3 0x000cf8f8 0x000cf943 0x000cf9c5 0x000cfa53 0x000cfb01 0x000cfb0f 0x000cfb14 0x000cfb1c 0x000d7894 0x000d7df0 '
      '0x000f5480 0x000f5860 0x000f6a60 0x000f6f20 0x00100373 0x00113110 0x001135d0 0x00113630 0x001138b0 0x00113ee2 0x00115650 '
      '0x00117870 0x0011796d 0x0011c4c0 0x0011c621 0x0011c743 0x0011d137 0x001251d0 0x00125dee 0x00129240 0x001292b8 0x001315b0 '
      '0x00134f10 0x001674b0 0x0016fc60 0x00171c90 0x0017e309 0x0017f320 0x0017f360 0x0018c880 0x001a7f72 0x001a8460 0x001af8e5 '
      '0x001af8ed 0x001af8f0 0x001af8f5 0x001af8f6 0x001af92e 0x001af944 0x001b011f 0x001b0b0e 0x001b1594 0x001b32ee 0x001b3c54 '
      '0x001b5f75 0x001b5f9f 0x001b6603 0x001b6849 0x001bd439 0x001bd4a2 0x001bd4e3 0x001bd518 0x001bd543 0x001bd55b 0x001bd570 '
      '0x001bd660 0x001bd670 0x001bd680 0x001bd7c0 0x001bd7d0 0x001bd7e0 0x001bd7f0 0x001bd800 0x001bd840 0x001d136c 0x001d1379 '
      '0x001d1636 0x001d1e08 0x001d1e4f 0x001d1e9d 0x001d1f04 0x001d1f51 0x001d1f9c 0x001d2460 0x001d2bcd 0x001d36c0 0x001d3a11 '
      '0x001d4d5f 0x001d4d6b 0x001d4f3f 0x001d558f 0x001d5a85 0x001d5b6a 0x001d5db5 0x001d5e00 0x001d62df 0x001d63e5 0x001d7e73 '
      '0x001d8397 0x001d88ab 0x001d8a14 0x001dab69 0x001dbd74 0x00204907 0x00206456 0x0020a719 0x002262b0 0x002262f0 0x0022637c '
      '0x00250510 0x0028f58c').split()
d = json.load(open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\analysis\disasm\functions.json')); d = d.get('functions', d) if isinstance(d, dict) else d
items = d.values() if isinstance(d, dict) else d
toint = lambda v: int(v, 16) if isinstance(v, str) else v
F = sorted((toint(f['start']), toint(f['end'])) for f in items); starts = {s for s, _ in F}
missing = []
for a in (int(h, 16) for h in HM):
    if a in starts: continue
    host = next((s for s, e in F if s < a < e), None)
    missing.append((a, host))
print(f'{len(HM)} of their seeds; {len(HM) - len(missing)} already function starts in ours; {len(missing)} missing:')
for a, host in missing: print(f'  {a:#010x}  ' + (f'inside our sub_{host:08X}' if host else 'not covered by any function'))
json.dump([{'start': f'{a:#x}', 'source': 'heromachine-seed-list (missing in ours)', 'observed': False} for a, _ in missing],
          open(r'C:\Users\iron_\OneDrive\Emulation\xr\breakdown\config\hm_missing_seeds.json', 'w'), indent=1)
