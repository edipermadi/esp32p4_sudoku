#!/usr/bin/env bash
# Readies a build folder for flashing everything: tools/build_extras.sh BUILD_DIR
#
#   - checks the app fits its slot in partitions.csv (Partition Scheme "Custom" lets
#     the IDE accept up to 16 MB, more than a slot holds)
#   - copies extras/avatars.bin into BUILD_DIR and adds it to BUILD_DIR/flash_args at
#     the avatars partition's offset, so the release files and the web flasher write
#     the avatars too (once there, they stay: firmware updates don't touch them)
#
# Safe to run more than once. Used by tools/package_release.sh and make_flasher_site.sh.
set -euo pipefail

build=${1:?build dir}
here=$(cd "$(dirname "$0")/.." && pwd)

python3 - "$build" "$here" <<'PY'
import os, shutil, sys
build, here = sys.argv[1:3]
parts = {}
for line in open(os.path.join(here, "partitions.csv")):
    line = line.split("#")[0].strip()
    if not line:
        continue
    name, typ, sub, off, size = [w.strip() for w in line.split(",")[:5]]
    parts[name] = (int(off, 0), int(size, 0))

app = os.path.getsize(os.path.join(build, "esp32p4_sudoku.ino.bin"))
slot = min(parts[n][1] for n in ("app0", "app1"))
print("app: %d bytes of a %d-byte slot (%d%%)" % (app, slot, app * 100 // slot))
if app > slot:
    sys.exit("the app doesn't fit its slot in partitions.csv")

av = os.path.join(here, "extras", "avatars.bin")
off, size = parts["avatars"]
if os.path.getsize(av) > size:
    sys.exit("extras/avatars.bin is bigger than the avatars partition")
shutil.copy(av, os.path.join(build, "avatars.bin"))
fa = os.path.join(build, "flash_args")
lines = open(fa).read().splitlines()
if not any(l.split()[-1:] == ["avatars.bin"] for l in lines):
    lines.append("0x%x avatars.bin" % off)
    open(fa, "w").write("\n".join(lines) + "\n")
print("flash_args: avatars.bin @ 0x%x" % off)
PY
