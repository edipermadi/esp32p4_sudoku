# Kropki

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Kropki: variant-examples #17](../screenshots/variants/kropki.png)

## The rules

[Classic](classic.md) rules, and **dots** on the lines between side-by-side cells:

- a **white dot**: the two digits are **1 apart** (3 and 4);
- a **black dot**: one digit is **double** the other (3 and 6).

**Every dot is shown**, so where there's **no dot**, the two digits are neither 1 apart nor one double the other. 1 and 2 are both, so they get either colour.

## On the board

- White dots (with a dark rim) and black dots sit on the grid lines between cells.
- Two digits that break a dot, or a missing dot, turn red.
- The grader and hints narrow each cell by its neighbours across the dots: "The dots around row 3, column 4 (and where there are none) leave it only 2, 6".
- **Puzzles → ⋮ → New puzzle → Kropki**. The dots carry most of the puzzle: Easy keeps about 15 givens, Expert often none at all.

## How to solve it

- **Black dot pairs** are few: 1–2, 2–4, 3–6, 4–8. A cell on a black dot is never 5, 7 or 9.
- **White dot next to a digit:** two choices (next to a 7: 6 or 8).
- **No dot next to a digit** rules out up to four digits: next to a 3 with no dot, the cell isn't 2, 4 (1 apart) or 6 (double); next to a 4, not 3, 5, 2 or 8.
- A cell with no dots at all around it, among filled neighbours, loses many candidates at once.

## Worked example

**variant-examples #17**, Kropki Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*). The dots are in the screenshot.

```
. . . | 9 . . | 8 . 7
. 7 5 | . . . | 6 . .
. . . | . . . | . . .
------+-------+------
. . . | . . . | . . .
1 . . | . . . | . . 5
. . . | . . . | . . .
------+-------+------
. . . | . . . | . . .
. . 1 | . . . | 3 5 .
7 . 2 | . . 9 | . . .
```

**Step 1: r9c7, with no dot to the 3 above it.** Row 9 has 7, 2 and 9; column 7 has 8, 6 and 3; box 9 has 3 and 5. By the Classic rules r9c7 could be 1 or 4. The cell above, r8c7, is a 3, and there's **no dot** between them: so r9c7 isn't 4 (1 apart from 3), nor 2 or 6. **r9c7 = 1**.

**Step 2: r1c3, with no dots around it.** Row 1 has 9, 8 and 7; column 3 has 5, 1 and 2; box 1 has 7 and 5. That leaves 3, 4 and 6. The cell below, r2c3, is a 5, with **no dot**: so r1c3 isn't 4 or 6. **r1c3 = 3**.

**Step 3: the 1 in column 2.** Column 2 needs a 1:

- r4c2, r5c2, r6c2: box 4 has a 1 (r5c1).
- r7c2, r8c2: box 7 and row 8 have a 1 (r8c3).
- r9c2: row 9 now has the 1 from step 1.
- r3c2: there's a **white dot** between it and the 7 above (r2c2), so it's 6 or 8, not 1.

Only **r1c2** is left: **r1c2 = 1**. Notice that r1c2 (1) and r1c3 (3) have no dot between them, as they shouldn't: 1 and 3 are neither 1 apart nor double.
