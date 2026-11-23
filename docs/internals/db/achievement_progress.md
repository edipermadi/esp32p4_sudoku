# `achievement_progress`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

A cache: each player's progress toward every achievement, so the Trophies screen reads one table instead of running every achievement's query (about 40 of them). It can be deleted at any time: it is rebuilt.

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `player_id` | INTEGER | no | → `players.id` |
| `key` | TEXT | no | as in `achievements.key` |
| `value` | INTEGER | no | the progress, in the achievement's unit: a count, days, or minutes (**Marathon**) |

## Keys

Primary key `(player_id, key)`.

## Written

Whenever achievements are checked (`sdb_ach_sync()`): every value worked out again and written with `INSERT OR REPLACE` in a savepoint. That is at start-up for every player, after a solve, a first Learn practice and a time attack run.

## Read

Trophies > Achievements. If a player's rows are missing or incomplete (a new player, a firmware with new achievements), the screen works them out and fills them in first.

## Deleted

With its player. Deleting rows by hand is safe.

## History

New in the firmware that added it: created with `CREATE TABLE IF NOT EXISTS`, filled at the next start.

## Example queries

```sh
sqlite3 -box sudoku.db "SELECT key, value FROM achievement_progress WHERE player_id = 1 ORDER BY key"
```
