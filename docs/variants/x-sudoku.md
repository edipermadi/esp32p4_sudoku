# X-Sudoku

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![X-Sudoku: variant-examples #2](../screenshots/variants/x-sudoku.png)

## The rules

[Classic](classic.md) rules, and both long **diagonals** must also hold 1 to 9, each once: the **main diagonal** from the top-left corner to the bottom-right, and the **anti-diagonal** from the top-right corner to the bottom-left. The centre cell is on both.

## On the board

- The 17 diagonal cells have a warm tint.
- A digit that repeats on a diagonal turns red like any other repeat, and completing a diagonal sparkles.
- Hints name the diagonals: "Look at the top-left diagonal" (the main one, starting in the top-left corner) or "the top-right diagonal".
- **Puzzles → ⋮ → New puzzle → X-Sudoku**; it's also Wednesday's daily puzzle.

## How to solve it

- Treat each diagonal as a tenth "row": a hidden single can be on a diagonal ("the only place for 5 on the main diagonal").
- A diagonal cell has more neighbours than a normal cell (its row, column, box and diagonal), so diagonal cells often run out of candidates first: look there for naked singles.
- The centre cell sees both diagonals: 32 other cells instead of 20.

## Worked example

**variant-examples #2**, X-Sudoku Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*).

```
. 4 3 | . 6 . | 7 . .
8 7 . | 3 . . | . . 9
. . 1 | 9 . . | . 3 .
------+-------+------
3 8 . | . . . | . 1 7
7 1 . | 5 4 3 | . 2 8
4 2 . | . . . | . 9 3
------+-------+------
. 9 . | . . 2 | 3 . .
2 . . | . . 6 | . 5 1
. . 4 | . 3 . | 9 7 .
```

**Step 1: r2c8, using the anti-diagonal.** r2c8 is on the anti-diagonal (it runs r1c9, r2c8, r3c7, r4c6, r5c5, r6c4, r7c3, r8c2, r9c1). Which digits can it take?

- Row 2 has 8, 7, 3 and 9.
- Column 8 has 3, 1, 2, 9, 5 and 7.
- That leaves 4 and 6. By the Classic rules alone that's as far as it goes.
- But r5c5, on the same anti-diagonal, is a 4.

So **r2c8 = 6**.

**Step 2: r6c6, using the main diagonal.** r6c6 is on the main diagonal (r1c1, r2c2, ..., r9c9), which already has 7 (r2c2), 1 (r3c3), 4 (r5c5), 3 (r7c7) and 5 (r8c8). Add row 6 (4, 2, 9, 3), column 6 (3, 2, 6) and box 5 (5, 4, 3): every digit but 8 is there. **r6c6 = 8**.

**Step 3: the 6 in column 4.** Column 4 needs a 6:

- r1c4: row 1 has a 6 (r1c5).
- r6c4: it's on the anti-diagonal, which now has the 6 from step 1 (r2c8).
- r7c4, r9c4: box 8 has a 6 (r8c6).
- r8c4: row 8 has that same 6.
- r2c4, r3c4 and r5c4 are filled (3, 9, 5).

Only **r4c4** is left: **r4c4 = 6**. Without the diagonal rule, r6c4 would still have been a place for it.
