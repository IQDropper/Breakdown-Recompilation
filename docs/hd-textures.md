# HD textures (your own copy of the game)

Three steps. Everything stays on your PC: dumps, images and packs come from your own game files, so never commit
or share them.

1. **Dump** the textures of the places you play: run the game with `RECOMP_TEX_DUMP=<folder>`. Each texture is
   saved once as `<hash>_<w>x<h>_<format>.tga`.
2. **Upscale** them:
   ```
   py -3 tools\upscale_tex.py <dump folder> <out folder> --scale 4 --edge wrap
   ```
   - Lanczos-3 (smooth, sharp edges, no dark fringes on cut-outs). Standard library only; it uses every CPU core,
     and Pillow if you have it installed (much faster).
   - `--edge wrap` suits tiling walls and floors; `clamp` (default) suits cut-outs, UI and atlases.
   - `--only <hash> ...` does just some textures.
   - `--external "your-upscaler.exe {in} {out}"` runs an upscaler you installed yourself (for example an ESRGAN
     build) instead: `{in}` is a PNG of the original and `{out}` the PNG it must write. The script downloads nothing.
   - You can also edit or repaint any image by hand: keep the first 16 hex digits of its name.
3. **Pack** and play:
   ```
   py -3 tools\make_tex_pack.py <out folder> hd.pack
   ```
   Then run with `RECOMP_TEXTURES_PACK=<full path to hd.pack>`.

**Measured** (doorway room, 167 textures, i7-9750H, standard library path):
- Upscale x4 took 6.5 minutes; building the pack took several more (not timed). The pack is **1.1 GB**
  (uncompressed, with mipmaps).
- In game the pack loads (`[TEXPACK] ... 167 entries`) and is used.
- Small print, labels and thin edges get cleaner. Lanczos can't invent detail, so big surfaces look about the same;
  an AI upscaler (`--external`) is what adds detail.
- Size: x2 is a quarter of x4. Leaving out the 640x480 full-screen images (render targets, about half the bytes
  here) shrinks the pack a lot.
