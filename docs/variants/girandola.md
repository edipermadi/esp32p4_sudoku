# Girandola

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Girandola: variant-examples #51](../screenshots/variants/girandola.png)

## The rules

[Classic](classic.md) rules, and **nine tinted cells in a pinwheel pattern** also hold 1 to 9, each once: r1c1, r1c9, r2c5, r5c2, r5c5, r5c8, r8c5, r9c1 and r9c9. There's one in each box.

## On the board

- The nine cells are tinted, like the Asterisk's and Center Dot's.
- Two of the same digit among them turn red, as in a row.
- The hints count the pinwheel as one more group: "the row, column, box or the pinwheel".
- **Puzzles → ⋮ → New puzzle → Grids → Girandola.**

## How to solve it

- **Scan the pinwheel like a row:** a digit placed in one of its cells is out of the other eight.
- **Hidden singles in it:** with most of its digits placed, a missing one often has one tinted cell left.
- **The corners see a lot:** r1c1 shares row 1 with r1c9 and column 1 with r9c1, so between them, the corners see five of the pinwheel's cells without it.

## Worked example

**variant-examples #51**, Girandola Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*).

**Step 1: the 3 in box 1**, by the Classic rules: row 1 has a 3 (r1c7), row 3 one (r3c4). **r2c3 = 3**.

**Step 2: the 9 in box 1**: row 1 has a 9 (r1c6), column 3 one (r9c3). **r3c1 = 9**.

**Step 3: the 4 in box 1.** r1c1 is a pinwheel cell, and the pinwheel has a 4 already (r2c5); r1c3 and r3c3: column 3 has one (r7c3). Only **r1c2** is left: **r1c2 = 4**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py girandola: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 45 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/girandola-walkthrough/step-00.png)

| [1](girandola/step-01.md) | [2](girandola/step-02.md) | [3](girandola/step-03.md) | [4](girandola/step-04.md) | [5](girandola/step-05.md) | [6](girandola/step-06.md) | [7](girandola/step-07.md) | [8](girandola/step-08.md) | [9](girandola/step-09.md) | [10](girandola/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](girandola/step-11.md) | [12](girandola/step-12.md) | [13](girandola/step-13.md) | [14](girandola/step-14.md) | [15](girandola/step-15.md) | [16](girandola/step-16.md) | [17](girandola/step-17.md) | [18](girandola/step-18.md) | [19](girandola/step-19.md) | [20](girandola/step-20.md) |
| [21](girandola/step-21.md) | [22](girandola/step-22.md) | [23](girandola/step-23.md) | [24](girandola/step-24.md) | [25](girandola/step-25.md) | [26](girandola/step-26.md) | [27](girandola/step-27.md) | [28](girandola/step-28.md) | [29](girandola/step-29.md) | [30](girandola/step-30.md) |
| [31](girandola/step-31.md) | [32](girandola/step-32.md) | [33](girandola/step-33.md) | [34](girandola/step-34.md) | [35](girandola/step-35.md) | [36](girandola/step-36.md) | [37](girandola/step-37.md) | [38](girandola/step-38.md) | [39](girandola/step-39.md) | [40](girandola/step-40.md) |
| [41](girandola/step-41.md) | [42](girandola/step-42.md) | [43](girandola/step-43.md) | [44](girandola/step-44.md) | [45](girandola/step-45.md) |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
