# Entropic Lines

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Entropic Lines: variant-examples #32](../screenshots/variants/entropic-lines.png)

## The rules

[Classic](classic.md) rules, plus **orange lines** through cells side by side or diagonal. The digits fall into three groups: **low** (1, 2, 3), **middle** (4, 5, 6) and **high** (7, 8, 9). **Any three cells in a row on a line hold one digit of each group**, in any order: 2, 9, 4, 1, 8 works, since 2 9 4, 9 4 1 and 4 1 8 each have a low, a middle and a high digit.

It follows that **cells three apart on a line are in the same group**: on 2, 9, 4, 1, 8 the 2 and the 1 are both low, the 9 and the 8 both high.

## On the board

- Each line is a narrow orange line under the digits.
- Two digits on a line that break the rule turn red: three apart in different groups, or closer together in the same group.
- The grader and hints work out, for each third of the line (its 1st, 4th, 7th cells; its 2nd, 5th, 8th; its 3rd, 6th, 9th), which groups it can still be, and give the thirds three different groups: "Any three cells in a row on this line hold a low (1-3), a middle (4-6) and a high (7-9) digit, so row 4, column 2 can only be 1".
- **Puzzles → ⋮ → New puzzle → Lines → Entropic**: 8 lines (Hard and Expert 10) of 3 to 8 cells.

## How to solve it

- **Every third cell matches:** once a cell's group is known, so is the group of every cell 3, 6 ... along from it.
- **Two groups known, the third follows:** if two cells side by side are low and high, the cells next to them are middle.
- **Each group is three digits:** a cell known to be middle is 4, 5 or 6, and its row, column and box often pick one.
- **Short lines say the least:** a line of three only says its cells are one of each group.

## Worked example

**variant-examples #32**, Entropic Lines Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The lines are in the screenshot; two matter here:

- a line of **seven**: r8c6, r9c5, r8c4 (a 6), r8c3, r7c3, r6c3 and r5c4 (a 9);
- a line of **eight**: r6c2, r6c1, r5c1 (a 5), r4c2 (a 1), r3c2, r2c2 (a 5), r3c1 (a 2) and r4c1 (a 9).

**Step 1: r8c6, the start of the line of seven.** Row 8 has 2, 4, 6 and 8; column 6 has 1, 3, 6 and 9: that leaves 5 and 7. It is the line's 1st cell; its 7th, six cells along, is a 9: high. So r8c6 is high too. **r8c6 = 7**.

**Step 2: r3c2, the 5th cell of the line of eight.** Row 3 has 2 and 7; column 2 has 1, 4, 5 and 8: that leaves 3, 6 and 9. Three cells along is the 8th, r4c1, a 9: high. **r3c2 = 9**.

**Step 3: r6c3, the 6th cell of the line of seven.** Row 6 has 1, 3, 6 and 7; box 4 has 1, 5 and 9: that leaves 2, 4 and 8. Three cells back is the 3rd, r8c4, a 6: middle. **r6c3 = 4**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py entropic-lines: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 51 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick its number below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/entropic-lines-walkthrough/step-00.png)

| [1](entropic-lines/step-01.md) | [2](entropic-lines/step-02.md) | [3](entropic-lines/step-03.md) | [4](entropic-lines/step-04.md) | [5](entropic-lines/step-05.md) | [6](entropic-lines/step-06.md) | [7](entropic-lines/step-07.md) | [8](entropic-lines/step-08.md) | [9](entropic-lines/step-09.md) | [10](entropic-lines/step-10.md) |
|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|
| [11](entropic-lines/step-11.md) | [12](entropic-lines/step-12.md) | [13](entropic-lines/step-13.md) | [14](entropic-lines/step-14.md) | [15](entropic-lines/step-15.md) | [16](entropic-lines/step-16.md) | [17](entropic-lines/step-17.md) | [18](entropic-lines/step-18.md) | [19](entropic-lines/step-19.md) | [20](entropic-lines/step-20.md) |
| [21](entropic-lines/step-21.md) | [22](entropic-lines/step-22.md) | [23](entropic-lines/step-23.md) | [24](entropic-lines/step-24.md) | [25](entropic-lines/step-25.md) | [26](entropic-lines/step-26.md) | [27](entropic-lines/step-27.md) | [28](entropic-lines/step-28.md) | [29](entropic-lines/step-29.md) | [30](entropic-lines/step-30.md) |
| [31](entropic-lines/step-31.md) | [32](entropic-lines/step-32.md) | [33](entropic-lines/step-33.md) | [34](entropic-lines/step-34.md) | [35](entropic-lines/step-35.md) | [36](entropic-lines/step-36.md) | [37](entropic-lines/step-37.md) | [38](entropic-lines/step-38.md) | [39](entropic-lines/step-39.md) | [40](entropic-lines/step-40.md) |
| [41](entropic-lines/step-41.md) | [42](entropic-lines/step-42.md) | [43](entropic-lines/step-43.md) | [44](entropic-lines/step-44.md) | [45](entropic-lines/step-45.md) | [46](entropic-lines/step-46.md) | [47](entropic-lines/step-47.md) | [48](entropic-lines/step-48.md) | [49](entropic-lines/step-49.md) | [50](entropic-lines/step-50.md) |
| [51](entropic-lines/step-51.md) |  |  |  |  |  |  |  |  |  |

Solved.

<!-- /walkthrough -->
