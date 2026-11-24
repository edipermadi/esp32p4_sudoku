# Building and releases

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

1. Install **esp32 Arduino core 3.3.x**.
2. Install **LVGL 9.6.0** and put `lv_conf.h` at `~/Arduino/libraries/lv_conf.h`. It needs Montserrat 12–28 enabled. `extras/lv_conf.h` is the one the firmware is built with (CI copies it there); keep the two in step. It turns on LVGL's PPA draw unit (`LV_USE_PPA`, `LV_USE_PPA_IMG`): the ESP32-P4's Pixel Processing Accelerator draws plain fills and images, which makes full-screen redraws 6–34% faster (measured with `bench`, see [Remote control](devctl.md)). Two extra lines there make it build with the esp32 core's ESP-IDF 5.5; they are explained in the file.
3. Choose these settings in the Tools menu:

   | Setting          | Value                                   |
   |------------------|-----------------------------------------|
   | Board            | ESP32P4 Dev Module                      |
   | PSRAM            | Enabled (required)                      |
   | Flash Size       | 32MB (256Mb)                            |
   | Partition Scheme | Custom (the sketch's `partitions.csv`)  |
   | Chip Variant     | Match your chip (most 4B boards are pre-v3.00) |

   Or build from the command line:
   ```
   arduino-cli compile -b esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=custom,ChipVariant=prev3 .
   ```

4. Upload through the USB-UART port. Serial output is at 115200 baud.
   ```
   arduino-cli upload -b esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=custom,ChipVariant=prev3 -p /dev/ttyACM0 .
   ```

   Game data is on the microSD card, so uploads don't touch it; the Wi-Fi network survives a normal upload too. "Erase All Flash Before Sketch Upload" forgets the Wi-Fi network (and the old pre-database data).

5. **Once per board: the avatars.** The players' emoji avatars (7 MB) live in their own flash partition, not in the app, so an upload doesn't write them. Until they're there, avatars are blank. Either:
   - copy `extras/avatars.bin` to the microSD card's root (over **USB drive**, or `python3 tools/devctl.py --port /dev/ttyACM0 "put extras/avatars.bin avatars.bin"`) and restart: the board installs it by itself in about a minute ("Avatars installed."); or
   - flash it with esptool at the avatars partition's offset:
     ```
     esptool --chip esp32p4 --port /dev/ttyACM0 --baud 921600 write-flash 0x8F0000 extras/avatars.bin
     ```

   They stay through uploads and firmware updates. Only "Erase All Flash" or a newer avatar set (`tools/gen_avatars.py` changes `AVATAR_CRC`) needs them again. The release files and the web flasher write them already.

## With make

The `Makefile` at the top runs the same commands (arduino-cli from `PATH`, else the one inside the Arduino IDE; override with `ARDUINO_CLI=...`):

| Command | Does |
|---|---|
| `make` | builds into `build/` with the FQBN above, and checks the app fits its slot in `partitions.csv` |
| `make upload PORT=/dev/ttyACM0` | builds, then flashes over the UART port |
| `make avatars PORT=/dev/ttyACM0` | flashes `extras/avatars.bin` into its partition (once per board) |
| `make release VERSION=v1.2.3` | the release files into `dist/` (`tools/package_release.sh`) |
| `make site` | the web flasher, docs and code docs site into `site/` |
| `make clean` | removes `build/`, `dist/` and `site/` |

## The flash layout

`partitions.csv` in the sketch folder replaces the core's table when **Partition Scheme** is **Custom**. Only the first 16 MiB of the 32 MB flash is used, because the esp32 core's prebuilt bootloader and ESP-IDF address flash with 24 bits. Anything above 0x1000000 can't be mapped, and an app there fails its check, which is why a 12.5 MB second slot never worked for updates.

| Partition | Offset | Size | |
|---|---|---|---|
| nvs | 0x9000 | 20 KB | settings kept on the board (the Wi-Fi network) |
| otadata | 0xE000 | 8 KB | which app slot boots |
| app0, app1 | 0x10000, 0x480000 | 4.44 MB each | the firmware, and the slot a firmware update from the card goes into |
| avatars | 0x8F0000 | 7 MB | `extras/avatars.bin`, shared by both app slots |
| coredump | 0xFF0000 | 64 KB | the last crash's core dump |

The app is about 2.6 MB. With the **Custom** scheme the IDE accepts up to 16 MB, more than a slot holds, so `tools/build_extras.sh BUILD_DIR` checks the size; CI runs it on every build. The nvs and otadata offsets are the same as the core's own tables, so switching to this layout over USB keeps the Wi-Fi setting. A board with the older layout must be flashed over USB once: a firmware update can't change the partition table.

## Builds and releases (GitHub Actions)

`.github/workflows/firmware.yml` builds the firmware on every push to `main` and every pull request, with the versions pinned in it (arduino-cli 1.6.0, esp32 core 3.3.12, LVGL 9.6.0, `extras/lv_conf.h`, the FQBN above). The run's summary shows the size and checksums, and the files are kept as the run's artifact for 30 days. It also fails when the docs' lists of variants are out of date (`python3 tools/doc_lists.py --check`; see [Development](development.md#tests)).

Pushing a tag that starts with `v` also publishes a GitHub release with them (a tag with a `-`, like `v1.1.0-rc1`, makes a pre-release):

```
git tag v1.0.0
git push origin v1.0.0
```

| File | What it's for |
|------|---------------|
| `esp32p4_sudoku-VERSION-update.bin` | The app alone. Copy it to the card as `update.bin` and use **Settings → Data → Firmware update** (or flash it at 0x10000). |
| `esp32p4_sudoku-VERSION-flash.zip` | The bootloader, partition table, `boot_app0.bin` and app, with `flash_args`: `esptool --chip esp32p4 -p PORT write-flash @flash_args` in its folder. Keeps the Wi-Fi setting. |
| `esp32p4_sudoku-VERSION-factory.bin` | All of it in one image, written at 0x0: for a blank board. Its gaps cover the settings area in flash (NVS), so a configured board forgets its Wi-Fi network. |
| `esp32p4_sudoku-VERSION-elf.zip` | The ELF file, to decode a crash's backtrace. |
| `SHA256SUMS` | Checksums of the above. |

The release builds are for pre-v3.00 chips (`ChipVariant=prev3`, most 4B boards). `tools/package_release.sh BUILD_DIR OUT_DIR VERSION` makes the same files from a local build.

## Web flasher

A release tag without a `-` also publishes a web flasher on GitHub Pages: **https://edipermadi.github.io/esp32p4_sudoku/**. It installs that release from Chrome or Edge on a desktop computer over Web Serial, with no Arduino IDE or esptool needed. It uses [ESP Web Tools](https://esphome.github.io/esp-web-tools/) 10.4.0, which picks the flasher stub for pre-v3 chips by itself.

- **What it flashes:** the separate parts at their `flash_args` offsets. This keeps the Wi-Fi setting unless *Erase device* is ticked in the installer. Data on the card is never touched.
- **The page:** `web/index.html`. `tools/make_flasher_site.sh BUILD_DIR OUT_DIR VERSION` adds the firmware and `manifest.json`.
- **The documentation:** the same site has `README.md` and `docs/*.md` as web pages under `/docs/` (**https://edipermadi.github.io/esp32p4_sudoku/docs/**), made by `tools/make_docs_site.py OUT_DIR [VERSION]` (needs `pip install markdown-it-py`). Links between the Markdown files become links between the pages, and links to other files go to GitHub. A new page in `docs/` needs a line in its `PAGES` list. The pages match the release they were published with.
- **The code documentation:** the firmware's code, documented with Doxygen (as `make doxygen`), is under `/code/` (**https://edipermadi.github.io/esp32p4_sudoku/code/**), linked from the docs pages' top bar. `tools/make_flasher_site.sh` runs Doxygen when it's installed (`apt install doxygen graphviz`) and leaves the code docs out otherwise; the workflow installs it and sets `REQUIRE_CODE_DOCS=1`, so a release can't go out without them.
- **Trying it locally:** `python3 -m http.server -d OUT_DIR 8000`, then open `http://localhost:8000`. Web Serial needs HTTPS or localhost.

One-time setup on GitHub:

1. **Settings → Pages → Build and deployment → Source: GitHub Actions.**
2. **Settings → Environments → github-pages → Deployment branches and tags:** add a tag rule `v*`. The environment only allows `main` by default, and the flasher deploys from tags.

To publish the flasher for a tag that already exists, use **Actions → Firmware → Run workflow** and pick the tag. Running it this way doesn't touch the release.
