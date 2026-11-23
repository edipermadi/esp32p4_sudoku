# `solves`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

The log: one row per puzzle finished, never changed afterwards. Everything historic is worked out from here: the charts, streaks, records, the daily calendar and leaderboards, History, Gameplays and Replay, and most achievements.

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `id` | INTEGER | no | primary key, in the order solved; Replay and Gameplays refer to a solve by it |
| `player_id` | INTEGER | no | → `players.id` |
| `puzzle` | INTEGER | no | the [puzzle id](../database.md#the-puzzle-id-what-ties-everything-together): 0–1599 built in, 2000 + `generated.id` otherwise |
| `level` | TEXT | no | the collection's name (`Gnome 2 Hard`), or kind and level for a generated one (`Generated Easy`, `Daily Hard`, `Skyscraper Expert`), or an imported collection's name |
| `number` | INTEGER | no | its place: 1–100 in a built-in collection, `generated.id` or the place in an imported collection |
| `seconds` | INTEGER | no | time to solve, pauses left out |
| `mistakes` | INTEGER | no | digits placed that broke a rule (undo doesn't remove them) |
| `finished_at` | INTEGER | yes | Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet); was `solved_at` in old databases |
| `started_at` | INTEGER | yes | when it was opened or restarted; NULL: not known |
| `hints` | INTEGER | no | cells filled with Fill (the solution's digit); 0 by default |
| `moves` | BLOB | yes | the move log: 6 bytes per move (below); NULL: not recorded (older solves) |
| `flags` | INTEGER | no | `GAME_FLAG_*` (`src/store.h`): bit *k* (0–15) = a hint showed technique *k*; bit 20 = the no-notes challenge; 0 by default |

## The move log

Every change of a cell's digit, in order (`src/moves.h`). Each move is 6 bytes, little-endian:

| Bytes | What |
|---|---|
| 0–3 | `t_ms`: the game clock at that moment, ms (uint32) |
| 4 | `cell`: 0–80, row by row |
| 5 | low 4 bits: the digit (0 = cleared); high 4 bits: what made it: 0 the pad, 1 Fill, 2 Erase, 3 Undo, 4 Redo |

Pencil marks aren't logged. Replaying the log over the givens rebuilds the board at any moment (Replay, MP4 export, Mistake insights). A log stops growing at `GAME_MOVES_MAX` (2048) moves.

## The flags

| Bit | Meaning |
|---|---|
| 1–15 | a hint showed that technique (`TECH_*` in `src/generator.h`: 1 pointing, 2 claiming, 3 naked pair, 4 hidden pair, ... 10 X-Wing, 11 Swordfish, 12 XY-Wing ...) |
| 20 | played in the no-notes challenge |

The achievements read them: **Gone fishing** is `flags & 3072`, **On a wing** `flags & 4096`, **Bare hands** `flags & 1048576`.

## Keys and indexes

- Primary key `id`.
- `solves_puzzle` on `(puzzle)`: the variant and daily filters, records.
- `solves_player` on `(player_id, puzzle)`: a player's history, charts, achievements.

## Written

Once, when a puzzle is solved (`sdb_do_solve()` in `src/stats_db_solves.c`), in **one transaction** with the `puzzle_stats` upsert, so the two never disagree. The game in progress's moves and flags come along; its `saved_game` row is deleted after. Time attack runs log each puzzle here too. Learn's practice games are never logged.

## Read

Statistics (the chart, per level), Mistake insights (the last 300 move logs), History (a day's games), Gameplays and Replay, the Daily and Weekly leaderboards, head-to-head, Records, achievements, the CSV export (`export/solves.csv`).

## Deleted

Only with its player. A solve is never edited.

## History

`started_at`, `hints`, `moves` and `flags` were added later (`ALTER TABLE ... ADD COLUMN`; defaults 0 or NULL keep old rows valid), and `solved_at` was renamed `finished_at`. Before players existed it had no `player_id`: the upgrade copies it into a new table, every row player 1's.

## Example queries

```sh
# the last 10 solves
sqlite3 -box sudoku.db "SELECT id, level, number, seconds, mistakes, hints, datetime(finished_at, 'unixepoch', 'localtime') AS finished FROM solves ORDER BY id DESC LIMIT 10"
# solves where a hint showed an X-Wing or a Swordfish
sqlite3 -box sudoku.db "SELECT id, level, number FROM solves WHERE flags & 3072"
# moves per solve
sqlite3 -box sudoku.db "SELECT id, length(moves) / 6 AS moves FROM solves WHERE moves IS NOT NULL ORDER BY id DESC LIMIT 10"
```
