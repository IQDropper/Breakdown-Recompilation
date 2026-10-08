#!/bin/sh
# Assemble the Windows release folder:
#
#   <OUT>/Breakdown.exe          the launcher (build.sh)
#   <OUT>/README.txt
#   <OUT>/tools/VERSION
#   <OUT>/tools/python/          embeddable Python + capstone, cmake, ninja
#   <OUT>/tools/zig/             zig (the C compiler)
#   <OUT>/tools/shims/           zcc/zar/zranlib/zrc.exe, which CMake runs
#   <OUT>/tools/implib/          libxinput.a, libd3dcompiler.a
#   <OUT>/tools/toolkit/         xboxrecomp, committed HEAD
#   <OUT>/tools/project/         this repository's sources (working tree, minus
#                                anything gitignored, which is where every
#                                game-derived file lives, and minus the
#                                developer tools the build never runs)
#
# No game data: the player's disc is the only input, on their own machine.
#
#   OUT         default /mnt/dev/Breakdown-Windows
#   VERSION     default 0.2.2
#   XBOXRECOMP  default /mnt/dev/xboxrecomp-fork
#   ZIG         zig for building the launcher here (default zig on PATH)
#   CACHE       downloads from fetch_toolchain.sh
set -e
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PROJECT=$(dirname "$HERE")
OUT=${OUT:-/mnt/dev/Breakdown-Windows}
VERSION=${VERSION:-0.2.2}
XBOXRECOMP=${XBOXRECOMP:-/mnt/dev/xboxrecomp-fork}
CACHE=${CACHE:-$HOME/.cache/breakdown-win-toolchain}

"$HERE/fetch_toolchain.sh"
OUT="$HERE/out" "$HERE/build.sh"

STAGE=$(mktemp -d "${TMPDIR:-/tmp}/breakdown-release.XXXXXX")
trap 'rm -rf "$STAGE"' EXIT
T=$STAGE/tools
mkdir -p "$T/python/Lib/site-packages" "$T/toolkit" "$T/project"

cp "$HERE/out/Breakdown.exe" "$STAGE/"
cp "$HERE/README.txt" "$STAGE/README.txt"
printf '%s\n' "$VERSION" > "$T/VERSION"

# Python. Its ._pth file fixes sys.path (the embeddable distribution ignores
# PYTHONPATH and the working directory), so site-packages is added there.
unzip -q "$CACHE/python-3.14.7-embed-amd64.zip" -d "$T/python"
printf 'python314.zip\n.\nLib\\site-packages\n' > "$T/python/python314._pth"
for w in "$CACHE"/wheels/*.whl; do unzip -q -o "$w" -d "$T/python/Lib/site-packages"; done
# Unzipping is not installing: a wheel's <name>.data/scripts (ninja.exe) is
# moved into place by pip. Put them beside python.exe, where the ninja
# package looks when there is no Scripts folder.
for d in "$T"/python/Lib/site-packages/*.data; do
    [ -d "$d/scripts" ] && mv "$d"/scripts/* "$T/python/"
    rm -rf "$d"
done

# zig, the shims CMake runs it through, and the two import libraries the
# game links that zig's MinGW lacks (made from zig's own .def files).
unzip -q "$CACHE/zig-x86_64-windows-0.14.1.zip" -d "$STAGE"
mv "$STAGE/zig-x86_64-windows-0.14.1" "$T/zig"
cp -r "$HERE/out/shims" "$T/shims"
mkdir -p "$T/implib"
DEFS=$T/zig/lib/libc/mingw/lib-common
"${ZIG:-zig}" dlltool -d "$DEFS/xinput1_4.def" -l "$T/implib/libxinput.a" -m i386:x86-64
"${ZIG:-zig}" dlltool -d "$DEFS/d3dcompiler_47.def" -l "$T/implib/libd3dcompiler.a" -m i386:x86-64

# Sources
git -C "$XBOXRECOMP" archive HEAD | tar -x -C "$T/toolkit"
(cd "$PROJECT" && git ls-files -co --exclude-standard -z \
    | grep -zv -e '^launcher/' -e '^launcher-win/' -e '__pycache__' \
               -e '^tools/probes/' -e '^tools/ghidra/' -e '^tools/frame-pacing/' \
               -e '^tools/scan_' -e '\.sh$' \
    | xargs -0 tar -c) | tar -x -C "$T/project"

# Line endings the Windows tools and Notepad both read.
sed -i 's/$/\r/' "$STAGE/README.txt"

mkdir -p "$OUT"
rm -rf "$OUT/tools" "$OUT/Breakdown.exe" "$OUT/README.txt"
cp -r "$STAGE"/. "$OUT"/
echo "release $VERSION assembled in $OUT ($(du -sh "$OUT" | cut -f1))"
