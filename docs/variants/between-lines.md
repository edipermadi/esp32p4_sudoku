# Between Lines

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Between Lines: variant-examples #31](../screenshots/variants/between-lines.png)

## The rules

[Classic](classic.md) rules, plus **grey lines with a circle at each end**, through cells side by side or diagonal. Every digit on a line, between its two circles, lies **strictly between the two circled digits**: with 3 and 8 in the circles, the cells between hold 4 to 7. Either circle can be the smaller one, and the cells between may repeat a digit where the rows, columns and boxes allow it.

## On the board

- Each line is a grey line from one ring to the other.
- With both circles filled, a digit on the line that isn't strictly between them turns red, with the circles.
- The grader and hints keep, for each cell, the digits some pair of circles allows, and for each circle the digits that leave every cell between something to hold: "The digits on this line lie between the digits in its two circles, so row 6, column 3 can only be 9".
- **Puzzles → ⋮ → New puzzle → Lines → Between**: 8 lines (Hard and Expert 10) of 3 to 7 cells.

## How to solve it

- **The circles are far apart:** a line with a 7 between its circles has one circle at 6 or less and the other at 8 or more.
- **Long lines need wide gaps:** the cells between are often different digits (in a row or box), so the gap must fit them: four different digits between need circles at least 5 apart.
- **A circle at 1 or 9 settles the side:** the other circle is the high (or low) end.
- **No 1 or 9 between:** nothing is strictly below 1 or above 9.

## Worked example

**variant-examples #31**, Between Lines Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The lines are in the screenshot; three matter here:

- a line of **four**: circles at r5c4 and r3c4, with r4c4 and r3c3 between;
- a line of **three**: circles at r7c4 (a 4) and r6c4, with r6c5 (a 5) between;
- a line of **seven**: circles at r8c3 (a 1) and r7c1, with r9c4, r8c5 (a 2), r8c4, r7c3 (a 6) and r6c2 between.

**Step 1: r3c3, between the circles of the line of four.** Row 3 has 2, 5, 7 and 8; column 3 has 1, 3, 6, 7 and 8: that leaves 4 and 9. Nothing is above 9, so a 9 can never be strictly between two circles. **r3c3 = 4**.

**Step 2: the 1 in row 6.** Row 6 has 2, 3, 5 and 7. The 1s in r1c2, r8c3 and r2c8 rule out r6c2, r6c3 and r6c8. That leaves r6c4 and r6c6. r6c4 is a circle of the line of three, whose other circle is a 4 with a 5 between them: r6c4 must be above 5. **r6c6 = 1**.

**Step 3: the 9 in row 9.** Row 9 has 1 and 4. The 9 in r8c2 rules out r9c1 to r9c3, the 9s in r4c5 and r7c9 rule out r9c5 and r9c9. That leaves r9c4 and r9c6. r9c4 is between the circles of the line of seven, one of them a 1: it must be below the other circle, and nothing is above 9. **r9c6 = 9**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py between-lines: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 53 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick one from the list below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/between-lines-walkthrough/step-00.png)

1. [the 6 in box 1](between-lines/step-01.md)
2. [the 1 in box 4](between-lines/step-02.md)
3. [the 9 in box 4](between-lines/step-03.md)
4. [the 9 in box 1](between-lines/step-04.md)
5. [the 7 in box 1](between-lines/step-05.md)
6. [the 5 in box 1](between-lines/step-06.md)
7. [the 4 in box 1](between-lines/step-07.md)
8. [the 7 in box 3](between-lines/step-08.md)
9. [the 1 in box 5](between-lines/step-09.md)
10. [the 4 in box 7](between-lines/step-10.md)
11. [the 7 in box 7](between-lines/step-11.md)
12. [the 5 in box 7](between-lines/step-12.md)
13. [the 3 in box 7](between-lines/step-13.md)
14. [the 2 in box 7](between-lines/step-14.md)
15. [the 8 in box 7](between-lines/step-15.md)
16. [the 1 in box 8](between-lines/step-16.md)
17. [the 1 in box 2](between-lines/step-17.md)
18. [the 6 in box 2](between-lines/step-18.md)
19. [the 7 in box 8](between-lines/step-19.md)
20. [the 7 in box 5](between-lines/step-20.md)
21. [the 9 in box 8](between-lines/step-21.md)
22. [the 9 in box 2](between-lines/step-22.md)
23. [the 5 in box 2](between-lines/step-23.md)
24. [the 9 in box 3](between-lines/step-24.md)
25. [the 3 in box 3](between-lines/step-25.md)
26. [the 5 in box 8](between-lines/step-26.md)
27. [the 6 in box 8](between-lines/step-27.md)
28. [the 3 in box 8](between-lines/step-28.md)
29. [the 3 in box 5](between-lines/step-29.md)
30. [the 3 in box 6](between-lines/step-30.md)
31. [the 5 in box 6](between-lines/step-31.md)
32. [the 4 in box 6](between-lines/step-32.md)
33. [the 4 in box 3](between-lines/step-33.md)
34. [the 4 in box 2](between-lines/step-34.md)
35. [the 4 in box 5](between-lines/step-35.md)
36. [the 4 in box 4](between-lines/step-36.md)
37. [the 2 in box 5](between-lines/step-37.md)
38. [the 2 in box 2](between-lines/step-38.md)
39. [the 8 in box 2](between-lines/step-39.md)
40. [the 2 in box 4](between-lines/step-40.md)
41. [the 6 in box 4](between-lines/step-41.md)
42. [the 8 in box 5](between-lines/step-42.md)
43. [the 6 in box 5](between-lines/step-43.md)
44. [the 6 in box 6](between-lines/step-44.md)
45. [the 8 in box 6](between-lines/step-45.md)
46. [the 8 in box 3](between-lines/step-46.md)
47. [the 2 in box 3](between-lines/step-47.md)
48. [the 8 in box 8](between-lines/step-48.md)
49. [the 2 in box 9](between-lines/step-49.md)
50. [the 3 in box 9](between-lines/step-50.md)
51. [the 5 in box 9](between-lines/step-51.md)
52. [the 6 in box 9](between-lines/step-52.md)
53. [the 8 in box 9](between-lines/step-53.md)

Solved.

<!-- /walkthrough -->
