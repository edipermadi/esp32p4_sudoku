# Consecutive

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Consecutive: variant-examples #9](../screenshots/variants/consecutive.png)

## The rules

[Classic](classic.md) rules, and an orange **bar** sits on the line between every two side-by-side cells whose digits are **1 apart** (like 4 and 5). Every such pair has a bar, so **where there's no bar, the two digits are not 1 apart**. Cells that only touch at a corner don't count.

## On the board

- The bars are short orange marks across the line between two cells.
- Two digits that break the rule (a bar and not 1 apart, or 1 apart with no bar) turn red.
- The grader and hints use the bars: "The bars around row 4, column 2 (and where there are none) leave it only 3, 5".
- **Puzzles → ⋮ → New puzzle → Consecutive**. The bars carry most of the puzzle: Easy keeps about 14 givens, Expert as few as 2 to 4.

## How to solve it

- **A bar next to a digit** leaves two choices: next to a 6, the cell is 5 or 7. Next to a 1 or a 9, only one: 2 or 8.
- **No bar is information too.** A cell next to a 5 with no bar can't be 4 or 6. A cell with no bars at all around it, next to several filled cells, loses a lot of candidates.
- **Count the bars:** a 1 can only have a bar to a 2, and a row and a column each hold one 2, so a 1 (or a 9) has at most two bars. A cell with three or four bars is 2 to 8.
- In the worked example, both kinds show up: bars that leave two choices, and missing bars that rule a digit out.

## Worked example

**variant-examples #9**, Consecutive Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*). The bars are in the screenshot.

```
. . . | . . . | . . .
. . . | . . . | . . 2
. . . | . . . | . . 5
------+-------+------
. 6 . | 4 9 . | . . .
. . . | 3 . 7 | . . .
. . . | . 8 5 | . 6 .
------+-------+------
5 . . | . . . | . . .
7 . . | . . . | . . .
. . . | . . . | . . .
```

**Step 1: r6c4, with no bar above it.** Row 6 has 8, 5 and 6; column 4 has 4 and 3; box 5 has 4, 9, 3, 7, 8 and 5. By the Classic rules r6c4 could be 1 or 2. But r5c4, right above it, is a 3, and there's **no bar** between them: so r6c4 isn't 2 (or 4). **r6c4 = 1**.

**Step 2: r6c9, with a bar to its left.** There's a **bar** between r6c8 (a 6) and r6c9, so r6c9 is 5 or 7. Row 6 already has a 5 (r6c6). **r6c9 = 7**.

**Step 3: the 5 in box 4.** Box 4 (rows 4–6, columns 1–3) needs a 5:

- r4c1, r5c1: column 1 has a 5 (r7c1).
- r6c1, r6c2, r6c3: row 6 has a 5 (r6c6).
- r4c3: its right-hand neighbour r4c4 is a 4, with **no bar**: r4c3 can't be 3 or 5.
- r5c2: the cell above it, r4c2, is a 6, with **no bar**: r5c2 can't be 5 or 7.

Only **r5c3** is left: **r5c3 = 5**.

While you're at r4c3: it has a **bar** to the 6 on its left (so it's 5 or 7) and no bar to the 4 on its right (so it's not 5): **r4c3 = 7**.
