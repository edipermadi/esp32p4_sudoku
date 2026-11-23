# Dutch Whispers

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Dutch Whispers: variant-examples #38](../screenshots/variants/dutch-whispers.png)

## The rules

[Classic](classic.md) rules, plus **amber lines** through cells side by side or diagonal. Along a line, **two neighbours differ by at least 4**: 3 next to 7 is fine, 3 next to 6 isn't. It's [German Whispers](german-whispers.md) with a gap of 4 instead of 5, so a **5 can be on a line**, but only between a 1 and a 9.

## On the board

- Each line is a narrow amber line under the digits.
- Two neighbours on a line less than 4 apart turn red.
- The grader and hints narrow each line cell by its neighbours: "Along this line, neighbours are 4 or more apart, so row 7, column 3 can only be 1, 2".
- **Puzzles → ⋮ → New puzzle → Lines → Dutch**: 8 lines (Hard and Expert 10) of 3 to 6 cells.

## How to solve it

- **A 5 is pinned:** the only digits 4 or more away from a 5 are 1 and 9. A 5 in the middle of a line has a 1 on one side and a 9 on the other.
- **4 and 6 are tight:** next to a 4, a cell is 8 or 9; next to a 6, 1 or 2.
- **Low and high mostly alternate:** with a gap of 4, two neighbours can't both be 1–4 or both be 6–9 (the most two such digits differ by is 3). Only a 5 breaks the pattern.
- **Next to an extreme, more is left than in German Whispers:** next to a 9, a cell is 1 to 5; next to a 1, 5 to 9.

## Worked example

**variant-examples #38**, Dutch Whispers Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The lines are in the screenshot; two matter here:

- r3c5, r2c5 (a 7), r1c4, r2c4 (a 9), r3c3 (a 5): the 5 at its end sits next to a 9, 4 away;
- r1c2 (a 2), r2c2 (a 6), r2c3, r3c2.

**Step 1: r1c4, between a 7 and a 9.** Row 1 has 1, 2, 5 and 6; column 4 has 4 and 9; box 2 has a 7; that leaves 3 and 8. On its line, r1c4 is next to the 7 in r2c5 and the 9 in r2c4, and 8 is only 1 away from either. **r1c4 = 3**.

**Step 2: r2c3, next to a 6.** Row 2 has 6, 7 and 9; column 3 has 2 and 5; box 1 has a 4; that leaves 1, 3 and 8. On its line, r2c3 is next to the 6 in r2c2: 3 and 8 are less than 4 away from it. **r2c3 = 1**.

**Step 3: r3c2, next to step 2's 1.** Row 3 has 4 and 5; column 2 has 2, 6, 7 and 8; box 1 has the new 1; that leaves 3 and 9. On its line, r3c2 is next to the 1 in r2c3, and 3 is only 2 away. **r3c2 = 9**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py dutch-whispers: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 52 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/dutch-whispers-walkthrough/step-00.png)

| [1](dutch-whispers/step-01.md) | [2](dutch-whispers/step-02.md) | [3](dutch-whispers/step-03.md) | [4](dutch-whispers/step-04.md) | [5](dutch-whispers/step-05.md) | [6](dutch-whispers/step-06.md) | [7](dutch-whispers/step-07.md) | [8](dutch-whispers/step-08.md) | [9](dutch-whispers/step-09.md) | [10](dutch-whispers/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](dutch-whispers/step-11.md) | [12](dutch-whispers/step-12.md) | [13](dutch-whispers/step-13.md) | [14](dutch-whispers/step-14.md) | [15](dutch-whispers/step-15.md) | [16](dutch-whispers/step-16.md) | [17](dutch-whispers/step-17.md) | [18](dutch-whispers/step-18.md) | [19](dutch-whispers/step-19.md) | [20](dutch-whispers/step-20.md) |
| [21](dutch-whispers/step-21.md) | [22](dutch-whispers/step-22.md) | [23](dutch-whispers/step-23.md) | [24](dutch-whispers/step-24.md) | [25](dutch-whispers/step-25.md) | [26](dutch-whispers/step-26.md) | [27](dutch-whispers/step-27.md) | [28](dutch-whispers/step-28.md) | [29](dutch-whispers/step-29.md) | [30](dutch-whispers/step-30.md) |
| [31](dutch-whispers/step-31.md) | [32](dutch-whispers/step-32.md) | [33](dutch-whispers/step-33.md) | [34](dutch-whispers/step-34.md) | [35](dutch-whispers/step-35.md) | [36](dutch-whispers/step-36.md) | [37](dutch-whispers/step-37.md) | [38](dutch-whispers/step-38.md) | [39](dutch-whispers/step-39.md) | [40](dutch-whispers/step-40.md) |
| [41](dutch-whispers/step-41.md) | [42](dutch-whispers/step-42.md) | [43](dutch-whispers/step-43.md) | [44](dutch-whispers/step-44.md) | [45](dutch-whispers/step-45.md) | [46](dutch-whispers/step-46.md) | [47](dutch-whispers/step-47.md) | [48](dutch-whispers/step-48.md) | [49](dutch-whispers/step-49.md) | [50](dutch-whispers/step-50.md) |
| [51](dutch-whispers/step-51.md) | [52](dutch-whispers/step-52.md) |  |  |  |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
