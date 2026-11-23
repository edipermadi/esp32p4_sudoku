# `saved_game`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

Games in progress: up to `STATS_MAX_GAMES` (5) per player, one per puzzle. Everything needed to carry on: the digits, pencil marks, cell colours, the clock, mistakes, Fill uses and the move log so far.

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `player_id` | INTEGER | no | → `players.id` |
| `puzzle` | INTEGER | no | the puzzle id |
| `level` | TEXT | no | as in `solves.level` |
| `number` | INTEGER | no | as in `solves.number` |
| `cells` | TEXT | no | 81 digits row by row, 0 = empty, givens included |
| `notes` | BLOB | no | pencil marks: 81 × uint16 little-endian (162 bytes), bit *d* = mark *d* |
| `elapsed_ms` | INTEGER | no | the clock, pauses left out |
| `mistakes` | INTEGER | no | so far |
| `saved_at` | INTEGER | yes | Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |
| `started_at` | INTEGER | yes | when it was opened or restarted |
| `hints` | INTEGER | no | Fill uses so far |
| `moves` | BLOB | yes | the move log so far, the [same format as `solves.moves`](solves.md#the-move-log) |
| `seq` | INTEGER | no | higher = played more recently: each save takes the player's highest `seq` + 1 |
| `colors` | BLOB | yes | 81 bytes, a cell colour each (0 = none, 1–`CELL_COLORS`); NULL when no cell is coloured |
| `flags` | INTEGER | no | `GAME_FLAG_*`, as in `solves.flags`: carried into the solve |

## Keys and limits

- Primary key `(player_id, puzzle)`: one game per puzzle and player.
- After each save, games beyond the 5 newest (by `seq`) are deleted, with a notice.

## Written

After every move, as `INSERT OR REPLACE` (`sdb_save_game()` in `src/stats_db_games.c`). Saves are queued; only the newest one waiting is written, so a burst of taps is one write. The game in progress also lives in RAM (`store_game.cpp`), so nothing is lost without a card until power-off.

## Read

Home's **Continue**, the Unfinished games dialog (newest first), opening a puzzle (it resumes if there is a row), a player switch (their newest game).

## Deleted

When the game is solved, discarded from the Unfinished games dialog or restarted (it starts again from the givens, and its next move saves a new row); with its player.

## History

`started_at`, `hints`, `moves`, `seq`, `colors` and `flags` were added later. It once held one game per player: the upgrade to five renames the old table `saved_game_v8`, copies the rows (each gets `seq` 1) and drops it. Before players existed it had no `player_id` (`saved_game_v3`).

## Example queries

```sh
# games in progress, the latest first
sqlite3 -box sudoku.db "SELECT p.name, sg.level, sg.number, sg.elapsed_ms / 1000 AS seconds, sg.mistakes FROM saved_game sg JOIN players p ON p.id = sg.player_id ORDER BY sg.player_id, sg.seq DESC"
```
