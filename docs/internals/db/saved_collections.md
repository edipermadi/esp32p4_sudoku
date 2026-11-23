# `saved_collections`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

Collections made on the board with the game screen's ⋮ > Save (and `Received`, for puzzles that came by sound). Their puzzles are in [`saved_puzzles`](saved_puzzles.md). Shared by all players.

**Group:** puzzles and collections

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `id` | INTEGER | no | primary key |
| `name` | TEXT | no | unique, typed in the Save dialog |
| `created_at` | INTEGER | yes | when; saving waits for the clock |

## Keys and limits

Primary key `id`; `name` is `UNIQUE`. At most `STATS_MAX_SAVED` (16) collections.

## Written

Created with the first puzzle put in it (`sdb_do_saved_add()` in `src/stats_db_saved.c`).

## Read

The Puzzles screen's Saved tab, the Save dialog's list, Export collection (an `.sdx` file in `export/`).

## Deleted

When its last puzzle is taken out, or Puzzles > ⋮ > Delete collection: its `saved_puzzles` first, then this row, in one transaction.
