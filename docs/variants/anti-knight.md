# Anti-Knight

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Anti-Knight: variant-examples #7](../screenshots/variants/anti-knight.png)

## The rules

[Classic](classic.md) rules, and two cells a **chess knight's move** apart never hold the same digit. A knight's move is two cells in one direction and one cell sideways: from r5c5, the cells r3c4, r3c6, r4c3, r4c7, r6c3, r6c7, r7c4 and r7c6.

## On the board

- Nothing extra is drawn: the rule is the same everywhere.
- A digit that repeats a knight's move away turns red like any other repeat; hints say "a knight's move away".
- The rules card (the ⋮ menu → Rules) draws a 5 with its eight knight cells crossed out.
- **Puzzles → ⋮ → New puzzle → Anti-Knight**.

## How to solve it

- Every cell has up to 8 extra neighbours, so naked singles come much sooner than in Classic: when you check a cell, also look at the eight knight cells around it.
- For a hidden single in a row or box, a digit rules out the cells a knight's move from it, often in the next box over.
- A digit in a 3×3 box rules out a lot of the neighbouring boxes: a 5 in the centre of box 5 sees the edges of boxes 2, 4, 6 and 8.

## Worked example

**variant-examples #7**, Anti-Knight Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*).

```
. . 2 | . . 9 | 3 . .
. . . | 2 6 . | 4 1 9
. . 3 | 4 . . | 7 . .
------+-------+------
2 . . | 3 4 . | . . .
. . 5 | 9 . 6 | 1 . .
. . . | . 8 7 | . . 2
------+-------+------
. . 1 | . . 4 | 5 . .
8 5 4 | . 9 2 | . . .
. . 7 | 8 . . | 2 . .
```

**Step 1: r7c4.** Row 7 has 1, 4 and 5; column 4 has 2, 4, 3, 9 and 8; box 8 has 4, 9, 2 and 8. By the Classic rules r7c4 could be 6 or 7. A knight's move from r7c4 (one row up, two columns right) is **r6c6**, which is a 7. **r7c4 = 6**.

**Step 2: the 4 in row 6.** Row 6 has 8, 7 and 2; it needs a 4 among r6c1, r6c2, r6c3, r6c4, r6c7 and r6c8:

- r6c2: **r8c3** is a 4, two rows down and one column right: a knight's move.
- r6c3: column 3 has that 4 (r8c3).
- r6c4: column 4 has a 4 (r3c4).
- r6c7: column 7 has a 4 (r2c7).
- r6c8: **r7c6** is a 4, one row down and two columns left: a knight's move.

Only **r6c1** is left: **r6c1 = 4**. Two of the five cells were ruled out by knights.

**Step 3: the 5 in row 9.** Row 9 has 7, 8 and 2; for its 5:

- r9c1, r9c2: box 7 has a 5 (r8c2).
- r9c6: **r7c7** is a 5, two rows up and one column right: a knight's move.
- r9c8, r9c9: box 9 has that 5 (r7c7).

Only **r9c5** is left: **r9c5 = 5**.
