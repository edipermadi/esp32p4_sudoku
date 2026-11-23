# `saved_puzzles`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

The puzzles of each [saved collection](saved_collections.md), in order.

**Group:** puzzles and collections

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `collection_id` | INTEGER | no | → `saved_collections.id` |
| `puzzle` | INTEGER | no | the puzzle id (usually 2000 + `generated.id`) |
| `number` | INTEGER | no | its place in the collection, from 1 |
| `added_at` | INTEGER | yes | Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |

## Keys

Primary key `(collection_id, puzzle)`: a puzzle is in a collection once.

## Written

`INSERT` with `number` = the collection's highest + 1; `DELETE` to take one out (the others keep their numbers).

## Read

The Saved tab's grid, the game screen (is the puzzle saved, for the ⋮ menu), the solved counts per collection, the export.

## Deleted

Taken out, or with its collection.

## Example queries

```sh
sqlite3 -box sudoku.db "SELECT c.name, sp.number, sp.puzzle FROM saved_puzzles sp JOIN saved_collections c ON c.id = sp.collection_id ORDER BY c.id, sp.number"
```
