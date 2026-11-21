# Non-Consecutive

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Non-Consecutive: variant-examples #14](../screenshots/variants/non-consecutive.png)

## The rules

[Classic](classic.md) rules, and two **side-by-side** cells (left and right, or above and below) never hold digits that are **1 apart**: no 4 next to a 3 or a 5. Cells that touch only at a corner don't count.

It's [Consecutive](consecutive.md) with no bars at all.

## On the board

- Nothing extra is drawn.
- Two neighbours 1 apart turn red.
- The grader and hints treat it like Consecutive without bars: "Its neighbours can't be 1 apart from row 4, column 2, which leaves it only 3, 7".
- **Puzzles → ⋮ → New puzzle → Non-Consecutive**. Easy keeps 24 or more givens. It's the slowest variant to make (a few seconds at Hard).

## How to solve it

- **Every filled cell rules out two digits in up to four neighbours:** next to a 6, no 5 or 7. Next to a 1 or a 9, only one (no 2, no 8).
- When the Classic rules leave a cell two or three candidates, look at its four neighbours: one of them often rules a candidate out, as in every step of the worked example.
- **Between two digits:** a cell between a 4 and a 6 can't be 3, 5 or 7.
- Digits alternate a lot: neighbours are never "close", so a row tends to swing between low and high digits.

## Worked example

**variant-examples #14**, Non-Consecutive Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*).

```
. . 4 | . 3 . | . . .
. . . | . . . | 7 4 1
. . 2 | 4 . 8 | . 6 9
------+-------+------
. 5 . | . . . | . . .
. . 3 | 5 . 9 | 4 . .
. . . | . . . | . 9 .
------+-------+------
7 4 . | 8 . 3 | 9 . .
3 6 8 | . . . | . . .
. . . | . 4 . | 8 . .
```

**Step 1: r7c3.** Row 7 has 7, 4, 8, 3 and 9; column 3 has 4, 2, 3 and 8; box 7 has 7, 4, 3, 6 and 8. By the Classic rules r7c3 could be 1 or 5. Its left neighbour, r7c2, is a 4, and 5 is 1 away from 4. **r7c3 = 1**.

**Step 2: r9c3.** Row 9 has 4 and 8; column 3 now has 4, 2, 3, 8 and the new 1; box 7 has 7, 4, 1, 3, 6 and 8. That leaves 5 and 9. The cell above, r8c3, is an 8, and 9 is 1 away from 8. **r9c3 = 5**.

**Step 3: r9c1.** Row 9 has 4, 8 and now 5; column 1 has 7 and 3; box 7 has 7, 4, 1, 3, 6, 8 and 5. That leaves 2 and 9. The cell above, r8c1, is a 3, and 2 is 1 away from 3. **r9c1 = 9**.

Box 7 now has one empty cell, r9c2, and one missing digit, 2. Check it against the rule: its neighbours are 9, 5 and (above) 6, none of them 1 or 3. **r9c2 = 2**.
