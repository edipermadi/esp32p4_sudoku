# `achievements`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

One row per achievement a player has unlocked. The achievements themselves (their goals and the SQL that measures them) are in the firmware, the `ACH` list in `src/stats_db_achievements.c`; every one is described on [Achievements](../../achievements.md).

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `player_id` | INTEGER | no | → `players.id` |
| `key` | TEXT | no | the achievement's key, e.g. `clean_25`, `streak_7` (never renamed) |
| `unlocked_at` | INTEGER | yes | when; NULL when it was found already done (a new firmware, a restored database) or the clock wasn't set |

## Keys

Primary key `(player_id, key)`. A row is never updated: once unlocked, always unlocked.

## Written

`INSERT OR IGNORE` by `sdb_ach_sync()`: after a solve, a Learn practice finished for the first time and a time attack run (with the time, and the unlock toast); at start-up for every player (quietly, no time).

## Read

Trophies > Achievements, Home's Trophies line, the CSV export (`export/achievements.csv`).

## Deleted

Only with its player.

## Example queries

```sh
sqlite3 -box sudoku.db "SELECT p.name, a.key, datetime(a.unlocked_at, 'unixepoch', 'localtime') AS unlocked FROM achievements a JOIN players p ON p.id = a.player_id ORDER BY a.unlocked_at"
```
