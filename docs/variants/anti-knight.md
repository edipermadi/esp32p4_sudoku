# Anti-Knight

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Anti-Knight: variant-examples #7](../screenshots/variants/anti-knight.png)

## The rules

[Classic](classic.md) rules, and two cells a **chess knight's move** apart never hold the same digit. A knight's move is two cells in one direction and one cell sideways: from r5c5, the cells r3c4, r3c6, r4c3, r4c7, r6c3, r6c7, r7c4 and r7c6.

## On the board

- Nothing extra is drawn: the rule is the same everywhere.
- A digit that repeats a knight's move away turns red like any other repeat; hints say "a knight's move away".
- The rules card (the ⋮ menu → Rules) draws a 5 with its eight knight cells crossed out.
- **Puzzles → ⋮ → New puzzle → Anti-Knight**.

## How to solve it

- Every cell has up to 8 extra neighbours, so naked singles come much sooner than in Classic: when you check a cell, also look at the eight knight cells around it.
- For a hidden single in a row or box, a digit rules out the cells a knight's move from it, often in the next box over.
- A digit in a 3×3 box rules out a lot of the neighbouring boxes: a 5 in the centre of box 5 sees the edges of boxes 2, 4, 6 and 8.

## Worked example

**variant-examples #7**, Anti-Knight Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*).

**Step 1: r7c4.** Row 7 has 1, 4 and 5; column 4 has 2, 4, 3, 9 and 8; box 8 has 4, 9, 2 and 8. By the Classic rules r7c4 could be 6 or 7. A knight's move from r7c4 (one row up, two columns right) is **r6c6**, which is a 7. **r7c4 = 6**.

**Step 2: the 4 in row 6.** Row 6 has 8, 7 and 2; it needs a 4 among r6c1, r6c2, r6c3, r6c4, r6c7 and r6c8:

- r6c2: **r8c3** is a 4, two rows down and one column right: a knight's move.
- r6c3: column 3 has that 4 (r8c3).
- r6c4: column 4 has a 4 (r3c4).
- r6c7: column 7 has a 4 (r2c7).
- r6c8: **r7c6** is a 4, one row down and two columns left: a knight's move.

Only **r6c1** is left: **r6c1 = 4**. Two of the five cells were ruled out by knights.

**Step 3: the 5 in row 9.** Row 9 has 7, 8 and 2; for its 5:

- r9c1, r9c2: box 7 has a 5 (r8c2).
- r9c6: **r7c7** is a 5, two rows up and one column right: a knight's move.
- r9c8, r9c9: box 9 has that 5 (r7c7).

Only **r9c5** is left: **r9c5 = 5**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py anti-knight: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 49 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick one from the list below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/anti-knight-walkthrough/step-00.png)

1. [the 3 in box 2](anti-knight/step-01.md)
2. [the 8 in box 2](anti-knight/step-02.md)
3. [the 2 in box 3](anti-knight/step-03.md)
4. [the 2 in box 5](anti-knight/step-04.md)
5. [the 2 in box 7](anti-knight/step-05.md)
6. [the 5 in box 8](anti-knight/step-06.md)
7. [the 5 in box 2](anti-knight/step-07.md)
8. [the 7 in box 2](anti-knight/step-08.md)
9. [the 1 in box 2](anti-knight/step-09.md)
10. [the 5 in box 3](anti-knight/step-10.md)
11. [the 5 in box 1](anti-knight/step-11.md)
12. [the 7 in box 1](anti-knight/step-12.md)
13. [the 7 in box 4](anti-knight/step-13.md)
14. [the 5 in box 5](anti-knight/step-14.md)
15. [the 1 in box 5](anti-knight/step-15.md)
16. [the 1 in box 4](anti-knight/step-16.md)
17. [the 1 in box 1](anti-knight/step-17.md)
18. [the 4 in box 1](anti-knight/step-18.md)
19. [the 8 in box 1](anti-knight/step-19.md)
20. [the 4 in box 4](anti-knight/step-20.md)
21. [the 3 in box 4](anti-knight/step-21.md)
22. [the 8 in box 4](anti-knight/step-22.md)
23. [the 5 in box 6](anti-knight/step-23.md)
24. [the 7 in box 6](anti-knight/step-24.md)
25. [the 1 in box 8](anti-knight/step-25.md)
26. [the 3 in box 8](anti-knight/step-26.md)
27. [the 3 in box 7](anti-knight/step-27.md)
28. [the 7 in box 8](anti-knight/step-28.md)
29. [the 6 in box 8](anti-knight/step-29.md)
30. [the 6 in box 7](anti-knight/step-30.md)
31. [the 6 in box 1](anti-knight/step-31.md)
32. [the 9 in box 1](anti-knight/step-32.md)
33. [the 6 in box 4](anti-knight/step-33.md)
34. [the 9 in box 4](anti-knight/step-34.md)
35. [the 9 in box 6](anti-knight/step-35.md)
36. [the 9 in box 7](anti-knight/step-36.md)
37. [the 1 in box 9](anti-knight/step-37.md)
38. [the 3 in box 9](anti-knight/step-38.md)
39. [the 3 in box 6](anti-knight/step-39.md)
40. [the 4 in box 6](anti-knight/step-40.md)
41. [the 4 in box 9](anti-knight/step-41.md)
42. [the 6 in box 9](anti-knight/step-42.md)
43. [the 6 in box 6](anti-knight/step-43.md)
44. [the 6 in box 3](anti-knight/step-44.md)
45. [the 8 in box 3](anti-knight/step-45.md)
46. [the 8 in box 6](anti-knight/step-46.md)
47. [the 7 in box 9](anti-knight/step-47.md)
48. [the 8 in box 9](anti-knight/step-48.md)
49. [the 9 in box 9](anti-knight/step-49.md)

Solved.

<!-- /walkthrough -->
