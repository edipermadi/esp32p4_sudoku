# Asterisk

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Asterisk: variant-examples #16](../screenshots/variants/asterisk.png)

## The rules

[Classic](classic.md) rules, and nine **tinted cells** in an asterisk shape around the centre also hold 1 to 9, each once. The asterisk has one cell in each box: r2c5, r3c3, r3c7, r5c2, r5c5 (the centre), r5c8, r7c3, r7c7 and r8c5.

## On the board

- The nine asterisk cells have a warm tint, like the X-Sudoku diagonals.
- A repeat on the asterisk turns red; hints name it ("Look at the asterisk").
- The rules card draws the whole 9×9 with the asterisk's digits 1 to 9.
- **Puzzles → ⋮ → New puzzle → Asterisk**.

## How to solve it

- The asterisk is one extra "box", spread over all nine boxes: keep track of which of its digits are placed.
- Its cells are far apart, so the rule reaches across the grid: a digit on the asterisk rules out the other eight asterisk cells, in other boxes, other rows and other columns.
- The asterisk often settles the centre cell, r5c5: it sees its row, column, box and the asterisk.
- **Hidden singles on the asterisk:** when a digit can go in only one of its empty cells, it goes there.

## Worked example

**variant-examples #16**, Asterisk Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*).

```
. 4 2 | 5 7 9 | 3 . 8
. . . | . . . | . 4 7
. 6 . | . . 2 | 5 . 9
------+-------+------
6 . . | 8 3 . | . . 5
. . . | 6 . 5 | . . .
5 . . | . 2 7 | . . 3
------+-------+------
8 . 4 | 9 . . | . 3 .
2 5 . | . . . | . . .
9 . 3 | 2 8 4 | 1 5 .
```

**Step 1: the 4 in column 5.** Column 5 needs a 4; its empty cells are r2c5, r3c5, r5c5, r7c5 and r8c5.

- r2c5: row 2 has a 4 (r2c8).
- r7c5: row 7 has a 4 (r7c3).
- r8c5: box 8 has a 4 (r9c6).
- r5c5: it's the centre of the asterisk, and **r7c3**, also on the asterisk, is a 4. By the Classic rules alone, r5c5 would still be open (row 5, column 5 and box 5 have no 4).

Only **r3c5** is left: **r3c5 = 4**.

**Step 2: the 5 in box 1.** Box 1 (top left) needs a 5. Row 1 has one (r1c4), row 3 too (r3c7), column 1 (r6c1) and column 2 (r8c2): that rules out r1c1, r3c1, r3c3, r2c1 and r2c2. **r2c3 = 5**.

**Step 3: the 9 in box 1.** Row 1 has a 9 (r1c6), row 3 (r3c9) and column 1 (r9c1): r1c1, r3c1, r3c3 and r2c1 are out. **r2c2 = 9**.

Steps 2 and 3 are plain Classic singles: an Easy Asterisk puzzle mixes the two, and the asterisk comes back into play as its cells fill up (r3c3 is on it, and is now one of only two empty cells in box 1).
