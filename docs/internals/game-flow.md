# Game flow

[← Internals](README.md) · [README](../../README.md) · [Features](../features.md) · [Data & storage](../data.md) · [Building](../building.md) · [Development](../development.md) · [Remote control](../devctl.md)

What happens when a puzzle is opened, a digit is entered, the game is saved and the puzzle is solved, a player switches, a time attack runs and the daily puzzle is opened: the decisions the game screen makes, in the order it makes them. The code is in `src/ui_continue_open.c`, `src/ui_game.c` and `src/ui_game_win.c`; the state they share (the board, the clock, the move log) is described in [UI structure](ui-structure.md).

The flowcharts' sources are in `docs/diagrams/` (`make diagrams` renders them).

## Opening a puzzle

Every way into a game (Puzzles, Continue, Next, the daily puzzle, a puzzle received by sound) ends in `open_puzzle(id)`. With a card, `sudoku.db` keeps up to 5 unfinished games per player, so opening another puzzle never loses one; without a card there is a single slot in RAM, and the "Unfinished game" dialog asks first.

![Opening a puzzle](../diagrams/open_puzzle.svg)

## Entering a digit

A tap on the pad (or on a cell, with a digit locked by a long press) is `enter_digit()`. Every change keeps an undo step and is logged in the move log (pencil marks are undoable but not logged). The rules check runs after every move; a mistake is counted whether or not Mistake check shows it.

![Entering a digit](../diagrams/move.svg)

## Solving

`on_solved()` decides what kind of game it was. A normal game shows the Solved dialog after the confetti, and only then records the solve (80 ms later, so the dialog appears without waiting for the card); the database checks achievements and the toast comes by the usual poll.

![Solving](../diagrams/solved.svg)

## Saving the game in progress

After every move (and on a pause, or leaving the game) the game goes into RAM and, with a card, to `sudoku.db`. Saves are coalesced: only the newest one waiting is written, so a burst of taps is one write; a save of another game still waiting is written on its own first.

![Saving the game in progress](../diagrams/save_game.svg)

## Switching player

![Switching player](../diagrams/player_switch.svg)

## A time attack run

![A time attack run](../diagrams/time_attack.svg)

## The daily puzzle

![The daily puzzle](../diagrams/daily.svg)
