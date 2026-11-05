# breakdown static recompilation

A port of the original Xbox game *breakdown* to native code with the
[xboxrecomp](https://github.com/apexJCL/xboxrecomp) toolkit, driven by
[xboxrecomp-cli](https://github.com/apexJCL/xboxrecomp-cli). Started with
`xbr new`.

**No game data here, and none ever will be.** Put your own dump of the disc
(`default.xbe` and the game's files) in `game_files/`; git ignores it.

```sh
./breakdown setup      # once: .venv, llvm-mingw, the toolkit (each download sha256-pinned)
./breakdown all        # analyze (parse, disasm, funcid, abi), recomp, build
./breakdown doctor     # what this machine has, and what is next
```

Windows: `breakdown` (the `breakdown.cmd` wrapper) or `py -3 breakdown.py`, from a
short checkout path such as `C:\g\breakdown`, with
`git config --global core.longpaths true`.

The first build is `build-win/breakdown_recomp.exe`, cross-compiled with llvm-mingw on
every host. It will crash the first time: that is where the port starts.
`src/recomp_manual.c` takes hand-written overrides, `config/seed_functions.json`
the function starts the disassembler missed, and `./breakdown analyze` then
`./breakdown recomp` regenerate `src/recomp/gen/` (never edit it by hand).
The CLI's `docs/porting.md` has the loop; `game.toml` has every setting,
documented in the CLI's `docs/manifest.md`.
