# `generated`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

Every puzzle that isn't built in, one row each: made with New puzzle, a daily puzzle, typed in, received by sound or imported. Its puzzle id is 2000 + `id`. Shared by all players.

**Group:** puzzles and collections

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `id` | INTEGER | no | primary key; the puzzle id is 2000 + `id` |
| `level` | INTEGER | no | 0 Easy, 1 Medium, 2 Hard, 3 Expert (as graded when made) |
| `givens` | TEXT | no | 81 digits row by row, 0 = empty (a 6×6 or 4×4 puzzle in the top-left corner) |
| `created_at` | INTEGER | yes | Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |
| `day` | TEXT | yes | a daily puzzle: its date `YYYY-MM-DD`; else NULL |
| `variant` | INTEGER | no | the rules: 0 Classic … 72 Mini 8x8 (`VAR_*` in `src/generator.h`, only ever added to at the end); 0 by default |
| `regions` | TEXT | yes | Jigsaw: 81 digits 0–8, the region of each cell |
| `cages` | TEXT | yes | Killer cages (`sum:cell,cell,...;...`), Quadruple circles, Little Killer diagonals |
| `marks` | TEXT | yes | Even-Odd, Consecutive, Kropki, XV, Greater-Than, Fortress, Diagonal Consecutive (81 characters); Sandwich, Skyscraper, X-Sums, Frame, Numbered Rooms, Sum Skyscrapers clues (18 numbers); Outside clues (18 digit sets); Rossini arrows |
| `thermos` | TEXT | yes | Thermo and Slow Thermo (bulb first), and the line variants' lines (Arrow: circle first; Nabner too), `cell,cell,...;...` |
| `source` | TEXT | yes | the collection it belongs to: an imported file's name, `My puzzles` (typed in), `Received` (by sound); NULL for New puzzle and the daily |
| `number` | INTEGER | yes | its place in `source`, from 1 |

## Kinds of row

| Made by | `day` | `source` |
|---|---|---|
| New puzzle | NULL | NULL |
| Daily puzzle | its date | NULL |
| Enter a puzzle | NULL | `My puzzles` |
| Received by sound | NULL | `Received` |
| Imported from `puzzles/` | NULL | the collection |

The layout columns are the same four fields as a `.sdx` line (`src/sdx.c`), so importing, exporting and sharing use one format. A variant uses only the columns it needs; the others are NULL. The text formats are read and written in `src/gen_texts.c`; `puzzle_geom()` (`src/puzzles_lookup.c`) rebuilds a board from `variant` and these columns.

## Written

- **New puzzle, daily, typed in, received**: one `INSERT` (`sdb_do_gen_add()` in `src/stats_db_generated.c`); `number` is the source's highest + 1. The daily puzzle is made the first time it's opened that day; later opens find it by `day` (`SELECT id FROM generated WHERE day = ? ORDER BY id LIMIT 1`).
- **Imported**: many rows in one transaction per file, each checked for exactly one solution first, then the `imported` row.
- Without a card, generated puzzles live only in RAM (ids from `0xF000`) and are gone at power-off.

## Read

Opening any puzzle with id ≥ 2000 (the last 24 are cached in RAM), the Daily button and leaderboard (by `day`), records and achievements by variant (`puzzle IN (SELECT 2000 + id FROM generated WHERE variant = ?)`), imported and saved collections.

## Deleted

Never. A solve, a star or a saved collection can point at any row for good; deleting an imported collection keeps its puzzles here.

## History

`day`, `variant`, `regions`, `cages`, `marks`, `thermos`, `source` and `number` were added one by one as the features came (`ALTER TABLE ... ADD COLUMN`).

## Example queries

```sh
# a player's solves per variant
sqlite3 -box sudoku.db "SELECT g.variant, COUNT(*) AS solves, MIN(s.seconds) AS best FROM solves s JOIN generated g ON s.puzzle = 2000 + g.id WHERE s.player_id = 1 GROUP BY g.variant"
# the daily puzzles
sqlite3 -box sudoku.db "SELECT day, variant, level, 2000 + id AS puzzle FROM generated WHERE day IS NOT NULL ORDER BY day DESC LIMIT 14"
```
