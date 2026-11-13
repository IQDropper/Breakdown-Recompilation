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

## AI upscaling (Real-ESRGAN) - recommended

Sharper, with real-looking detail. Lanczos (step 2 above) is only the fallback when you don't want to download
anything: next to the AI result it isn't worth the time. Get **realesrgan-ncnn-vulkan** (Windows zip) from the official
[Real-ESRGAN releases](https://github.com/xinntao/Real-ESRGAN/releases) (v0.2.5.0,
`realesrgan-ncnn-vulkan-20220424-windows.zip`, 45 MB) and unzip it, for example into `third_party\realesrgan\`
(git-ignored). It runs on the graphics card (Vulkan). Then:
```
py -3 tools\upscale_tex.py <dump folder> <out folder> --max-side 512 --jobs 3 --skip 541051AC84F7F18D ^
   --external "third_party\realesrgan\realesrgan-ncnn-vulkan.exe -i {in} -o {out} -n realesrgan-x4plus"
```
- One image per call, 3 at a time: 141 textures of the doorway room in about 9 minutes on a GTX 1660 Ti.
- **Don't use folder mode** (`--external-batch` with `-i <folder>`) with this Real-ESRGAN build: on a 142-image
  folder it gave about half the textures another texture's picture (objects drew black or wrong in game). A
  3-image folder was fine, so it fails only on big batches.
- Every external output is now checked: shrunk back to the original size and compared. A mismatch is reported as
  FAIL and not written, so that texture stays original in game (seen: one 8x8 texture the AI turned to noise).
- `--max-side 512` leaves out the 640x480 full-screen images (they are half the pack's size).
- **Linear textures are skipped automatically** (format code 10-41 hex at the end of the dump name, e.g. `_13`):
  the game addresses them in pixels, so a bigger copy shows the wrong part.
- **HUD sheets: keep them original** with `--skip <hash>`. `541051AC84F7F18D` (doorway room) holds the life bar's
  tiny pieces; upscaled, the life bar turned white and grey. (The rest of that sheet is leftover GIMP source code
  a developer left in the image; the game never shows it.)
- Transparency is kept (same share of see-through pixels before and after).
- **Watch for:** AI invents detail. Tiny text and logos turn into made-up letters (seen on a monitor label). Fix
  those by hand (below).
- Result (doorway room, 4x render): sharper bolts, panels, edges and equipment; HUD unchanged; 140 textures plus the
  hand-fixed label = a 487 MB pack. Screens in the GO kit: `reports/showcase/hdai_compare.png`.

## Touching up by hand (and keeping the original)

- Textures are plain images: open any upscaled (or original) PNG in Krita, GIMP, Photoshop, or Blender's texture
  paint, fix it, and save it **under the same name** (the first 16 hex digits are the texture's ID). Re-run
  `make_tex_pack.py`.
- Quick way to change one texture without rebuilding everything: put only the fixed images in a small folder and run
  `make_tex_pack.py <small folder> final.pack --include big.pack`. Images in the folder replace the same textures
  from the included pack (6 seconds instead of minutes).
- Example done: the monitor label `07EA99B74034861A` (unreadable in the original) now reads **状況報告** ("status
  report"), painted in the original purple with Yu Gothic UI.
- 3D models (shapes) can't be replaced yet: the game's model format isn't readable, and the port has no model-swap
  hook. That would be a project of its own.
- **The original look is always there.** Nothing changes the game files; a pack is only used when
  `RECOMP_TEXTURES_PACK` points at it. Keep as many packs as you like side by side (for example `ai_x4.pack`,
  `touched_up.pack`) and pick one at launch; no pack = the original textures. The game loads one pack at a time;
  `make_tex_pack.py --include other.pack` merges packs.

**Measured** (doorway room, 167 textures, i7-9750H, standard library path):
- Upscale x4 took 6.5 minutes; building the pack took several more (not timed). The pack is **1.1 GB**
  (uncompressed, with mipmaps).
- In game the pack loads (`[TEXPACK] ... 167 entries`) and is used.
- Small print, labels and thin edges get cleaner. Lanczos can't invent detail, so big surfaces look about the same;
  an AI upscaler (`--external`) is what adds detail.
- Size: x2 is a quarter of x4. Leaving out the 640x480 full-screen images (render targets, about half the bytes
  here) shrinks the pack a lot.
