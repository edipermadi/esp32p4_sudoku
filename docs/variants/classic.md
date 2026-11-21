# Classic

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Classic: variant-examples #1](../screenshots/variants/classic.png)

## The rules

Fill the 9×9 grid so that every **row**, every **column** and every **3×3 box** (the nine blocks inside the thick lines) holds the digits 1 to 9, each exactly once. The grey cells are the givens: they're part of the puzzle and can't be changed. Every puzzle on the board has exactly one solution, and it can always be reached by reasoning, no guessing.

Every other variant on these pages keeps these rules and adds one of its own.

## On the board

- Tap a cell, then a digit on the pad on the right. The small number on each pad key is how many of that digit are still to place.
- A digit that repeats in a row, column or box turns red (Settings → Game → Mistake check), and the cells it clashes with show it too.
- The 1600 built-in puzzles (Puzzles → Built-in) are all Classic; **Puzzles → ⋮ → New puzzle → Classic** makes a new one at any level, and the daily puzzle is Classic.
- **Hint** (the eye) shows where to look first, then the cell and why.

## How to solve it

- **Hidden single:** pick a digit and a box (or a row or column). If every cell but one is ruled out for that digit, because the digit is already in that cell's row or column, it goes in the one that's left. This is the step to look for first.
- **Naked single:** a cell whose row, column and box already hold eight different digits can only take the ninth.
- Work through the digits that already appear most often (the pad's small counts tell you): they rule out the most cells.
- Harder puzzles need more: **pairs** (two cells in a unit that can only hold the same two digits), **pointing** (a digit that, inside a box, can only be in one row, so it's not anywhere else in that row), and beyond. **Puzzles → ⋮ → Learn** practises each technique.

## Worked example

The puzzle in the screenshot is **variant-examples #1**, a Classic Easy puzzle. To play along, import [`variant-examples.sdx`](../variant-examples.sdx) (copy it to `puzzles/` on the card, then **Files → puzzles → variant-examples.sdx → Import**): it lands in **Puzzles → Saved → variant-examples**. Cells are written **r**ow**c**olumn, counted from the top left: r1c3 is row 1, column 3.

```
. 4 . | . . . | 3 6 8
. . . | . 2 . | 7 . .
. . . | 1 . . | 5 2 9
------+-------+------
. 7 4 | 9 6 3 | . 1 2
. . 3 | 2 . 7 | 4 . .
2 1 . | 4 5 8 | 6 7 .
------+-------+------
9 2 7 | . . 5 | . . .
. . 6 | . 9 . | . . .
4 5 1 | . . . | . 3 .
```

**Step 1: the 2 in box 1 (top left).** Box 1 needs a 2. Go through its empty cells:

- r1c1: column 1 already has a 2 (r6c1).
- r2c1, r2c2, r2c3: row 2 has a 2 (r2c5).
- r3c1, r3c2, r3c3: row 3 has a 2 (r3c8).
- r1c2 holds the 4.

Only **r1c3** is left, so **r1c3 = 2**.

**Step 2: the 1 in box 3 (top right).** Box 3 has seven digits already (3, 6, 8, 7, 5, 2, 9); its empty cells are r2c8 and r2c9. Column 8 has a 1 at r4c8, so r2c8 can't be 1. **r2c9 = 1**.

**Step 3: the 1 in box 1.** Now row 2 has a 1 (the one just placed in r2c9), so r2c1, r2c2 and r2c3 are out; row 3 has a 1 at r3c4, so r3c1, r3c2 and r3c3 are out. r1c2 and r1c3 are filled. Only **r1c1** is left: **r1c1 = 1**.

From here the puzzle goes on the same way, one single after another: an Easy puzzle never needs more. If you get stuck, **Hint** shows the next step.
