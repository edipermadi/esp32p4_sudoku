# Sandwich

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Sandwich: variant-examples #11](../screenshots/variants/sandwich.png)

## The rules

[Classic](classic.md) rules, and a number **left of each row** and **above each column**: the sum of the digits **between that line's 1 and its 9**, wherever they are and whichever comes first. A 0 means the 1 and the 9 are side by side.

## On the board

- The sums are in blue in a margin above and left of the board (the board is a little smaller to make room).
- Between a 1 and a 9 that add up to more than the sum, or, once filled, to anything else, the digits turn red.
- The grader and hints work out where a line's 1 and 9 can stand: "the 9 fewer places there".
- **Puzzles → ⋮ → New puzzle → Sandwich**.

## How to solve it

- **0:** the 1 and the 9 touch. Once one of them is placed, the other is right next to it.
- **35:** every digit from 2 to 8 is between them, so the 1 and the 9 are at the two ends of the line.
- **Small sums fix the gap:** the 1 is an end, so the digits between are 2 to 8. A sum of 2, 3 or 4 is that one digit alone; 5 is a single 5 or 2 + 3; 9 is two digits (2 + 7, 3 + 6, 4 + 5) or three (2 + 3 + 4). Small and very big sums leave few gap lengths.
- **Once the 1 is placed**, try each place for the 9: the cells between must be able to make the sum with the digits the line still has. Usually only one or two places work, as in the worked example.

## Worked example

**variant-examples #11**, Sandwich Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The sums are in the screenshot: rows 13, 18, 0, 2, 24, 5, 19, 0, 18 from the top; columns 8, 10, 8, 12, 8, 3, 14, 12, 0 from the left.

**Step 1: r7c7, next to row 7's 1.** Row 7 has 2 and 1; column 7 has 6, 4, 8, 5 and 3; box 9 has 3 and 8. By the Classic rules r7c7 could be 7 or 9. But row 7's 1 is in r7c6, right next to it: a 9 there would make row 7's sandwich 0, and its sum is **19**. **r7c7 = 7**.

**Step 2: the 9 in column 6 (sum 3).** Column 6's 1 is in r7c6. The digits between it and the 9 must add up to **3**. Where can the 9 go?

- r8c6, right below the 1: the sum would be 0.
- r9c6, two below: the one cell between, r8c6, would have to be 3. Possible.
- r6c6, r5c6, r2c6: rows 6, 5 and 2 already have a 9.
- r3c6: between it and the 1 are r4c6 (a 2), r5c6 and r6c6: at least 2 + 3 + 4 = 9, too much.
- r1c6 and r4c6 are filled.

**r9c6 = 9**.

**Step 3: r8c6, the filling.** r8c6 is now the only cell between column 6's 1 (r7c6) and 9 (r9c6), so it's the whole sum: **r8c6 = 3**.

Row 8's sum is 0 too, so its 1 and 9 touch; with the 3 in place, that's the next thing to look at.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py sandwich: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 53 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/sandwich-walkthrough/step-00.png)

| [1](sandwich/step-01.md) | [2](sandwich/step-02.md) | [3](sandwich/step-03.md) | [4](sandwich/step-04.md) | [5](sandwich/step-05.md) | [6](sandwich/step-06.md) | [7](sandwich/step-07.md) | [8](sandwich/step-08.md) | [9](sandwich/step-09.md) | [10](sandwich/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](sandwich/step-11.md) | [12](sandwich/step-12.md) | [13](sandwich/step-13.md) | [14](sandwich/step-14.md) | [15](sandwich/step-15.md) | [16](sandwich/step-16.md) | [17](sandwich/step-17.md) | [18](sandwich/step-18.md) | [19](sandwich/step-19.md) | [20](sandwich/step-20.md) |
| [21](sandwich/step-21.md) | [22](sandwich/step-22.md) | [23](sandwich/step-23.md) | [24](sandwich/step-24.md) | [25](sandwich/step-25.md) | [26](sandwich/step-26.md) | [27](sandwich/step-27.md) | [28](sandwich/step-28.md) | [29](sandwich/step-29.md) | [30](sandwich/step-30.md) |
| [31](sandwich/step-31.md) | [32](sandwich/step-32.md) | [33](sandwich/step-33.md) | [34](sandwich/step-34.md) | [35](sandwich/step-35.md) | [36](sandwich/step-36.md) | [37](sandwich/step-37.md) | [38](sandwich/step-38.md) | [39](sandwich/step-39.md) | [40](sandwich/step-40.md) |
| [41](sandwich/step-41.md) | [42](sandwich/step-42.md) | [43](sandwich/step-43.md) | [44](sandwich/step-44.md) | [45](sandwich/step-45.md) | [46](sandwich/step-46.md) | [47](sandwich/step-47.md) | [48](sandwich/step-48.md) | [49](sandwich/step-49.md) | [50](sandwich/step-50.md) |
| [51](sandwich/step-51.md) | [52](sandwich/step-52.md) | [53](sandwich/step-53.md) |  |  |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
