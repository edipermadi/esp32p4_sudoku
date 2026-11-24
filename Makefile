# Building the firmware, and formatting the project's own C code.
#
#   make                the firmware, into build/ (as CI: arduino-cli, the FQBN below), and
#                       checks the app fits its slot in partitions.csv
#   make upload         builds, then flashes it over the UART port (PORT=/dev/ttyACM0)
#   make avatars        flashes extras/avatars.bin into its partition (once per board)
#   make release        the release files into dist/ (tools/package_release.sh; VERSION=...)
#   make site           the web flasher, docs and code docs site into site/ (tools/make_flasher_site.sh)
#   make doxygen        the code's documentation (Doxyfile) into build/doxygen/html/index.html;
#                       its warnings in build/doxygen/warnings.log
#   make diagrams       the docs' flowcharts: docs/diagrams/*.dot rendered to .svg (Graphviz's dot)
#   make clean          removes build/, dist/ and site/
#
#   make format         formats the C / C++ sources, headers and the sketch in place with astyle (.astylerc)
#   make format-check   lists the files that need it, and fails if any do (changes nothing)
#   make test           the PC tests (tests/Makefile), and checks the docs' lists of variants are up to date
#   make doc-lists      rewrites the docs' lists of variants from the code (tools/doc_lists.py)
#
# arduino-cli: from PATH, else the one inside the Arduino IDE (/opt/arduino-ide); or
# ARDUINO_CLI=... Needs the esp32 core 3.3.x and LVGL 9.6.0 set up as docs/building.md says.
#
# Left out: vendored third-party code (SQLite, QR Code generator, ggwave (src/ggwave/), LVGL's lv_conf.h
# template) and generated files (puzzles, avatars, fonts: their generators would overwrite
# any formatting). sqlite3_esp.c, the project's own SQLite glue, is in.
#
# astyle 3.1 leaves a loop or condition without braces when its body is itself an if / for /
# while; tools/brace_nested.py braces those between two astyle passes.

ASTYLE  ?= astyle
PYTHON  ?= python3

ARDUINO_CLI ?= $(or $(shell command -v arduino-cli 2>/dev/null),\
                    $(wildcard /opt/arduino-ide/resources/app/lib/backend/resources/arduino-cli))
FQBN    ?= esp32:esp32:esp32p4:PSRAM=enabled,FlashSize=32M,PartitionScheme=custom,ChipVariant=prev3
BUILD   ?= build
PORT    ?= /dev/ttyACM0
BAUD    ?= 921600
VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
ESPTOOL ?= $(lastword $(sort $(wildcard $(HOME)/.arduino15/packages/esp32/tools/esptool_py/*/esptool)))
# where the avatars go: their partition's offset in partitions.csv
AVATARS_AT = $(shell awk -F, '$$1 ~ /^avatars/ {gsub(/ /, "", $$4); print $$4}' partitions.csv)

SOURCES := $(filter-out \
	src/avatars.c src/avatars.h src/puzzles_data.c src/puzzles_count.h src/puzzles_canon.c, \
	$(wildcard src/*.c src/*.cpp src/*.h src/bsp/*.c src/bsp/*.cpp src/bsp/*.h)) \
	src/sqlite/sqlite3_esp.c esp32p4_sudoku.ino \
	$(wildcard tests/*.c tools/*.c)

ASTYLE_RUN = $(ASTYLE) --options=.astylerc --quiet

.PHONY: all build upload avatars release site doxygen diagrams clean format format-check test doc-lists

all: build

build:
	@[ -n "$(ARDUINO_CLI)" ] || { echo "no arduino-cli (set ARDUINO_CLI)"; exit 1; }
	$(ARDUINO_CLI) compile --fqbn "$(FQBN)" --build-path $(BUILD) .
	@tools/build_extras.sh $(BUILD)

upload: build
	@$(PYTHON) tools/devctl.py --port $(PORT) --stop >/dev/null 2>&1 || true   # (a devctl session holds the port)
	$(ARDUINO_CLI) upload --fqbn "$(FQBN)" --input-dir $(BUILD) -p $(PORT) .

avatars:
	@[ -x "$(ESPTOOL)" ] || { echo "no esptool (set ESPTOOL)"; exit 1; }
	$(ESPTOOL) --chip esp32p4 --port $(PORT) --baud $(BAUD) write-flash $(AVATARS_AT) extras/avatars.bin

release: build
	ESPTOOL="$(ESPTOOL)" tools/package_release.sh $(BUILD) dist $(VERSION)

site: build
	tools/make_flasher_site.sh $(BUILD) site $(VERSION)

DIAGRAMS := $(patsubst %.dot,%.svg,$(wildcard docs/diagrams/*.dot))

diagrams: $(DIAGRAMS)

docs/diagrams/%.svg: docs/diagrams/%.dot
	@command -v dot >/dev/null || { echo "no dot (apt install graphviz)"; exit 1; }
	dot -Tsvg $< -o $@

doxygen:
	@command -v doxygen >/dev/null || { echo "no doxygen (apt install doxygen graphviz)"; exit 1; }
	@mkdir -p build/doxygen
	VERSION="$(VERSION)" doxygen Doxyfile
	@n=$$(grep -c "warning" build/doxygen/warnings.log 2>/dev/null || true); \
	echo "build/doxygen/html/index.html ($${n:-0} warnings: build/doxygen/warnings.log)"

clean:
	rm -rf $(BUILD) dist site

format:
	@$(ASTYLE_RUN) $(SOURCES)
	@$(PYTHON) tools/brace_nested.py $(SOURCES)
	@$(ASTYLE_RUN) $(SOURCES)

format-check:
	@tmp=$$(mktemp -d) && trap 'rm -rf "$$tmp"' EXIT && \
	for f in $(SOURCES); do mkdir -p "$$tmp/$$(dirname $$f)" && cp "$$f" "$$tmp/$$f"; done && \
	(cd "$$tmp" && $(ASTYLE) --options="$(CURDIR)/.astylerc" --quiet $(SOURCES) >/dev/null && \
	 $(PYTHON) "$(CURDIR)/tools/brace_nested.py" $(SOURCES) >/dev/null && \
	 $(ASTYLE) --options="$(CURDIR)/.astylerc" --quiet $(SOURCES) >/dev/null) && \
	bad=0; for f in $(SOURCES); do \
		cmp -s "$$f" "$$tmp/$$f" || { echo "needs formatting: $$f"; bad=1; }; \
	done; \
	[ $$bad = 0 ] || { echo "run: make format"; exit 1; }

test:
	@$(MAKE) -C tests
	@$(PYTHON) tools/doc_lists.py --check

doc-lists:
	@$(PYTHON) tools/doc_lists.py
