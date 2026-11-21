# Thermo

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Thermo: variant-examples #10](../screenshots/variants/thermo.png)

## The rules

[Classic](classic.md) rules, plus **thermometers**: grey lines with a round **bulb** at one end, through cells side by side or diagonal. Along each one, the digits **rise** from the bulb: every digit is bigger than the one before it (not necessarily by 1).

## On the board

- Each thermometer is a wide, translucent grey line with a round bulb; the digits show through.
- Two digits along one that don't rise turn red.
- The grader and hints narrow each thermometer cell from both ends: "Along the thermometer the digits rise from the bulb, so row 2, column 5 can only be 4, 5".
- **Puzzles → ⋮ → New puzzle → Thermo**: 7 thermometers of 3 to 6 cells (Hard and Expert up to 10).

## How to solve it

- **Count from the ends:** on a thermometer of 5 cells, the bulb is at most 5 and the tip at least 5; the k-th cell is at least k and at most 9 − (length − k).
- **Around a given:** cells before a given digit are smaller than it, cells after it bigger. A bulb before a 2 must be 1.
- Combine with the Classic rules: a thermometer often leaves a cell two or three options, and the row or box picks one, as in the worked example.

## Worked example

**variant-examples #10**, Thermo Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*). The thermometers are in the screenshot; two matter here, both through a 7:

- bulb **r4c6**, then r3c6 (a 7), then r2c6, going up;
- bulb **r7c6**, then r6c7 (a 7), then r5c7, going diagonally up and right.

```
. . 3 | . 4 5 | 8 2 .
. . 5 | . . . | . . 4
2 . . | . . 7 | . . .
------+-------+------
. . 7 | 3 9 . | . . .
1 . . | 6 . 4 | . . 5
. . . | . 5 1 | 7 . .
------+-------+------
. . . | 4 . . | . . 9
4 . . | . . . | 3 . .
. 3 2 | 5 8 . | 1 . .
```

**Step 1: r4c6, a bulb below a 7.** Row 4 has 7, 3 and 9; column 6 has 5, 7, 4 and 1; box 5 has 3, 9, 6, 4, 5 and 1. By the Classic rules r4c6 could be 2 or 8. It's the bulb of the thermometer through the 7 at r3c6, so it's smaller than 7. **r4c6 = 2**.

**Step 2: r5c7, the tip after a 7.** Row 5 has 1, 6, 4 and 5; column 7 has 8, 7, 3 and 1. That leaves 2 and 9. r5c7 comes after the 7 (r6c7) on its thermometer, so it's bigger than 7. **r5c7 = 9**.

**Step 3: the 3 in column 6.** Column 6 is now filled except for r2c6, r7c6, r8c6 and r9c6:

- r2c6: it comes after the 7 (r3c6) on the first thermometer, so it's 8 or 9, not 3.
- r8c6: row 8 has a 3 (r8c7).
- r9c6: row 9 has a 3 (r9c2).

Only **r7c6** is left: **r7c6 = 3**. It's the bulb of the second thermometer, and 3 is below its 7, as it should be.
