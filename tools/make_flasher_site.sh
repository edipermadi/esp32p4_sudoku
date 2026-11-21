#!/usr/bin/env bash
# Builds the web flasher site (GitHub Pages): tools/make_flasher_site.sh BUILD_DIR OUT_DIR VERSION
#
#   OUT_DIR/index.html          web/index.html (ESP Web Tools' install button)
#   OUT_DIR/manifest.json       what to flash where, from the build's flash_args
#   OUT_DIR/firmware/*.bin      bootloader, partition table, boot_app0, app, avatars
#   OUT_DIR/home.png            a screenshot for the page
#   OUT_DIR/docs/               README.md and the docs (variants, internals too) as HTML (tools/make_docs_site.py)
#
# The parts go separately (not the merged image from 0x0), so flashing leaves
# NVS, and with it the board's Wi-Fi setting, alone unless the user asks the
# installer to erase the device. Serve OUT_DIR over http(s) to try it locally:
#   python3 -m http.server -d OUT_DIR 8000    (then http://localhost:8000 in Chrome/Edge)
set -euo pipefail

build=${1:?build dir}
out=${2:?output dir}
ver=${3:?version}
here=$(cd "$(dirname "$0")/.." && pwd)

[ -f "$build/flash_args" ] || { echo "missing $build/flash_args" >&2; exit 1; }
"$(dirname "$0")/build_extras.sh" "$build"     # avatars.bin into flash_args (the installer writes it too)
rm -rf "$out"
mkdir -p "$out/firmware"
cp "$here/web/index.html" "$out/index.html"
cp "$here/docs/screenshots/home.png" "$out/home.png"
touch "$out/.nojekyll"
python3 "$here/tools/make_docs_site.py" "$out/docs" "$ver"

python3 - "$build" "$out" "$ver" <<'PY'
import json, os, shutil, sys
build, out, ver = sys.argv[1:4]
parts = []
for line in open(os.path.join(build, "flash_args")):
    words = line.split()
    if len(words) == 2 and words[0].startswith("0x"):        # "<offset> <file>" (the first line is options)
        offset, name = int(words[0], 16), words[1]
        shutil.copy(os.path.join(build, name), os.path.join(out, "firmware", name))
        parts.append({"path": "firmware/" + name, "offset": offset})
if not any(p["offset"] == 0x10000 for p in parts):
    sys.exit("flash_args has no app at 0x10000")
manifest = {
    "name": "Sudoku for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B",
    "version": ver,
    "new_install_prompt_erase": True,   # the installer asks; not erasing keeps the Wi-Fi setting
    "new_install_improv_wait_time": 0,  # the firmware doesn't speak Improv: don't wait for it
    "builds": [{"chipFamily": "ESP32-P4", "parts": parts}],
}
json.dump(manifest, open(os.path.join(out, "manifest.json"), "w"), indent=2)
print("manifest:", ", ".join("%s @ 0x%x" % (p["path"], p["offset"]) for p in parts))
PY
