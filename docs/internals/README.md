# Internals

[← README](../../README.md) · [Features](../features.md) · [Data & storage](../data.md) · [Building](../building.md) · [Development](../development.md) · [Remote control](../devctl.md)

How the game works inside, for anyone changing the firmware or reading its data. For building, testing and the source layout, see [Development](../development.md).

| Page | What it covers |
|---|---|
| [Puzzle generator](generator.md) | How puzzles are made, graded Easy to Expert, solved and hinted: the board struct, the solver, digging, the daily seed, running on the board, adding a variant |
| [Database design](database.md) | `sudoku.db`: the tables, the puzzle id that links them, a diagram, how data is written, deleted and upgraded |
