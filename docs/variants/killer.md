# Killer

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Killer: variant-examples #6](../screenshots/variants/killer.png)

## The rules

[Classic](classic.md) rules, plus **cages**: groups of 2 to 5 connected cells outlined with a dashed line, with a small **sum** in the top-left corner. The digits in a cage add up to its sum, and **no digit repeats within a cage**. Killer puzzles have few givens, or none: the cages carry the information.

## On the board

- Each cage is a dashed outline; its sum is in its top-left cell. Pencil marks are drawn smaller so they stay clear of the sum.
- A cage whose digits go over its sum, or that's full and short of it, turns red; so does a digit repeated in a cage.
- **Hold a cell** to see which digits can still make up its cage (the cage helper).
- **Puzzles → ⋮ → New puzzle → Killer**: Easy and Medium use cages of 2 or 3 cells, Hard and Expert up to 5. It's Friday's daily puzzle (Medium).

## How to solve it

- **Two-cell cages with a given:** the other cell is the sum minus that digit.
- **Small and big sums have few combinations:** 3 in two cells is always 1 + 2; 4 is 1 + 3; 16 is 7 + 9; 17 is 8 + 9; 6 in three cells is 1 + 2 + 3; 24 in three is 7 + 8 + 9.
- **Impossible digits:** a digit that would need its partner to be 10 or more, or 0 or less, or a repeat, can't go in the cage. That's what the worked example uses.
- **The rule of 45:** a row, column or box adds up to 45. If cages cover a box except for one cell, that cell is 45 minus the cages' sums; if one cage pokes out of the box, the cell sticking out is the cages' total minus 45.

## Worked example

**variant-examples #6**, Killer Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*). The cages are in the screenshot; these three matter:

- the bottom-left cage: **15** over r9c1, r9c2 and r9c3 (r9c3 is a given 2);
- a two-cell cage of **13**: r4c7 (a given 8) and r5c7;
- a two-cell cage of **16**: r4c8 and r4c9 (a given 9).

```
. . . . . . 1 . .
3 . . . . . . . .
. . . . . . . . .
. . . . . 1 8 . 9
. . . . . . . . .
1 . 3 5 . . . . .
. . . . . . . . .
. . . . . . . . 3
. . 2 . . . . . .
```

**Step 1: the 3 in column 2.** Column 2 needs a 3. Go down it:

- r1c2, r2c2, r3c2: box 1 has a 3 (r2c1).
- r4c2, r5c2, r6c2: box 4 has a 3 (r6c3).
- r8c2: row 8 has a 3 (r8c9).
- r9c2: it's in the bottom-left cage of 15 with the given 2, so r9c1 + r9c2 = 13. If r9c2 were 3, r9c1 would have to be 10. Impossible.

Only **r7c2** is left: **r7c2 = 3**. Without the cage, r9c2 would still be open.

**Step 2: the 3 in column 7.** r1c7 and r4c7 are filled; r2c7, r6c7, r7c7 (the 3 just placed in row 7), r8c7 and r9c7 all see a 3 in their row or box. That leaves r3c7 and **r5c7**, and r5c7 is in the cage of 13 with the 8: it must be 13 − 8 = 5, not 3. **r3c7 = 3**.

**Step 3: the 3 in column 8.** r1c8 and r3c8 see the 3 just placed in r3c7 (box 3, row 3); r2c8, r6c8, r7c8, r8c8 and r9c8 see a 3 in their row or box. That leaves r5c8 and **r4c8**, and r4c8 is in the cage of 16 with the 9: it must be 7. **r5c8 = 3**.

The same cages give two more digits at once: **r5c7 = 5** and **r4c8 = 7**. Killer often goes like this: a few sums fixed early, then the Classic rules take over.
