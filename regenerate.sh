#!/bin/sh
#
# Regenerate the recompiled C for Breakdown from the user's own disc image.
#
# The pipeline, and the seed list it depends on (with the reason for every
# seed), now live in tools/regenerate.py so the Windows launcher can run it
# too. This wrapper keeps the old entry point working.
#
# Usage:  ./regenerate.sh        then rebuild in build-win/
#   XBOXRECOMP  the xboxrecomp toolkit   (default /mnt/dev/xboxrecomp-fork)
#   PY          the Python to use        (default the toolkit's .venv)
set -e
XBOXRECOMP=${XBOXRECOMP:-/mnt/dev/xboxrecomp-fork}
PROJECT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
PY=${PY:-$XBOXRECOMP/.venv/bin/python}
[ -x "$PY" ] || { echo "missing $PY -- create the toolkit venv first" >&2; exit 1; }
exec "$PY" "$PROJECT/tools/regenerate.py" --toolkit "$XBOXRECOMP" "$@"
