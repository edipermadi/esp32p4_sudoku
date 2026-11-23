# Extra Regions

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Extra Regions: variant-examples #54](../screenshots/variants/extra-regions.png)

## The rules

[Classic](classic.md) rules, plus a few **shaded regions** of 5 to 9 cells: **no digit repeats in a region**. A region reaches over box borders (and across rows and columns), so it ties together cells that otherwise don't see each other. Regions never touch, not even at a corner, so each shaded patch is one region.

## On the board

- The regions are tinted, like the Asterisk's or Hyper's cells.
- Two of the same digit in a region turn red, as in a row.
- The hints count a region as one more group: "the row, column, box or shaded region".
- **Puzzles → ⋮ → New puzzle → Grids → Extra Regions**: 5 regions (Hard and Expert: up to 6).

## How to solve it

- **Scan a region like a short row:** a digit placed in it rules that digit out of its other cells, in whatever box they are.
- **Hidden singles across the border:** a region often leaves a box with one place for a digit.
- **Pairs travel:** two cells of a region that can only be 3 or 7 keep 3 and 7 out of the rest of the region.

## Worked example

**variant-examples #54**, Extra Regions Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*).

**Step 1: the 9 in box 9.** r7c9 and r9c9 are filled. r8c7 to r8c9: row 8 has a 9 (r8c5). r7c8 and r9c8: column 8 has one (r5c8). r7c7 is in a shaded region with r5c8, which holds 9. Only **r9c7** is left: **r9c7 = 9**.

**Step 2: the 2 in box 1**, by the Classic rules: column 2 has a 2 (r7c2), row 2 one (r2c5), row 3 one (r3c8). **r1c3 = 2**.

**Step 3: the 9 in box 4**: row 4 has a 9 (r4c4), row 5 one (r5c8). **r6c3 = 9**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py extra-regions: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 54 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/extra-regions-walkthrough/step-00.png)

| [1](extra-regions/step-01.md) | [2](extra-regions/step-02.md) | [3](extra-regions/step-03.md) | [4](extra-regions/step-04.md) | [5](extra-regions/step-05.md) | [6](extra-regions/step-06.md) | [7](extra-regions/step-07.md) | [8](extra-regions/step-08.md) | [9](extra-regions/step-09.md) | [10](extra-regions/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](extra-regions/step-11.md) | [12](extra-regions/step-12.md) | [13](extra-regions/step-13.md) | [14](extra-regions/step-14.md) | [15](extra-regions/step-15.md) | [16](extra-regions/step-16.md) | [17](extra-regions/step-17.md) | [18](extra-regions/step-18.md) | [19](extra-regions/step-19.md) | [20](extra-regions/step-20.md) |
| [21](extra-regions/step-21.md) | [22](extra-regions/step-22.md) | [23](extra-regions/step-23.md) | [24](extra-regions/step-24.md) | [25](extra-regions/step-25.md) | [26](extra-regions/step-26.md) | [27](extra-regions/step-27.md) | [28](extra-regions/step-28.md) | [29](extra-regions/step-29.md) | [30](extra-regions/step-30.md) |
| [31](extra-regions/step-31.md) | [32](extra-regions/step-32.md) | [33](extra-regions/step-33.md) | [34](extra-regions/step-34.md) | [35](extra-regions/step-35.md) | [36](extra-regions/step-36.md) | [37](extra-regions/step-37.md) | [38](extra-regions/step-38.md) | [39](extra-regions/step-39.md) | [40](extra-regions/step-40.md) |
| [41](extra-regions/step-41.md) | [42](extra-regions/step-42.md) | [43](extra-regions/step-43.md) | [44](extra-regions/step-44.md) | [45](extra-regions/step-45.md) | [46](extra-regions/step-46.md) | [47](extra-regions/step-47.md) | [48](extra-regions/step-48.md) | [49](extra-regions/step-49.md) | [50](extra-regions/step-50.md) |
| [51](extra-regions/step-51.md) | [52](extra-regions/step-52.md) | [53](extra-regions/step-53.md) | [54](extra-regions/step-54.md) |  |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
