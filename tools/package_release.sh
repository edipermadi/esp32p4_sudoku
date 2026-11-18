#!/usr/bin/env bash
# Packages a build for a release: tools/package_release.sh BUILD_DIR OUT_DIR VERSION
#
#   esp32p4_sudoku-VERSION-update.bin    the app only: copy to the card as update.bin
#                                        (Settings > Data > Firmware update)
#   esp32p4_sudoku-VERSION-factory.bin   bootloader, partition table and app in one
#                                        image, flashed at 0x0: for a blank board (its
#                                        0xFF gaps cover NVS at 0x9000: the Wi-Fi setting goes)
#   esp32p4_sudoku-VERSION-flash.zip     the separate parts and flash_args (keeps NVS):
#                                        esptool ... write-flash @flash_args
#   esp32p4_sudoku-VERSION-elf.zip       the ELF file, to decode a crash's backtrace
#   SHA256SUMS
# and RELEASE.md (the release's text).
#
# Needs esptool (merge-bin); by default the newest one the esp32 core installed,
# or set ESPTOOL.
set -euo pipefail

build=${1:?build dir}
out=${2:?output dir}
ver=${3:?version}
name=esp32p4_sudoku
base="$name-$ver"

for f in "$name.ino.bin" "$name.ino.bootloader.bin" "$name.ino.partitions.bin" boot_app0.bin flash_args "$name.ino.elf"; do
    [ -f "$build/$f" ] || { echo "missing $build/$f" >&2; exit 1; }
done

if [ -z "${ESPTOOL:-}" ]; then
    ESPTOOL=$(ls -d "$HOME"/.arduino15/packages/esp32/tools/esptool_py/*/esptool 2>/dev/null | sort -V | tail -1)
fi
[ -x "${ESPTOOL:-}" ] || { echo "no esptool (set ESPTOOL)" >&2; exit 1; }

mkdir -p "$out"
out=$(cd "$out" && pwd)

cp "$build/$name.ino.bin" "$out/$base-update.bin"

# one image from 0x0, not padded to the flash size (the build's .merged.bin is 32 MB)
# shellcheck disable=SC2046
(cd "$build" && "$ESPTOOL" --chip esp32p4 merge-bin -o "$out/$base-factory.bin" $(cat flash_args)) >/dev/null

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
mkdir "$tmp/$base"
cp "$build/$name.ino.bin" "$build/$name.ino.bootloader.bin" "$build/$name.ino.partitions.bin" \
   "$build/boot_app0.bin" "$build/flash_args" "$tmp/$base/"
(cd "$tmp" && zip -qr "$out/$base-flash.zip" "$base")
(cd "$build" && zip -qj "$out/$base-elf.zip" "$name.ino.elf")

(cd "$out" && sha256sum "$base"-* > SHA256SUMS)

app_kb=$(( $(stat -c %s "$build/$name.ino.bin") / 1024 ))
cat > "$out/RELEASE.md" <<EOF
Firmware for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4B, built for **pre-v3.00 ESP32-P4 chips** (most 4B boards; \`ChipVariant=prev3\`), 32 MB flash, PSRAM. App: ${app_kb} KB.

**Update a board that already runs Sudoku**: copy \`$base-update.bin\` to the microSD card's root as \`update.bin\` (for example with \`python3 tools/devctl.py --port /dev/ttyACM0 "put $base-update.bin update.bin"\`, or Settings → Data → USB drive), then **Settings → Data → Firmware update**. Game data on the card is kept.

**Flash over USB** (the UART port), keeping the board's Wi-Fi setting: unzip \`$base-flash.zip\` and, in its folder,
\`\`\`
esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 write-flash @flash_args
\`\`\`

**A blank board**: \`$base-factory.bin\` is everything in one image from 0x0. It also overwrites the settings area in flash (NVS), so a board that had a Wi-Fi network set up forgets it. Game data on the card is kept.
\`\`\`
esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 write-flash 0x0 $base-factory.bin
\`\`\`

\`$base-elf.zip\` decodes a crash's backtrace (\`addr2line -e esp32p4_sudoku.ino.elf <addresses>\`).
EOF

ls -l "$out"
