# Internals

[← README](../../README.md) · [Features](../features.md) · [Data & storage](../data.md) · [Building](../building.md) · [Development](../development.md) · [Remote control](../devctl.md)

How the game works inside, for anyone changing the firmware or reading its data. Flowcharts (Graphviz, `docs/diagrams/`) show the processes with steps and decisions. For building, testing and the source layout, see [Development](../development.md).

| Page | What it covers |
|---|---|
| [Puzzle generator](generator.md) | How puzzles are made, graded Easy to Expert, solved and hinted: the board struct, the solver, digging, the daily seed, running on the board, hints and Learn, adding a variant |
| [Database design](database.md) | `sudoku.db`: the tables, the puzzle id that links them, a diagram, how data is written, deleted and upgraded |
| [Database tables](db/README.md) | A page per table: its columns, keys, when it's written and read, how rows go, its history, example queries |
| [Game flow](game-flow.md) | Flowcharts of opening a puzzle, entering a digit, solving, and the daily puzzle: every decision the game screen makes |
| [Tasks and cores](tasks.md) | Every FreeRTOS task (core, priority, stack, who starts it), how results get back to the screen, and the rules: LVGL on one task, long work on core 0 with yields, nothing slow on core 1 |
| [Start-up and power](startup-power.md) | Power-on to Home step by step (and why the card comes before Wi-Fi), the screen saver and power saving, turning off, lending the card over USB, a firmware update's probation |
| [Display and the flash](display.md) | From LVGL to the panel (PPA, PSRAM buffers, the DPI frame buffer), what makes the panel blink, the dark screen during flash writes, the 16 MB mapping limit |
| [Replay and video](replay-video.md) | The move log, the Replay screen, MP4 export through the PPA and the H.264 encoder, and the viewer's MP4 and JPEG decoding |
| [Sound](sound.md) | The sound-effects synthesizer, Gibberlink's commands by sound, and Send by sound's message format |
| [UI structure](ui-structure.md) | How the screens are organised: ui and core files, accessors instead of globals, building and rebuilding every screen, layers, timers, adding a screen |
| [Memory](memory.md) | Internal RAM vs PSRAM, who needs internal RAM, `malloc()` to PSRAM from 4 bytes, the big buffers, rules of thumb |
