# `puzzle_stats`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

A summary of `solves`: one row per player and puzzle solved, with the best time, fewest mistakes and how often. It exists so the Puzzles grid, Home and Statistics don't scan the log; at start-up the current player's rows are loaded into RAM (the copy the UI reads, `store_stats.cpp`).

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `player_id` | INTEGER | no | → `players.id` |
| `puzzle` | INTEGER | no | the puzzle id |
| `level` | TEXT | no | as in `solves.level` |
| `number` | INTEGER | no | as in `solves.number` |
| `best_seconds` | INTEGER | no | the fastest solve |
| `fewest_mistakes` | INTEGER | yes | the fewest mistakes; NULL: solved before mistakes were counted |
| `solves` | INTEGER | no | times solved |

## Keys and indexes

- Primary key `(player_id, puzzle)`.
- `puzzle_stats_level` on `(player_id, level, number)`: the per-collection rows on Statistics. Recreated at every start.

## Written

In the same transaction as each `solves` insert, as an upsert:

```sql
INSERT INTO puzzle_stats(...) VALUES(..., 1)
ON CONFLICT(player_id, puzzle) DO UPDATE SET
  best_seconds = MIN(best_seconds, excluded.best_seconds),
  fewest_mistakes = COALESCE(MIN(fewest_mistakes, excluded.fewest_mistakes), fewest_mistakes, excluded.fewest_mistakes),
  solves = solves + 1
```

(`MIN()` in SQL is NULL if either side is; the `COALESCE` keeps the known one.) Statistics kept in the board's NVS by old firmware are merged in once, the same way but idempotent (`solves = MAX(...)`).

## Read

At start-up and on a player switch (into RAM), the Puzzles grid's solved marks, the panel's counts, Statistics per level, the `level_stats` view, the solved counts per collection, the **Explorer** and **Completionist** achievements.

## Deleted

Only with its player.

## History

Before players existed it was keyed by `puzzle` alone; the upgrade rebuilds it with `player_id` (the old table is kept as `puzzle_stats_v3` until copied, all in one transaction).

## Example queries

```sh
# the best times of a collection
sqlite3 -box sudoku.db "SELECT number, best_seconds, fewest_mistakes, solves FROM puzzle_stats WHERE player_id = 1 AND level = 'Gnome 2 Hard' ORDER BY number"
```
