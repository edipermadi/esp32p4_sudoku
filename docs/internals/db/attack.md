# `attack`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

Time attack runs: as many Easy puzzles as possible in 10 minutes. A run is many puzzles, so it has its own table; each puzzle solved in it is also a row in `solves`.

**Group:** per player

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `id` | INTEGER | no | primary key |
| `player_id` | INTEGER | no | → `players.id` |
| `finished_at` | INTEGER | yes | Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |
| `solved` | INTEGER | no | puzzles solved in the run |
| `mistakes` | INTEGER | yes | mistakes over the run |

## Written

One `INSERT` when the 10 minutes are up (`src/stats_db_small.c`); a run stopped early isn't recorded. Then achievements are checked (**Against the clock**, **Cool under pressure**).

## Read

The time attack leaderboard (each player's best run and how many runs, `STATS_ATTACK_ROWS` rows), the achievements.

## Deleted

Only with its player.

## Example queries

```sh
# the leaderboard
sqlite3 -box sudoku.db "SELECT p.name, MAX(a.solved) AS best, COUNT(*) AS runs FROM attack a JOIN players p ON p.id = a.player_id GROUP BY a.player_id ORDER BY best DESC"
```
