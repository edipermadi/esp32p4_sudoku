# Hyper

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Hyper: variant-examples #4](../screenshots/variants/hyper.png)

## The rules

[Classic](classic.md) rules, and four extra 3×3 **windows** must also hold 1 to 9, each once. The windows sit one cell in from the edges, between the boxes: rows 2–4 and 6–8 crossed with columns 2–4 and 6–8. Also known as Windoku.

## On the board

- The 36 window cells are tinted, like the X-Sudoku diagonals.
- A repeat in a window turns red; hints name the windows ("Look at the top-left window").
- **Puzzles → ⋮ → New puzzle → Hyper**; it's also Thursday's daily puzzle (Hard).

## How to solve it

- Each window is a tenth to thirteenth "box": a hidden single can be in a window.
- A window overlaps four boxes, so the cells *outside* a window in those boxes are tied to it: if a box's digit must be inside the window, it can't be anywhere else in the window, and the other way round. That gives hidden singles in the thin strips between windows.
- A cell inside a window has 26 neighbours instead of 20, so window cells run out of candidates first: look there for naked singles.

## Worked example

**variant-examples #4**, Hyper Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*).

The top-left window is rows 2–4, columns 2–4; the top-right one is rows 2–4, columns 6–8.

**Step 1: r2c4, in the top-left window.** Row 2 has 3 and 9; column 4 has 5, 2, 6 and 1; box 2 has 5, 9 and 2. By the Classic rules r2c4 could still be 4, 7 or 8. But it's in the top-left window, which has a 7 (r4c3) and an 8 (r3c3). **r2c4 = 4**.

**Step 2: r2c7, in the top-right window.** Row 2 now has 3, 9 and 4; column 7 has 9, 6, 2, 8 and 7; box 3 has 8, 9 and 4. That leaves 1 or 5, and the top-right window has a 5 (r4c8). **r2c7 = 1**.

**Step 3: r2c8, in the top-right window too.** Row 2 has 3, 9, 4 and 1; column 8 has 4, 5, 9 and 8; so 2, 6 or 7 are left. The window has a 2 (r4c6) and a 6 (r4c7). **r2c8 = 7**.

Three cells in a row, each settled by a window: that's typical of Hyper, where the windows do as much work as the boxes.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py hyper: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 43 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/hyper-walkthrough/step-00.png)

| [1](hyper/step-01.md) | [2](hyper/step-02.md) | [3](hyper/step-03.md) | [4](hyper/step-04.md) | [5](hyper/step-05.md) | [6](hyper/step-06.md) | [7](hyper/step-07.md) | [8](hyper/step-08.md) | [9](hyper/step-09.md) | [10](hyper/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](hyper/step-11.md) | [12](hyper/step-12.md) | [13](hyper/step-13.md) | [14](hyper/step-14.md) | [15](hyper/step-15.md) | [16](hyper/step-16.md) | [17](hyper/step-17.md) | [18](hyper/step-18.md) | [19](hyper/step-19.md) | [20](hyper/step-20.md) |
| [21](hyper/step-21.md) | [22](hyper/step-22.md) | [23](hyper/step-23.md) | [24](hyper/step-24.md) | [25](hyper/step-25.md) | [26](hyper/step-26.md) | [27](hyper/step-27.md) | [28](hyper/step-28.md) | [29](hyper/step-29.md) | [30](hyper/step-30.md) |
| [31](hyper/step-31.md) | [32](hyper/step-32.md) | [33](hyper/step-33.md) | [34](hyper/step-34.md) | [35](hyper/step-35.md) | [36](hyper/step-36.md) | [37](hyper/step-37.md) | [38](hyper/step-38.md) | [39](hyper/step-39.md) | [40](hyper/step-40.md) |
| [41](hyper/step-41.md) | [42](hyper/step-42.md) | [43](hyper/step-43.md) |  |  |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
