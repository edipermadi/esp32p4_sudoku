# `settings`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

The board's settings, as key-value pairs: one set for the board, not per player. The Wi-Fi network isn't here; it stays in the board's NVS.

**Group:** device

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `key` | TEXT | no | the setting's name (below) |
| `value` | (any) | yes | its value: a number, or text for `lock_pin`, `time_zone` and `last_level` |

## Keys

| Key | Value |
|---|---|
| `dark_mode`, `sound`, `show_timer`, `cvd`, `no_notes`, `left_hand`, `gibberlink`, `big_digits`, `hint_nudge`, `night_mode` | 0 or 1 |
| `brightness`, `volume`, `night_brightness` | percent (brightness 10–100) |
| `screen_off_minutes` | 0 = never |
| `assist` | Mistake check: 0 Off, 1 Rules, 2 Solution |
| `saver_minutes` | Screen saver for: minutes before the saver goes dark, 0 = until a touch (10, 20, 30) |
| `saver_clock` | Screen saver: 0 Dark, 1 Digital clock, 2 Life, 3 Analog clock, 4 Rain, 5 Attractor, 6 Boids, 7 Lissajous, 8 Pendulum, 9 Langton's ant, 10 Brian's Brain, 11 Cyclic spirals, 12 Rock paper scissors, 13 Excitable medium, 14 Day & Night, 15 Seeds, 16 Maze, 17 Star Wars, 18 Forest fire, 19 3D Crystal growth, 20 3D Diamond growth, 21 3D Pyroclastic, 22 3D Builder, 23 Starfield (named from when it was the clock or not) |
| `accent` | 0 Blue … 4 Pink |
| `rules_seen` | a bit per variant: its rules card was shown |
| `lock_mode`, `lock_pin` | the lock screen: 0 Off, 1 Swipe, 2 PIN; the PIN |
| `night_start`, `night_end` | hours 0–23 |
| `time_zone` | an IANA name, e.g. `Europe/Berlin` |
| `last_level` | the collection last shown (its name) |
| `current_player` | `players.id` of who is playing |
| `migrated_from_nvs` | 1 once old firmware's NVS settings were copied in |

## Written

Each changed setting is written on its own (`sdb_set_setting()` in `src/stats_db_prefs.c`); `current_player` on a player switch. A new database gets the board's current settings.

## Read

All of them at start-up and when the card comes back (into `store_prefs_t`, `store.cpp`); the UI reads that RAM copy.

## Deleted

Never one by one; factory reset deletes the whole file.

## Example queries

```sh
sqlite3 -box sudoku.db "SELECT key, value FROM settings ORDER BY key"
```
