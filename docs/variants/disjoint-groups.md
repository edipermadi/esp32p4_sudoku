# Disjoint Groups

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Disjoint Groups: variant-examples #15](../screenshots/variants/disjoint-groups.png)

## The rules

[Classic](classic.md) rules, and the nine cells **in the same place in each 3×3 box** also hold 1 to 9, each once: the nine top-left corners of the boxes, the nine centres, the nine bottom-right corners, and so on. That's nine more groups of nine cells, spread out over the whole grid.

## On the board

- The groups aren't drawn (they'd cover the board); selecting a cell highlights its group along with its row, column and box.
- A repeat within a group turns red; hints name the groups by their place in the box: "Look at the centre cells of the boxes".
- The rules card shows two boxes with the same place tinted in each.
- **Puzzles → ⋮ → New puzzle → Disjoint Groups**.

## How to solve it

- **Find a cell's group:** its place inside its box. Row 5 is the middle row of its box and column 6 the right column, so r5c6 is in the "middle-right" group with r2c3, r2c6, r2c9, r5c3, r5c9, r8c3, r8c6 and r8c9: the same row-in-box and column-in-box everywhere.
- Each cell now has 24 neighbours instead of 20 (four of its group are new; the others already share its row or column), so naked singles come sooner.
- A group is like a tenth "box" scattered over the grid: a hidden single can be in a group too.

## Worked example

**variant-examples #15**, Disjoint Groups Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*).

**Step 1: r5c6, in the middle-right group.** Row 5 has a 6; column 6 has 4, 7, 3 and 8; box 5 has 7, 6 and 3. By the Classic rules r5c6 could be 1, 2, 5 or 9. Its group (the middle-right cell of every box: r2c3, r2c6, r2c9, r5c3, r5c9, r8c3, r8c6, r8c9) has a 5 (r2c3), a 1 (r2c9) and a 9 (r8c3). **r5c6 = 2**.

**Step 2: r4c5, in the top-middle group.** Row 4 has 9, 1 and 7; column 5 has 7, 6 and 4; box 5 has 7, 6, 3 and the new 2. That leaves 5 and 8. Its group (the top-middle cell of every box: r1c2, r1c5, r1c8, r4c2, r4c8, r7c2, r7c5, r7c8) has a 5 (r7c8). **r4c5 = 8**.

**Step 3: r6c5, in the bottom-middle group.** Row 6 has 3, 9 and 6; column 5 has 7, 6, 4 and the new 8; box 5 has 7, 6, 3, 2 and 8. That leaves 1 and 5. Its group (the bottom-middle cell of every box: r3c2, r3c5, r3c8, r6c2, r6c8, r9c2, r9c5, r9c8) has a 5 (r9c2). **r6c5 = 1**.

Each time the Classic rules got a cell down to a few digits, and a group far away across the grid settled it.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py disjoint-groups: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 48 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/disjoint-groups-walkthrough/step-00.png)

| [1](disjoint-groups/step-01.md) | [2](disjoint-groups/step-02.md) | [3](disjoint-groups/step-03.md) | [4](disjoint-groups/step-04.md) | [5](disjoint-groups/step-05.md) | [6](disjoint-groups/step-06.md) | [7](disjoint-groups/step-07.md) | [8](disjoint-groups/step-08.md) | [9](disjoint-groups/step-09.md) | [10](disjoint-groups/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](disjoint-groups/step-11.md) | [12](disjoint-groups/step-12.md) | [13](disjoint-groups/step-13.md) | [14](disjoint-groups/step-14.md) | [15](disjoint-groups/step-15.md) | [16](disjoint-groups/step-16.md) | [17](disjoint-groups/step-17.md) | [18](disjoint-groups/step-18.md) | [19](disjoint-groups/step-19.md) | [20](disjoint-groups/step-20.md) |
| [21](disjoint-groups/step-21.md) | [22](disjoint-groups/step-22.md) | [23](disjoint-groups/step-23.md) | [24](disjoint-groups/step-24.md) | [25](disjoint-groups/step-25.md) | [26](disjoint-groups/step-26.md) | [27](disjoint-groups/step-27.md) | [28](disjoint-groups/step-28.md) | [29](disjoint-groups/step-29.md) | [30](disjoint-groups/step-30.md) |
| [31](disjoint-groups/step-31.md) | [32](disjoint-groups/step-32.md) | [33](disjoint-groups/step-33.md) | [34](disjoint-groups/step-34.md) | [35](disjoint-groups/step-35.md) | [36](disjoint-groups/step-36.md) | [37](disjoint-groups/step-37.md) | [38](disjoint-groups/step-38.md) | [39](disjoint-groups/step-39.md) | [40](disjoint-groups/step-40.md) |
| [41](disjoint-groups/step-41.md) | [42](disjoint-groups/step-42.md) | [43](disjoint-groups/step-43.md) | [44](disjoint-groups/step-44.md) | [45](disjoint-groups/step-45.md) | [46](disjoint-groups/step-46.md) | [47](disjoint-groups/step-47.md) | [48](disjoint-groups/step-48.md) |  |  |

Solved.

<!-- /walkthrough -->
