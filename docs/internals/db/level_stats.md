# `level_stats` (view)

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

A view for anyone reading the database with `sqlite3`: per player and level (collection, or kind and level), from `puzzle_stats`. The firmware doesn't read it.

## Columns

| Column | What it holds |
|---|---|
| `player` | the player's name |
| `level` | the level |
| `solved` | puzzles solved |
| `solves` | solves in all |
| `fastest_seconds` | the fastest best time |
| `average_seconds` | the average best time |
| `average_mistakes` | the average fewest mistakes |
| `without_mistakes` | puzzles solved without a mistake |

## Written

`SCHEMA_POST` drops and creates it at every start, so it always matches the current build.

## Example queries

```sh
sqlite3 -box sudoku.db "SELECT * FROM level_stats"
```
