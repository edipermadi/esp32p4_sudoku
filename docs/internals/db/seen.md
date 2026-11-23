# `seen`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

Things shown once per player, so they don't show again.

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `player_id` | INTEGER | no | → `players.id` |
| `what` | TEXT | no | what was shown: `tour` (the game screen's tour) |
| `at` | INTEGER | yes | Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |

## Keys

Primary key `(player_id, what)`.

## Written

`INSERT OR IGNORE` once shown; `DELETE` to show it again (Settings > Game > Game screen tour, **Show again**).

## Read

Opening a game: the tour shows if there is no `tour` row.

## Deleted

As above, or with its player.
