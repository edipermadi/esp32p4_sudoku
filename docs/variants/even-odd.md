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

**variant-examples #8**, Even-Odd Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The marks are in the screenshot.

**Step 1: r1c9, a shaded (even) cell.** Row 1 has 1, 5 and 9; column 9 has 1, 2, 5, 6 and 7; box 3 has 4 and 1. That leaves 3 and 8, and the cell is even. **r1c9 = 8**.

**Step 2: r2c3, a ringed (odd) cell.** Row 2 has 9, 2, 4 and 1; column 3 has 3 and 9; box 1 has 1, 9 and 7. That leaves 5, 6 and 8, and the cell is odd. **r2c3 = 5**.

**Step 3: r6c4, a shaded (even) cell.** Row 6 has 2, 1, 9, 8 and 7; column 4 has 5, 9 and 6; box 5 has 9 and 8. That leaves 3 and 4, and the cell is even. **r6c4 = 4**.

The pattern is the same each time: the Classic rules narrow a cell to two or three digits, and its mark picks one.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py even-odd: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 51 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/even-odd-walkthrough/step-00.png)

| [1](even-odd/step-01.md) | [2](even-odd/step-02.md) | [3](even-odd/step-03.md) | [4](even-odd/step-04.md) | [5](even-odd/step-05.md) | [6](even-odd/step-06.md) | [7](even-odd/step-07.md) | [8](even-odd/step-08.md) | [9](even-odd/step-09.md) | [10](even-odd/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](even-odd/step-11.md) | [12](even-odd/step-12.md) | [13](even-odd/step-13.md) | [14](even-odd/step-14.md) | [15](even-odd/step-15.md) | [16](even-odd/step-16.md) | [17](even-odd/step-17.md) | [18](even-odd/step-18.md) | [19](even-odd/step-19.md) | [20](even-odd/step-20.md) |
| [21](even-odd/step-21.md) | [22](even-odd/step-22.md) | [23](even-odd/step-23.md) | [24](even-odd/step-24.md) | [25](even-odd/step-25.md) | [26](even-odd/step-26.md) | [27](even-odd/step-27.md) | [28](even-odd/step-28.md) | [29](even-odd/step-29.md) | [30](even-odd/step-30.md) |
| [31](even-odd/step-31.md) | [32](even-odd/step-32.md) | [33](even-odd/step-33.md) | [34](even-odd/step-34.md) | [35](even-odd/step-35.md) | [36](even-odd/step-36.md) | [37](even-odd/step-37.md) | [38](even-odd/step-38.md) | [39](even-odd/step-39.md) | [40](even-odd/step-40.md) |
| [41](even-odd/step-41.md) | [42](even-odd/step-42.md) | [43](even-odd/step-43.md) | [44](even-odd/step-44.md) | [45](even-odd/step-45.md) | [46](even-odd/step-46.md) | [47](even-odd/step-47.md) | [48](even-odd/step-48.md) | [49](even-odd/step-49.md) | [50](even-odd/step-50.md) |
| [51](even-odd/step-51.md) |  |  |  |  |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
