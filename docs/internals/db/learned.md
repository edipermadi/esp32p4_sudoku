# `learned`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

Learn practices finished: the player placed the digit the technique leads to.

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `player_id` | INTEGER | no | → `players.id` |
| `technique` | INTEGER | no | Learn's technique lessons: 0 hidden single, 1 naked single, 2 pointing, 3 claiming, 4 naked pair, 5 hidden pair, 6 naked triple, 7 X-Wing, 8 Swordfish, 9 XY-Wing |
| `at` | INTEGER | yes | the first time; Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |

## Keys

Primary key `(player_id, technique)`: the first time counts.

## Written

`INSERT OR IGNORE` when a practice is finished (`sdb_do_learned()` in `src/stats_db_small.c`); a new row checks achievements (**Graduate**).

## Read

The **Graduate** achievement (`COUNT(DISTINCT technique) ... technique < 10`).

## Deleted

Only with its player.
