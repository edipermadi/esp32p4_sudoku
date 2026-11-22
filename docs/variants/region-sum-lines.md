# Region Sum Lines

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Region Sum Lines: variant-examples #27](../screenshots/variants/region-sum-lines.png)

## The rules

[Classic](classic.md) rules, plus **blue lines** through cells side by side or diagonal, each passing through two boxes or more. Where a line passes through a box, the digits on it there make a **part**, and **every part of a line adds up to the same total**. On a line through two boxes holding 2, 6 in one and 5, 3 in the other, both parts are 8. A box the line enters twice counts both of its stretches together as one part.

The digits in one part are in one box, so they never repeat; digits in different parts may.

## On the board

- Each line is a narrow blue line under the digits; the box borders show where its parts begin and end.
- When two of a line's parts are full and add up to different totals, or a part not yet full is already at or past a full one's total, the line's digits turn red.
- The grader and hints compare each part's smallest and biggest possible total with the others': "In each box this line passes through, its digits add up to the same, so row 6, column 6 can only be 3".
- **Puzzles → ⋮ → New puzzle → Region Sum**: 8 lines (Hard and Expert 10) of 3 to 8 cells.

## How to solve it

- **A one-cell part is the line's total:** if a line has a single cell in one box, that cell is the sum of every other part.
- **A full part gives the total:** fill one part and every other part must match it.
- **Count the cells:** a part's digits are in one box, so they never repeat: a part of two cells is at least 1 + 2 = 3, of three at least 6, of four at least 10. A one-cell part on the same line is at most 9, so a three-cell part beside it is 6 to 9 (1 2 3, 1 2 4, 1 2 5 or 1 3 4, ...).
- **Long parts make big totals:** a part of four cells is at least 10, so a one-cell part on the same line is impossible; the generator never makes one.

## Worked example

**variant-examples #27**, Region Sum Lines Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The lines are in the picture; three matter here:

- a line of **three**: r5c8 (a 2) and r5c7 (a 1) in box 6, then r6c6 in box 5;
- another line of **three**: r6c3 (a 4) and r6c2 (a 2) in box 4, then r7c2 in box 7;
- a line of **six**: r2c5 (a 9) and r2c4 in box 2, then r2c3 (a 7), r1c2, r2c2 and r1c3 in box 1.

**Step 1: r6c6, on the first line.** Row 6 has 1, 2, 4, 8 and 9; column 6 has a 6; box 5 has a 7. By the Classic rules r6c6 could be 3 or 5. Its part in box 5 is just r6c6, and the part in box 6 is already full: 2 + 1 = 3. **r6c6 = 3**.

**Step 2: r7c2, on the second line.** Row 7 has 2, 7, 8 and 9; column 2 has a 3; box 7 has a 4; that leaves 1, 5 and 6. The part in box 4 is full: 4 + 2 = 6, and r7c2 is the whole part in box 7. **r7c2 = 6**.

**Step 3: the 3 in row 2.** Row 2 has 2, 4, 7 and 9. The 3s in r8c2, r6c6 (step 1) and r4c7 rule it out of r2c2, r2c6 and r2c7. That leaves r2c1 and r2c4. r2c4 is on the line of six: the line's part in box 1 has the 7 and three more cells of that box, different digits, so it is at least 7 + 1 + 2 + 3 = 13. The part in box 2 is 9 + r2c4, so r2c4 is at least 4: not a 3. **r2c1 = 3**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py region-sum-lines: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 49 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/region-sum-lines-walkthrough/step-00.png)

#### Step 1: the 5 in box 4

Box 4 needs a 5. r4c1 and r4c2: row 4 has one (r4c8). Only **r5c1** is left: **r5c1 = 5**.

![Step 1](../screenshots/variants/region-sum-lines-walkthrough/step-01.png)

#### Step 2: the 4 in box 5

Box 5 needs a 4. r4c4 and r4c6: row 4 has one (r4c9). r6c4 and r6c6: row 6 has one (r6c3). Only **r5c5** is left: **r5c5 = 4**.

![Step 2](../screenshots/variants/region-sum-lines-walkthrough/step-02.png)

#### Step 3: the 5 in box 5

Box 5 needs a 5. r4c4 and r4c6: row 4 has one (r4c8). r6c6: its blue line adds up to 3 in another box, and a 5 would make its part in this box 5. Only **r6c4** is left: **r6c4 = 5**.

![Step 3](../screenshots/variants/region-sum-lines-walkthrough/step-03.png)

#### Step 4: the 3 in box 5

Box 5 needs a 3. r4c4 and r4c6: row 4 has one (r4c7). Only **r6c6** is left: **r6c6 = 3**.

![Step 4](../screenshots/variants/region-sum-lines-walkthrough/step-04.png)

#### Step 5: the 8 in box 6

Box 6 needs an 8. r6c8 and r6c9: row 6 has one (r6c1). Only **r5c9** is left: **r5c9 = 8**.

![Step 5](../screenshots/variants/region-sum-lines-walkthrough/step-05.png)

#### Step 6: the 7 in box 7

Box 7 needs a 7. r7c1 and r7c2: row 7 has one (r7c7). r8c3 and r9c3: column 3 has one (r2c3). r9c2: column 2 has one (r5c2). Only **r9c1** is left: **r9c1 = 7**.

![Step 6](../screenshots/variants/region-sum-lines-walkthrough/step-06.png)

#### Step 7: the 8 in box 7

Box 7 needs an 8. r7c1 and r7c2: row 7 has one (r7c5). r8c3 and r9c3: column 3 has one (r3c3). Only **r9c2** is left: **r9c2 = 8**.

![Step 7](../screenshots/variants/region-sum-lines-walkthrough/step-07.png)

#### Step 8: the 5 in box 7

Box 7 needs a 5. r7c1: column 1 has one (r5c1). r8c3: row 8 has one (r8c5). r7c2: its blue line adds up to 6 in another box, and a 5 would make its part in this box 5. Only **r9c3** is left: **r9c3 = 5**.

![Step 8](../screenshots/variants/region-sum-lines-walkthrough/step-08.png)

#### Step 9: the 9 in box 7

Box 7 needs a 9. r7c1 and r7c2: row 7 has one (r7c6). Only **r8c3** is left: **r8c3 = 9**.

![Step 9](../screenshots/variants/region-sum-lines-walkthrough/step-09.png)

#### Step 10: the 1 in box 7

Box 7 needs a 1. r7c2: its blue line adds up to 6 in another box, and a 1 would make its part in this box 1. Only **r7c1** is left: **r7c1 = 1**.

![Step 10](../screenshots/variants/region-sum-lines-walkthrough/step-10.png)

#### Step 11: the 1 in box 4

Box 4 needs a 1. r4c1: column 1 has one (r7c1). Only **r4c2** is left: **r4c2 = 1**.

![Step 11](../screenshots/variants/region-sum-lines-walkthrough/step-11.png)

#### Step 12: the 1 in box 1

Box 1 needs a 1. r1c1, r2c1 and r3c1: column 1 has one (r7c1). r1c2, r2c2 and r3c2: column 2 has one (r4c2). Only **r1c3** is left: **r1c3 = 1**.

![Step 12](../screenshots/variants/region-sum-lines-walkthrough/step-12.png)

#### Step 13: the 9 in box 4

Box 4 needs a 9, and r4c1 is its last empty cell: **r4c1 = 9**.

![Step 13](../screenshots/variants/region-sum-lines-walkthrough/step-13.png)

#### Step 14: the 4 in box 1

Box 1 needs a 4. r1c1 and r3c1: column 1 has one (r8c1). r2c1 and r2c2: row 2 has one (r2c8). r3c2: its blue line adds up to 17 in another box, and a 4 would make its part in this box 12. Only **r1c2** is left: **r1c2 = 4**.

![Step 14](../screenshots/variants/region-sum-lines-walkthrough/step-14.png)

#### Step 15: the 5 in box 1

Box 1 needs a 5. r1c1 and r2c1: column 1 has one (r5c1). r3c1 and r3c2: row 3 has one (r3c7). Only **r2c2** is left: **r2c2 = 5**.

![Step 15](../screenshots/variants/region-sum-lines-walkthrough/step-15.png)

#### Step 16: the 9 in box 1

Box 1 needs a 9. r1c1 and r3c1: column 1 has one (r4c1). r2c1: row 2 has one (r2c5). Only **r3c2** is left: **r3c2 = 9**.

![Step 16](../screenshots/variants/region-sum-lines-walkthrough/step-16.png)

#### Step 17: the 4 in box 2

Box 2 needs a 4. r1c4, r1c5 and r1c6: row 1 has one (r1c2). r2c4 and r2c6: row 2 has one (r2c8). Only **r3c6** is left: **r3c6 = 4**.

![Step 17](../screenshots/variants/region-sum-lines-walkthrough/step-17.png)

#### Step 18: the 1 in box 2

Box 2 needs a 1. r1c4, r1c5 and r1c6: row 1 has one (r1c3). r2c4: its blue line adds up to 17 in another box, and a 1 would make its part in this box 10. Only **r2c6** is left: **r2c6 = 1**.

![Step 18](../screenshots/variants/region-sum-lines-walkthrough/step-18.png)

#### Step 19: the 5 in box 2

Box 2 needs a 5. r1c4: column 4 has one (r6c4). r1c5: column 5 has one (r8c5). r2c4: row 2 has one (r2c2). Only **r1c6** is left: **r1c6 = 5**.

![Step 19](../screenshots/variants/region-sum-lines-walkthrough/step-19.png)

#### Step 20: the 6 in box 7

Box 7 needs a 6, and r7c2 is its last empty cell: **r7c2 = 6**.

![Step 20](../screenshots/variants/region-sum-lines-walkthrough/step-20.png)

#### Step 21: the 7 in box 8

Box 8 needs a 7. r7c4: row 7 has one (r7c7). r8c4: column 4 has one (r3c4). r9c4, r9c5 and r9c6: row 9 has one (r9c1). Only **r8c6** is left: **r8c6 = 7**.

![Step 21](../screenshots/variants/region-sum-lines-walkthrough/step-21.png)

#### Step 22: the 4 in box 9

Box 9 needs a 4. r7c8 and r9c8: column 8 has one (r2c8). r7c9 and r9c9: column 9 has one (r4c9). r8c8 and r8c9: row 8 has one (r8c1). Only **r9c7** is left: **r9c7 = 4**.

![Step 22](../screenshots/variants/region-sum-lines-walkthrough/step-22.png)

#### Step 23: the 4 in box 8

Box 8 needs a 4. r8c4: row 8 has one (r8c1). r9c4, r9c5 and r9c6: row 9 has one (r9c7). Only **r7c4** is left: **r7c4 = 4**.

![Step 23](../screenshots/variants/region-sum-lines-walkthrough/step-23.png)

#### Step 24: the 5 in box 9

Box 9 needs a 5. r7c8: column 8 has one (r4c8). r8c8 and r8c9: row 8 has one (r8c5). r9c8 and r9c9: row 9 has one (r9c3). Only **r7c9** is left: **r7c9 = 5**.

![Step 24](../screenshots/variants/region-sum-lines-walkthrough/step-24.png)

#### Step 25: the 8 in box 9

Box 9 needs an 8. r7c8: row 7 has one (r7c5). r8c9: column 9 has one (r5c9). r9c8 and r9c9: row 9 has one (r9c2). Only **r8c8** is left: **r8c8 = 8**.

![Step 25](../screenshots/variants/region-sum-lines-walkthrough/step-25.png)

#### Step 26: the 8 in box 3

Box 3 needs an 8. r1c8: column 8 has one (r8c8). r1c9: column 9 has one (r5c9). r3c8 and r3c9: row 3 has one (r3c3). r2c7: its blue line adds up to 10 in another box, and an 8 would make its part in this box 12. Only **r1c7** is left: **r1c7 = 8**.

![Step 26](../screenshots/variants/region-sum-lines-walkthrough/step-26.png)

#### Step 27: the 8 in box 2

Box 2 needs an 8. r1c4 and r1c5: row 1 has one (r1c7). Only **r2c4** is left: **r2c4 = 8**.

![Step 27](../screenshots/variants/region-sum-lines-walkthrough/step-27.png)

#### Step 28: the 8 in box 5

Box 5 needs an 8. r4c4: column 4 has one (r2c4). Only **r4c6** is left: **r4c6 = 8**.

![Step 28](../screenshots/variants/region-sum-lines-walkthrough/step-28.png)

#### Step 29: the 2 in box 5

Box 5 needs a 2, and r4c4 is its last empty cell: **r4c4 = 2**.

![Step 29](../screenshots/variants/region-sum-lines-walkthrough/step-29.png)

#### Step 30: the 2 in box 2

Box 2 needs a 2. r1c4: column 4 has one (r4c4). Only **r1c5** is left: **r1c5 = 2**.

![Step 30](../screenshots/variants/region-sum-lines-walkthrough/step-30.png)

#### Step 31: the 2 in box 1

Box 1 needs a 2. r1c1: row 1 has one (r1c5). r2c1: row 2 has one (r2c9). Only **r3c1** is left: **r3c1 = 2**.

![Step 31](../screenshots/variants/region-sum-lines-walkthrough/step-31.png)

#### Step 32: the 3 in box 2

Box 2 needs a 3, and r1c4 is its last empty cell: **r1c4 = 3**.

![Step 32](../screenshots/variants/region-sum-lines-walkthrough/step-32.png)

#### Step 33: the 3 in box 1

Box 1 needs a 3. r1c1: row 1 has one (r1c4). Only **r2c1** is left: **r2c1 = 3**.

![Step 33](../screenshots/variants/region-sum-lines-walkthrough/step-33.png)

#### Step 34: the 6 in box 1

Box 1 needs a 6, and r1c1 is its last empty cell: **r1c1 = 6**.

![Step 34](../screenshots/variants/region-sum-lines-walkthrough/step-34.png)

#### Step 35: the 6 in box 3

Box 3 needs a 6. r1c8 and r1c9: row 1 has one (r1c1). r3c8 and r3c9: row 3 has one (r3c5). Only **r2c7** is left: **r2c7 = 6**.

![Step 35](../screenshots/variants/region-sum-lines-walkthrough/step-35.png)

#### Step 36: the 2 in box 8

Box 8 needs a 2. r8c4: row 8 has one (r8c7). r9c4: column 4 has one (r4c4). r9c5: column 5 has one (r1c5). Only **r9c6** is left: **r9c6 = 2**.

![Step 36](../screenshots/variants/region-sum-lines-walkthrough/step-36.png)

#### Step 37: the 3 in box 8

Box 8 needs a 3. r8c4: row 8 has one (r8c2). r9c4: column 4 has one (r1c4). Only **r9c5** is left: **r9c5 = 3**.

![Step 37](../screenshots/variants/region-sum-lines-walkthrough/step-37.png)

#### Step 38: the 3 in box 9

Box 9 needs a 3. r8c9: row 8 has one (r8c2). r9c8 and r9c9: row 9 has one (r9c5). Only **r7c8** is left: **r7c8 = 3**.

![Step 38](../screenshots/variants/region-sum-lines-walkthrough/step-38.png)

#### Step 39: the 3 in box 3

Box 3 needs a 3. r1c8 and r1c9: row 1 has one (r1c4). r3c8: column 8 has one (r7c8). Only **r3c9** is left: **r3c9 = 3**.

![Step 39](../screenshots/variants/region-sum-lines-walkthrough/step-39.png)

#### Step 40: the 1 in box 3

Box 3 needs a 1. r1c8 and r1c9: row 1 has one (r1c3). Only **r3c8** is left: **r3c8 = 1**.

![Step 40](../screenshots/variants/region-sum-lines-walkthrough/step-40.png)

#### Step 41: the 6 in box 6

Box 6 needs a 6. r6c8: its blue line adds up to 16 in another box, and a 6 would make its part in this box 15. Only **r6c9** is left: **r6c9 = 6**.

![Step 41](../screenshots/variants/region-sum-lines-walkthrough/step-41.png)

#### Step 42: the 7 in box 6

Box 6 needs a 7, and r6c8 is its last empty cell: **r6c8 = 7**.

![Step 42](../screenshots/variants/region-sum-lines-walkthrough/step-42.png)

#### Step 43: the 7 in box 3

Box 3 needs a 7. r1c8: column 8 has one (r6c8). Only **r1c9** is left: **r1c9 = 7**.

![Step 43](../screenshots/variants/region-sum-lines-walkthrough/step-43.png)

#### Step 44: the 9 in box 3

Box 3 needs a 9, and r1c8 is its last empty cell: **r1c8 = 9**.

![Step 44](../screenshots/variants/region-sum-lines-walkthrough/step-44.png)

#### Step 45: the 6 in box 9

Box 9 needs a 6. r8c9 and r9c9: column 9 has one (r6c9). Only **r9c8** is left: **r9c8 = 6**.

![Step 45](../screenshots/variants/region-sum-lines-walkthrough/step-45.png)

#### Step 46: the 6 in box 8

Box 8 needs a 6. r9c4: row 9 has one (r9c8). Only **r8c4** is left: **r8c4 = 6**.

![Step 46](../screenshots/variants/region-sum-lines-walkthrough/step-46.png)

#### Step 47: the 1 in box 8

Box 8 needs a 1, and r9c4 is its last empty cell: **r9c4 = 1**.

![Step 47](../screenshots/variants/region-sum-lines-walkthrough/step-47.png)

#### Step 48: the 1 in box 9

Box 9 needs a 1. r9c9: row 9 has one (r9c4). Only **r8c9** is left: **r8c9 = 1**.

![Step 48](../screenshots/variants/region-sum-lines-walkthrough/step-48.png)

#### Step 49: the 9 in box 9

Box 9 needs a 9, and r9c9 is its last empty cell: **r9c9 = 9**.

![Step 49](../screenshots/variants/region-sum-lines-walkthrough/step-49.png)

Solved.

<!-- /walkthrough -->
