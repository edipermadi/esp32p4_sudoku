# Quadruple

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Quadruple: variant-examples #20](../screenshots/variants/quadruple.png)

## The rules

[Classic](classic.md) rules, and **circles** where four cells meet, each listing **1 to 4 digits that must be among those four cells**, in any order. The other cells around the circle can hold anything else. A digit can be listed twice: then it's in two of the four cells, corner to corner (side by side would put it twice in a row or column).

## On the board

- Each circle sits on the corner where its four cells meet, with its digits inside, two to a line.
- When a circle's empty cells are fewer than the digits it still misses, its cells turn red.
- The grader and hints know two steps: when a circle's missing digits exactly fill its empty cells, those cells take nothing else; and a missing digit with only as many places as it needs takes them. "The circle with 139 needs those digits among its four cells, which leaves row 2, column 5 only 3, 9".
- **Puzzles → ⋮ → New puzzle → Quadruple**: 14 circles (Hard and Expert 18).

## How to solve it

- **Count:** compare a circle's missing digits with its empty cells. Equal numbers mean every empty cell takes one of those digits, and nothing else.
- **A full circle with one gap:** if three of four listed digits are placed, the fourth goes in the last empty cell.
- **Where can a listed digit go?** Within the four cells, rule out the ones whose row, column or box already has it. If one cell is left, it's there.
- A circle also says what's *not* there: a circle of four digits fixes the whole set of its four cells.

## Worked example

**variant-examples #20**, Quadruple Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*). The circles are in the screenshot; two matter here:

- **1 2 8 9** where r8c5, r8c6, r9c5 and r9c6 meet (r9c6 is a given 2);
- **3 6 7 9** where r1c6, r1c7, r2c6 and r2c7 meet (r1c6 is 9, r2c6 is 6, r2c7 is 7).

```
1 4 . | 5 . 9 | . . .
. . . | . . 6 | 7 4 .
. . . | . 3 . | . 2 .
------+-------+------
. 7 . | 9 . 3 | . 1 .
6 . . | 2 . 7 | . . 5
. 1 . | 4 . 8 | . 7 .
------+-------+------
. 2 . | . 4 . | . . .
. 3 6 | 7 . . | . . .
. . . | 6 . 2 | . 3 7
```

**Step 1: r8c6, in the 1-2-8-9 circle.** Row 8 has 3, 6 and 7; column 6 has 9, 6, 3, 7, 8 and 2; box 8 has 4, 7, 6 and 2. By the Classic rules r8c6 could be 1 or 5. Now the circle: its 2 is placed (r9c6), so 1, 8 and 9 are still missing, and it has exactly three empty cells (r8c5, r8c6, r9c5). Each of them must take one of 1, 8 and 9: r8c6 can't be 5. **r8c6 = 1**.

**Step 2: r1c7, the last gap in the 3-6-7-9 circle.** The circle's 9 (r1c6), 6 (r2c6) and 7 (r2c7) are placed; its 3 must be in its one empty cell. **r1c7 = 3**.

**Step 3: the 6 in box 1.** Box 1 (top left) needs a 6. Column 3 has one (r8c3), row 2 too (r2c6) and column 1 (r5c1): that rules out r1c3, r3c3, r2c1, r2c2, r2c3 and r3c1. **r3c2 = 6**.

Back at the 1-2-8-9 circle: with the 1 in r8c6, its other two empty cells, r8c5 and r9c5, now hold 8 and 9 between them. Which is which comes later, from row 8, row 9 or column 5.
