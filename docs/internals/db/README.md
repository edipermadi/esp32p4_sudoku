# Database tables

[← Database design](../database.md) · [Internals](../README.md) · [Data & storage](../../data.md) · [README](../../../README.md)

A page per table of `sudoku.db`: its columns, keys and limits, when the firmware writes it, what reads it, how its rows go, how it changed over the versions, and queries to try with `sqlite3`. How the tables fit together (the puzzle id, the diagram, transactions, upgrades) is on [Database design](../database.md).

| Table | Group | What it holds |
|---|---|---|
| [`players`](players.md) | people | the players: name, colour, avatar |
| [`solves`](solves.md) | per player | the log: one row per puzzle solved, with its move log |
| [`puzzle_stats`](puzzle_stats.md) | per player | best time, fewest mistakes and solves per puzzle |
| [`saved_game`](saved_game.md) | per player | games in progress, up to 5 |
| [`bookmarks`](bookmarks.md) | per player | starred puzzles |
| [`achievements`](achievements.md) | per player | achievements unlocked |
| [`achievement_progress`](achievement_progress.md) | per player | progress toward each achievement (a cache) |
| [`learned`](learned.md) | per player | Learn practices finished |
| [`seen`](seen.md) | per player | things shown once (the tour) |
| [`attack`](attack.md) | per player | time attack runs |
| [`generated`](generated.md) | puzzles | every puzzle that isn't built in |
| [`imported`](imported.md) | puzzles | collections imported from the card |
| [`saved_collections`](saved_collections.md) | puzzles | collections made on the board |
| [`saved_puzzles`](saved_puzzles.md) | puzzles | their puzzles |
| [`settings`](settings.md) | device | the board's settings |
| [`level_stats`](level_stats.md) | view | per player and level, for `sqlite3` users |

Times are Unix time in UTC, NULL when the clock wasn't set. Links between tables (`player_id`, `puzzle`) aren't enforced by SQLite: the firmware keeps them consistent (see [Deleting](../database.md#deleting)).
