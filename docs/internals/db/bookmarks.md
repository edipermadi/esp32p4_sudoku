# `bookmarks`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

Starred puzzles (the game screen's ⋮ > Star, or the Puzzles screen's Starred list).

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `player_id` | INTEGER | no | → `players.id` |
| `puzzle` | INTEGER | no | the puzzle id |
| `at` | INTEGER | yes | when it was starred; Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |

## Keys and limits

Primary key `(player_id, puzzle)`. The Starred list shows the newest `STATS_MAX_BOOKMARKS` (64).

## Written

`INSERT OR IGNORE` to star, `DELETE` to unstar (`src/stats_db_small.c`). A puzzle kept only in RAM (no card) can't be starred.

## Read

The game screen (the star after the title), Puzzles > Starred (`ORDER BY at DESC`).

## Deleted

Unstarred, or with its player.

## Example queries

```sh
# starred puzzles that were never solved
sqlite3 -box sudoku.db "SELECT b.player_id, b.puzzle FROM bookmarks b LEFT JOIN puzzle_stats ps ON ps.player_id = b.player_id AND ps.puzzle = b.puzzle WHERE ps.puzzle IS NULL"
```
