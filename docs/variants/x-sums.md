# X-Sums

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![X-Sums: variant-examples #28](../screenshots/variants/x-sums.png)

## The rules

[Classic](classic.md) rules, and **clues** left of some rows and above some columns. Read the row from the left (the column from the top): its **first digit is X**, and the clue is **the sum of the first X digits**, the first one included. In 3, 4, 2, 9, 8 the first digit is 3, so the clue is 3 + 4 + 2 = 9. A line without a clue has no such rule.

## On the board

- The clues are in blue in a margin left of the rows and above the columns; a line without a clue has none.
- Once a line's first digit is in, its first X digits turn red when they already add up to more than the clue, or (all filled) to something else.
- The grader and hints try each first digit a clue still allows against the digits there: "The clue of row 4 is 31: the first digit from there says how many digits add up to it ...".
- **Puzzles → ⋮ → New puzzle → X-Sums**: about three quarters of the lines have a clue (Hard and Expert: about 60 %).

## How to solve it

- **A clue of 1:** the line starts with 1 (a 1 counts just itself). **A clue of 45:** it starts with 9 (all nine digits).
- **Each first digit X has a range:** X = 2 makes 3 to 11, X = 3 makes 6 to 20 (3 + 1 + 2 up to 3 + 9 + 8), X = 4 makes 10 to 28, and so on. A clue rules out every X whose range misses it; a clue of 2 can't happen.
- **Once X is known, it's a cage:** the first X cells add up to the clue, with no repeats (they're in one row or column). The cells after them are free.
- **Big clues, big X:** a clue of 35 needs at least 6 digits, so the line starts with 6 to 9.

## Worked example

**variant-examples #28**, X-Sums Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The clues are in the picture; from the left, row 4's is **31**; from the top, column 4's is **25** and column 8's **29**.

**Step 1: r4c2, row 4 (clue 31).** Row 4 reads 5, ., 4, 9, 6, 3, ., ., 2; by the Classic rules r4c2 could be 1 or 7. The row starts with 5, so its first five digits add up to 31: 5 + r4c2 + 4 + 9 + 6 = 24 + r4c2. **r4c2 = 7**.

**Step 2: r4c8, column 8 (clue 29).** Row 4 now misses 1 and 8. Column 8 starts with 6, so its first six digits add up to 29: 6, 4, r3c8, r4c8, 9 and r6c8. That's 19 already, leaving 10 for r3c8, r4c8 and r6c8. With an 8 in r4c8 the other two would need 2 between them: impossible with two different digits. **r4c8 = 1**.

**Step 3: r5c4, column 4 (clue 25).** Row 5 has 8 and 9; column 4 has 1, 3, 4, 8 and 9; box 5 has 5 and 6: that leaves 2 and 7. Column 4 reads ., 8, 1, 9, r5c4, 4, 3 from the top, and its first cell can only be 2, 5, 6 or 7. With a 7 in r5c4, the first cell is 2, 5 or 6: starting with 2 gives 2 + 8 = 10; with 5, 5 + 8 + 1 + 9 + 7 = 30; with 6, 6 + 8 + 1 + 9 + 7 + 4 = 35. None is 25. **r5c4 = 2** (and then the column starts with 5: 5 + 8 + 1 + 9 + 2 = 25).

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py x-sums: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 51 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/x-sums-walkthrough/step-00.png)

| [1](x-sums/step-01.md) | [2](x-sums/step-02.md) | [3](x-sums/step-03.md) | [4](x-sums/step-04.md) | [5](x-sums/step-05.md) | [6](x-sums/step-06.md) | [7](x-sums/step-07.md) | [8](x-sums/step-08.md) | [9](x-sums/step-09.md) | [10](x-sums/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](x-sums/step-11.md) | [12](x-sums/step-12.md) | [13](x-sums/step-13.md) | [14](x-sums/step-14.md) | [15](x-sums/step-15.md) | [16](x-sums/step-16.md) | [17](x-sums/step-17.md) | [18](x-sums/step-18.md) | [19](x-sums/step-19.md) | [20](x-sums/step-20.md) |
| [21](x-sums/step-21.md) | [22](x-sums/step-22.md) | [23](x-sums/step-23.md) | [24](x-sums/step-24.md) | [25](x-sums/step-25.md) | [26](x-sums/step-26.md) | [27](x-sums/step-27.md) | [28](x-sums/step-28.md) | [29](x-sums/step-29.md) | [30](x-sums/step-30.md) |
| [31](x-sums/step-31.md) | [32](x-sums/step-32.md) | [33](x-sums/step-33.md) | [34](x-sums/step-34.md) | [35](x-sums/step-35.md) | [36](x-sums/step-36.md) | [37](x-sums/step-37.md) | [38](x-sums/step-38.md) | [39](x-sums/step-39.md) | [40](x-sums/step-40.md) |
| [41](x-sums/step-41.md) | [42](x-sums/step-42.md) | [43](x-sums/step-43.md) | [44](x-sums/step-44.md) | [45](x-sums/step-45.md) | [46](x-sums/step-46.md) | [47](x-sums/step-47.md) | [48](x-sums/step-48.md) | [49](x-sums/step-49.md) | [50](x-sums/step-50.md) |
| [51](x-sums/step-51.md) |  |  |  |  |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
