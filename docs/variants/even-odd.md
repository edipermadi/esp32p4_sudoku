# Even-Odd

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Even-Odd: variant-examples #8](../screenshots/variants/even-odd.png)

## The rules

[Classic](classic.md) rules, and some cells are marked: a **shaded** cell holds an **even** digit (2, 4, 6 or 8), a cell with a **ring** holds an **odd** one (1, 3, 5, 7 or 9). Unmarked cells can hold either.

## On the board

- About a third of the cells are marked: shaded for even, a grey ring for odd (a given can be marked too).
- A digit of the wrong kind turns red at once.
- **Puzzles → ⋮ → New puzzle → Even-Odd**.

## How to solve it

- A mark halves a cell's possibilities. Whenever the Classic rules leave a cell with two or three candidates, check its mark: it often leaves one.
- Count by kind in a unit: a row has four even and five odd digits. If a row's even digits are all placed, every empty cell in it is odd, marked or not.
- For hidden singles, an odd digit can't go in a shaded cell: those cells drop out of the search for 1, 3, 5, 7 and 9.

## Worked example

**variant-examples #8**, Even-Odd Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*). The marks are in the screenshot.

```
1 . . | 5 . 9 | . . .
. 9 . | . 2 . | . 4 1
7 . . | . . . | . . .
------+-------+------
. 7 . | 9 . . | 8 1 2
6 . 3 | . . . | 4 . 5
2 1 9 | . . 8 | . 7 .
------+-------+------
. . . | . . . | . . 6
8 3 . | . 9 . | . 5 .
. . . | 6 . 2 | . . 7
```

**Step 1: r1c9, a shaded (even) cell.** Row 1 has 1, 5 and 9; column 9 has 1, 2, 5, 6 and 7; box 3 has 4 and 1. That leaves 3 and 8, and the cell is even. **r1c9 = 8**.

**Step 2: r2c3, a ringed (odd) cell.** Row 2 has 9, 2, 4 and 1; column 3 has 3 and 9; box 1 has 1, 9 and 7. That leaves 5, 6 and 8, and the cell is odd. **r2c3 = 5**.

**Step 3: r6c4, a shaded (even) cell.** Row 6 has 2, 1, 9, 8 and 7; column 4 has 5, 9 and 6; box 5 has 9 and 8. That leaves 3 and 4, and the cell is even. **r6c4 = 4**.

The pattern is the same each time: the Classic rules narrow a cell to two or three digits, and its mark picks one.
