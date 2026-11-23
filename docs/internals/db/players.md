# `players`

[← Database tables](README.md) · [Database design](../database.md) · [Data & storage](../../data.md) · [Internals](../README.md)

One row per player. Every per-player table points here with `player_id`. The player playing now is not stored here: it is `settings.current_player`.

**Group:** people

## Columns

| Column | Type | Null | What it holds |
|---|---|---|---|
| `id` | INTEGER | no | primary key; the first player is 1 |
| `name` | TEXT | no | unique, case-insensitive (`COLLATE NOCASE`); at most 23 characters, spaces trimmed at both ends |
| `created_at` | INTEGER | yes | Unix time, UTC; NULL when the clock wasn't set (no Wi-Fi yet) |
| `color` | INTEGER | yes | the player's colour, 0–7 (`STATS_PLAYER_COLORS`); NULL: picked from the id |
| `avatar` | INTEGER | yes | an emoji, 1–`AVATAR_COUNT` (`src/avatars.h`); NULL: none, the colour dot shows instead |

## Keys and limits

- Primary key `id`; `name` is `UNIQUE`.
- At most `STATS_MAX_PLAYERS` (16) players.

## Written

- **Add** (Players > ⋮ > Add, or the first start): `INSERT`; the new player starts playing (`sdb_do_add()` in `src/stats_db_players.c`). A name already taken, in any case, is refused.
- **Rename**: `UPDATE ... SET name`, the same checks.
- **Colour** (tap the dot) and **Avatar** (Players > ⋮ > Avatar): `UPDATE` of one column.
- **A database from before players existed** gets "Player 1" with id 1, and every old row becomes theirs (see [Upgrades](../database.md#upgrades)).

## Read

The Players screen, the Statistics player dropdown, every leaderboard (Daily, Weekly, Time attack), head-to-head on the Solved dialog, History's day list, Gameplays and Replay (the name and avatar shown), the CSV export.

## Deleted

Players > ⋮ > Delete: in one transaction, the player's rows in `solves`, `puzzle_stats`, `saved_game`, `achievements`, `achievement_progress`, `learned`, `bookmarks`, `seen` and `attack` go first, then this row (`sdb_do_delete()`). The only player can't be deleted.

## History

`color` and `avatar` were added later with `ALTER TABLE ... ADD COLUMN`; older rows have NULL in both.

## Example queries

```sh
sqlite3 -box sudoku.db "SELECT id, name, color, avatar, datetime(created_at, 'unixepoch', 'localtime') AS created FROM players"
# who is playing now
sqlite3 sudoku.db "SELECT p.name FROM players p JOIN settings s ON s.key = 'current_player' AND p.id = s.value"
```
