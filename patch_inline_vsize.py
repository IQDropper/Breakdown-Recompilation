"""patch_inline_vsize.py - idempotent. INLINE_ARRAY vertex size: use the real byte width of every NV2A
attribute type, unpadded (as xemu does). Before: S1/S32K (16-bit) were sized 4 bytes per component
and CMP 4*size, so a vertex with a SHORT2 texcoord looked bigger than it is, 4 quad vertices
counted as 3, and the quad was dropped (Breakdown: save-slot text and thumbnails, both backends)."""
import io
P = r'external\xboxrecomp\src\kernel\nv2a_pb_exec.c'
s = io.open(P, encoding='utf-8', newline='').read()
if '[inline-vsize]' in s:
    print('already patched'); raise SystemExit
nl = '\r\n' if '\r\n' in s else '\n'
old = ('        case 0:  bytes = 4;                        break;  /* D3DCOLOR   */' + nl +
       '        case 2:  bytes = 4 * s_gpu.attr[a].size;   break;  /* float      */' + nl +
       '        case 4:  bytes = s_gpu.attr[a].size;       break;  /* ubyte norm */' + nl +
       '        default: bytes = 4 * s_gpu.attr[a].size;   break;' + nl +
       '        }' + nl +
       '        s_gpu.attr[a].offset = off;' + nl +
       '        off += (bytes + 3u) & ~3u;')
assert s.count(old) == 1, 'site not found'
new = ('        /* [inline-vsize] real widths, unpadded (xemu: stride += size * count) */' + nl +
       '        case NV2A_VTX_D3DCOLOR: bytes = 4;                       break;' + nl +
       '        case NV2A_VTX_FLOAT:    bytes = 4 * s_gpu.attr[a].size;  break;' + nl +
       '        case NV2A_VTX_UBYTE:    bytes = s_gpu.attr[a].size;      break;' + nl +
       '        case NV2A_VTX_S1:' + nl +
       '        case NV2A_VTX_S32K:     bytes = 2 * s_gpu.attr[a].size;  break;' + nl +
       '        case NV2A_VTX_CMP:      bytes = 4;                       break;' + nl +
       '        default:                bytes = 4 * s_gpu.attr[a].size;  break;' + nl +
       '        }' + nl +
       '        s_gpu.attr[a].offset = off;' + nl +
       '        off += bytes;')
s = s.replace(old, new, 1)
s = s.replace(' * ponytail: each attribute is padded to a whole dword.',
              ' * Each attribute takes its real width, unpadded ([inline-vsize]; was: padded to a dword).', 1)
io.open(P, 'w', encoding='utf-8', newline='').write(s)
print('patched')
