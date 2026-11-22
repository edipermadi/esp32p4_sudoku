# Modular Lines

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Modular Lines: variant-examples #33](../screenshots/variants/modular-lines.png)

## The rules

[Classic](classic.md) rules, plus **teal lines** through cells side by side or diagonal. The digits fall into three groups by their remainder when divided by 3: **1 4 7**, **2 5 8** and **3 6 9**. **Any three cells in a row on a line hold one digit of each group**, in any order: 1, 5, 3, 7, 2 works, since 1 5 3, 5 3 7 and 3 7 2 each have one of 1 4 7, one of 2 5 8 and one of 3 6 9.

As with [Entropic Lines](entropic-lines.md), **cells three apart on a line are in the same group**: on 1, 5, 3, 7, 2 the 1 and the 7 are both of 1 4 7.

## On the board

- Each line is a narrow teal line under the digits.
- Two digits on a line that break the rule turn red: three apart in different groups, or closer together in the same group.
- The grader and hints work out, for each third of the line, which groups it can still be, and give the thirds three different groups: "Any three cells in a row on this line hold one of 1 4 7, one of 2 5 8 and one of 3 6 9, so ...".
- **Puzzles → ⋮ → New puzzle → Lines → Modular**: 8 lines (Hard and Expert 10) of 3 to 8 cells.

## How to solve it

- **Every third cell matches:** once a cell's group is known, so is the group of every cell 3, 6 ... along from it.
- **Two groups known, the third follows:** next to a 4 and a 9 on a line, the third cell is 2, 5 or 8.
- **The groups cut across low and high:** each holds a low, a middle and a high digit, so a group alone never says "big" or "small"; rows, columns and boxes pick the digit.

## Worked example

**variant-examples #33**, Modular Lines Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The lines are in the screenshot; three matter here:

- a line of **three**: r6c2, r5c3 (an 8) and r4c4;
- a line of **five**: r7c1, r8c1, r8c2, r7c2 and r8c3 (a 1);
- another line of **five**: r7c6, r6c6, r6c5, r5c4 (a 6) and r5c5 (a 7).

**Step 1: r4c4, on the line of three.** Row 4 has 1 and 7; column 4 has 2, 5, 6 and 9; box 5 has 4: that leaves 3 and 8. The line's three cells hold one of each group, and r5c3's 8 is the line's 2 5 8. **r4c4 = 3**.

**Step 2: r8c2, the 3rd cell of the first line of five.** Row 8 has 1, 3, 5 and 6; column 2 has 2, 7 and 8: that leaves 4 and 9. Two cells along is r8c3, a 1, in the same three-in-a-row: r8c2 can't be of 1 4 7 too. **r8c2 = 9**.

**Step 3: r6c6, the 2nd cell of the other line of five.** Row 6 has 3 and 7; column 6 has 4, 5, 6 and 9: that leaves 1, 2 and 8. Three cells along is its 5th, r5c5, a 7: of 1 4 7. **r6c6 = 1**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py modular-lines: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 52 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/modular-lines-walkthrough/step-00.png)

#### Step 1: the 9 in box 1

Box 1 needs a 9. r1c1, r1c2 and r1c3: row 1 has one (r1c4). Only **r2c1** is left: **r2c1 = 9**.

![Step 1](../screenshots/variants/modular-lines-walkthrough/step-01.png)

#### Step 2: the 9 in box 3

Box 3 needs a 9. r1c7, r1c8 and r1c9: row 1 has one (r1c4). r2c8 and r2c9: row 2 has one (r2c1). r3c7: column 7 has one (r5c7). r3c9: column 9 has one (r7c9). Only **r3c8** is left: **r3c8 = 9**.

![Step 2](../screenshots/variants/modular-lines-walkthrough/step-02.png)

#### Step 3: the 9 in box 5

Box 5 needs a 9. r4c4 and r6c4: column 4 has one (r1c4). r4c6 and r6c6: column 6 has one (r9c6). r6c5: next to it on its teal line there's one of 3 6 9 and one of 1 4 7 already, and three in a row need one of each. Only **r4c5** is left: **r4c5 = 9**.

![Step 3](../screenshots/variants/modular-lines-walkthrough/step-03.png)

#### Step 4: the 3 in box 5

Box 5 needs a 3. r6c4, r6c5 and r6c6: row 6 has one (r6c1). r4c6: next to it on its teal line there's one of 3 6 9 and one of 1 4 7 already, and three in a row need one of each. Only **r4c4** is left: **r4c4 = 3**.

![Step 4](../screenshots/variants/modular-lines-walkthrough/step-04.png)

#### Step 5: the 9 in box 4

Box 4 needs a 9. r4c1 and r4c2: row 4 has one (r4c5). r5c1: row 5 has one (r5c7). r6c2: next to it on its teal line there's one of 3 6 9 and one of 2 5 8 already, and three in a row need one of each. Only **r6c3** is left: **r6c3 = 9**.

![Step 5](../screenshots/variants/modular-lines-walkthrough/step-05.png)

#### Step 6: the 5 in box 5

Box 5 needs a 5. r4c6 and r6c6: column 6 has one (r1c6). r6c4: column 4 has one (r9c4). Only **r6c5** is left: **r6c5 = 5**.

![Step 6](../screenshots/variants/modular-lines-walkthrough/step-06.png)

#### Step 7: the 2 in box 5

Box 5 needs a 2. r6c4: column 4 has one (r2c4). r6c6: it's 3 cells from the 7 in r5c5 along its teal line, so it's one of 1 4 7 like it. Only **r4c6** is left: **r4c6 = 2**.

![Step 7](../screenshots/variants/modular-lines-walkthrough/step-07.png)

#### Step 8: the 8 in box 5

Box 5 needs an 8. r6c6: it's 3 cells from the 7 in r5c5 along its teal line, so it's one of 1 4 7 like it. Only **r6c4** is left: **r6c4 = 8**.

![Step 8](../screenshots/variants/modular-lines-walkthrough/step-08.png)

#### Step 9: the 1 in box 5

Box 5 needs a 1, and r6c6 is its last empty cell: **r6c6 = 1**.

![Step 9](../screenshots/variants/modular-lines-walkthrough/step-09.png)

#### Step 10: the 1 in box 4

Box 4 needs a 1. r4c1 and r4c2: row 4 has one (r4c9). r6c2: row 6 has one (r6c6). Only **r5c1** is left: **r5c1 = 1**.

![Step 10](../screenshots/variants/modular-lines-walkthrough/step-10.png)

#### Step 11: the 1 in box 1

Box 1 needs a 1. r1c1: column 1 has one (r5c1). r1c3: column 3 has one (r8c3). Only **r1c2** is left: **r1c2 = 1**.

![Step 11](../screenshots/variants/modular-lines-walkthrough/step-11.png)

#### Step 12: the 3 in box 1

Box 1 needs a 3. r1c1: column 1 has one (r6c1). Only **r1c3** is left: **r1c3 = 3**.

![Step 12](../screenshots/variants/modular-lines-walkthrough/step-12.png)

#### Step 13: the 6 in box 1

Box 1 needs a 6, and r1c1 is its last empty cell: **r1c1 = 6**.

![Step 13](../screenshots/variants/modular-lines-walkthrough/step-13.png)

#### Step 14: the 6 in box 2

Box 2 needs a 6. r1c5: row 1 has one (r1c1). r2c5 and r2c6: row 2 has one (r2c7). r3c4: column 4 has one (r5c4). r3c6: column 6 has one (r8c6). Only **r3c5** is left: **r3c5 = 6**.

![Step 14](../screenshots/variants/modular-lines-walkthrough/step-14.png)

#### Step 15: the 6 in box 4

Box 4 needs a 6. r4c1: column 1 has one (r1c1). r6c2: next to it on its teal line there's one of 3 6 9 and one of 2 5 8 already, and three in a row need one of each. Only **r4c2** is left: **r4c2 = 6**.

![Step 15](../screenshots/variants/modular-lines-walkthrough/step-15.png)

#### Step 16: the 5 in box 4

Box 4 needs a 5. r6c2: row 6 has one (r6c5). Only **r4c1** is left: **r4c1 = 5**.

![Step 16](../screenshots/variants/modular-lines-walkthrough/step-16.png)

#### Step 17: the 4 in box 4

Box 4 needs a 4, and r6c2 is its last empty cell: **r6c2 = 4**.

![Step 17](../screenshots/variants/modular-lines-walkthrough/step-17.png)

#### Step 18: the 2 in box 6

Box 6 needs a 2. r4c7 and r4c8: row 4 has one (r4c6). r5c9: row 5 has one (r5c2). r6c8: it's 3 cells from the 9 in r5c7 along its teal line, so it's one of 3 6 9 like it. Only **r6c9** is left: **r6c9 = 2**.

![Step 18](../screenshots/variants/modular-lines-walkthrough/step-18.png)

#### Step 19: the 2 in box 3

Box 3 needs a 2. r1c7: column 7 has one (r7c7). r1c9: column 9 has one (r6c9). r2c8 and r2c9: row 2 has one (r2c4). r3c7 and r3c9: row 3 has one (r3c1). Only **r1c8** is left: **r1c8 = 2**.

![Step 19](../screenshots/variants/modular-lines-walkthrough/step-19.png)

#### Step 20: the 4 in box 6

Box 6 needs a 4. r5c9: row 5 has one (r5c6). r6c8: row 6 has one (r6c2). r4c8: it's 3 cells from the 2 in r7c7 along its teal line, so it's one of 2 5 8 like it. Only **r4c7** is left: **r4c7 = 4**.

![Step 20](../screenshots/variants/modular-lines-walkthrough/step-20.png)

#### Step 21: the 5 in box 6

Box 6 needs a 5. r4c8: row 4 has one (r4c1). r6c8: row 6 has one (r6c5). Only **r5c9** is left: **r5c9 = 5**.

![Step 21](../screenshots/variants/modular-lines-walkthrough/step-21.png)

#### Step 22: the 5 in box 3

Box 3 needs a 5. r1c7 and r1c9: row 1 has one (r1c6). r2c8 and r2c9: row 2 has one (r2c3). r3c9: column 9 has one (r5c9). Only **r3c7** is left: **r3c7 = 5**.

![Step 22](../screenshots/variants/modular-lines-walkthrough/step-22.png)

#### Step 23: the 1 in box 3

Box 3 needs a 1. r1c7 and r1c9: row 1 has one (r1c2). r2c9 and r3c9: column 9 has one (r4c9). Only **r2c8** is left: **r2c8 = 1**.

![Step 23](../screenshots/variants/modular-lines-walkthrough/step-23.png)

#### Step 24: the 1 in box 2

Box 2 needs a 1. r1c5: row 1 has one (r1c2). r2c5 and r2c6: row 2 has one (r2c8). r3c6: column 6 has one (r6c6). Only **r3c4** is left: **r3c4 = 1**.

![Step 24](../screenshots/variants/modular-lines-walkthrough/step-24.png)

#### Step 25: the 7 in box 2

Box 2 needs a 7. r1c5: column 5 has one (r5c5). r2c5 and r2c6: row 2 has one (r2c2). Only **r3c6** is left: **r3c6 = 7**.

![Step 25](../screenshots/variants/modular-lines-walkthrough/step-25.png)

#### Step 26: the 7 in box 3

Box 3 needs a 7. r1c7: column 7 has one (r6c7). r2c9: row 2 has one (r2c2). r3c9: row 3 has one (r3c6). Only **r1c9** is left: **r1c9 = 7**.

![Step 26](../screenshots/variants/modular-lines-walkthrough/step-26.png)

#### Step 27: the 4 in box 3

Box 3 needs a 4. r1c7: column 7 has one (r4c7). r3c9: row 3 has one (r3c3). Only **r2c9** is left: **r2c9 = 4**.

![Step 27](../screenshots/variants/modular-lines-walkthrough/step-27.png)

#### Step 28: the 4 in box 2

Box 2 needs a 4. r2c5 and r2c6: row 2 has one (r2c9). Only **r1c5** is left: **r1c5 = 4**.

![Step 28](../screenshots/variants/modular-lines-walkthrough/step-28.png)

#### Step 29: the 3 in box 3

Box 3 needs a 3. r1c7: row 1 has one (r1c3). Only **r3c9** is left: **r3c9 = 3**.

![Step 29](../screenshots/variants/modular-lines-walkthrough/step-29.png)

#### Step 30: the 8 in box 3

Box 3 needs an 8, and r1c7 is its last empty cell: **r1c7 = 8**.

![Step 30](../screenshots/variants/modular-lines-walkthrough/step-30.png)

#### Step 31: the 6 in box 6

Box 6 needs a 6. r4c8: row 4 has one (r4c2). Only **r6c8** is left: **r6c8 = 6**.

![Step 31](../screenshots/variants/modular-lines-walkthrough/step-31.png)

#### Step 32: the 8 in box 6

Box 6 needs an 8, and r4c8 is its last empty cell: **r4c8 = 8**.

![Step 32](../screenshots/variants/modular-lines-walkthrough/step-32.png)

#### Step 33: the 2 in box 7

Box 7 needs a 2. r7c1, r7c2 and r7c3: row 7 has one (r7c7). r8c1 and r9c1: column 1 has one (r3c1). r8c2 and r9c2: column 2 has one (r5c2). Only **r9c3** is left: **r9c3 = 2**.

![Step 33](../screenshots/variants/modular-lines-walkthrough/step-33.png)

#### Step 34: the 5 in box 7

Box 7 needs a 5. r7c1: column 1 has one (r4c1). r7c3: column 3 has one (r2c3). r8c1 and r8c2: row 8 has one (r8c8). r9c1 and r9c2: row 9 has one (r9c4). Only **r7c2** is left: **r7c2 = 5**.

![Step 34](../screenshots/variants/modular-lines-walkthrough/step-34.png)

#### Step 35: the 3 in box 7

Box 7 needs a 3. r7c1 and r9c1: column 1 has one (r6c1). r7c3: column 3 has one (r1c3). r8c1 and r8c2: row 8 has one (r8c7). Only **r9c2** is left: **r9c2 = 3**.

![Step 35](../screenshots/variants/modular-lines-walkthrough/step-35.png)

#### Step 36: the 6 in box 7

Box 7 needs a 6. r7c1 and r9c1: column 1 has one (r1c1). r8c1 and r8c2: row 8 has one (r8c6). Only **r7c3** is left: **r7c3 = 6**.

![Step 36](../screenshots/variants/modular-lines-walkthrough/step-36.png)

#### Step 37: the 8 in box 7

Box 7 needs an 8. r8c2: column 2 has one (r3c2). r8c1: it's 3 cells from the 1 in r8c3 along its teal line, so it's one of 1 4 7 like it. r9c1: next to it on its teal line there's one of 3 6 9 and one of 2 5 8 already, and three in a row need one of each. Only **r7c1** is left: **r7c1 = 8**.

![Step 37](../screenshots/variants/modular-lines-walkthrough/step-37.png)

#### Step 38: the 9 in box 7

Box 7 needs a 9. r8c1: column 1 has one (r2c1). r9c1: row 9 has one (r9c6). Only **r8c2** is left: **r8c2 = 9**.

![Step 38](../screenshots/variants/modular-lines-walkthrough/step-38.png)

#### Step 39: the 1 in box 8

Box 8 needs a 1. r7c4: column 4 has one (r3c4). r7c6: column 6 has one (r6c6). r8c4 and r8c5: row 8 has one (r8c3). r9c5: it's 3 cells from the 5 in r8c8 along its teal line, so it's one of 2 5 8 like it. Only **r7c5** is left: **r7c5 = 1**.

![Step 39](../screenshots/variants/modular-lines-walkthrough/step-39.png)

#### Step 40: the 2 in box 8

Box 8 needs a 2. r7c4 and r7c6: row 7 has one (r7c7). r8c4: column 4 has one (r2c4). r9c5: row 9 has one (r9c3). Only **r8c5** is left: **r8c5 = 2**.

![Step 40](../screenshots/variants/modular-lines-walkthrough/step-40.png)

#### Step 41: the 3 in box 8

Box 8 needs a 3. r7c4: column 4 has one (r4c4). r8c4: row 8 has one (r8c7). r9c5: row 9 has one (r9c2). Only **r7c6** is left: **r7c6 = 3**.

![Step 41](../screenshots/variants/modular-lines-walkthrough/step-41.png)

#### Step 42: the 3 in box 2

Box 2 needs a 3. r2c6: column 6 has one (r7c6). Only **r2c5** is left: **r2c5 = 3**.

![Step 42](../screenshots/variants/modular-lines-walkthrough/step-42.png)

#### Step 43: the 8 in box 2

Box 2 needs an 8, and r2c6 is its last empty cell: **r2c6 = 8**.

![Step 43](../screenshots/variants/modular-lines-walkthrough/step-43.png)

#### Step 44: the 7 in box 8

Box 8 needs a 7. r7c4: row 7 has one (r7c8). r9c5: column 5 has one (r5c5). Only **r8c4** is left: **r8c4 = 7**.

![Step 44](../screenshots/variants/modular-lines-walkthrough/step-44.png)

#### Step 45: the 7 in box 7

Box 7 needs a 7. r8c1: row 8 has one (r8c4). Only **r9c1** is left: **r9c1 = 7**.

![Step 45](../screenshots/variants/modular-lines-walkthrough/step-45.png)

#### Step 46: the 4 in box 7

Box 7 needs a 4, and r8c1 is its last empty cell: **r8c1 = 4**.

![Step 46](../screenshots/variants/modular-lines-walkthrough/step-46.png)

#### Step 47: the 4 in box 8

Box 8 needs a 4. r9c5: column 5 has one (r1c5). Only **r7c4** is left: **r7c4 = 4**.

![Step 47](../screenshots/variants/modular-lines-walkthrough/step-47.png)

#### Step 48: the 8 in box 8

Box 8 needs an 8, and r9c5 is its last empty cell: **r9c5 = 8**.

![Step 48](../screenshots/variants/modular-lines-walkthrough/step-48.png)

#### Step 49: the 1 in box 9

Box 9 needs a 1. r8c9: row 8 has one (r8c3). r9c8: column 8 has one (r2c8). r9c9: column 9 has one (r4c9). Only **r9c7** is left: **r9c7 = 1**.

![Step 49](../screenshots/variants/modular-lines-walkthrough/step-49.png)

#### Step 50: the 4 in box 9

Box 9 needs a 4. r8c9: row 8 has one (r8c1). r9c9: column 9 has one (r2c9). Only **r9c8** is left: **r9c8 = 4**.

![Step 50](../screenshots/variants/modular-lines-walkthrough/step-50.png)

#### Step 51: the 6 in box 9

Box 9 needs a 6. r8c9: row 8 has one (r8c6). Only **r9c9** is left: **r9c9 = 6**.

![Step 51](../screenshots/variants/modular-lines-walkthrough/step-51.png)

#### Step 52: the 8 in box 9

Box 9 needs an 8, and r8c9 is its last empty cell: **r8c9 = 8**.

![Step 52](../screenshots/variants/modular-lines-walkthrough/step-52.png)

Solved.

<!-- /walkthrough -->
