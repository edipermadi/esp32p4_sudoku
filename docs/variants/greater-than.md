# Greater-Than

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Greater-Than: variant-examples #19](../screenshots/variants/greater-than.png)

## The rules

[Classic](classic.md) rules, and inside each 3×3 box, a **sign** between every two side-by-side cells says which digit is bigger. The sign **points to the smaller digit**: in "3 < 8" the point faces the 3. There are no signs across box borders.

## On the board

- Each sign is an orange chevron on the line between two cells: ‹ › between left and right, ^ v between above and below, the point toward the smaller digit.
- Two digits that break a sign turn red.
- The grader and hints use the signs ("The signs around ... leave it only ..."), and also each cell's **range** from the chains of signs in its box: a cell bigger than three others, through the signs, is at least 4.
- **Puzzles → ⋮ → New puzzle → Greater-Than**. Givens are few: Easy keeps about 18, Medium about 4.

## How to solve it

- **Find the box's 1 and 9:** the 1 is smaller than all its neighbours (every sign points at it), the 9 bigger than all of them (every sign points away).
- **Follow the chains:** if a < b < c < d inside a box, a is at most 6 and d at least 4. A cell with four smaller neighbours chained behind them can be far from 1.
- **Sandwiched cells:** between "3 <" and "< 8", a cell is 4 to 7.
- Then let the Classic rules cut the range down, as in the worked example.

## Worked example

**variant-examples #19**, Greater-Than Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The signs are in the screenshot.

**Step 1: r7c7, the top-left cell of box 9.** Row 7 has 9, 2 and 5; column 7 has 3, 7, 4 and 6; box 9 has a 3. By the Classic rules r7c7 could be 1 or 8. The signs say it's **smaller than r7c8** (to its right) and **smaller than r8c7** (below): two different cells of box 9 are bigger than it, and they can't both be 9. So r7c7 is at most 7. **r7c7 = 1**.

**Step 2: the 1 in row 1.** Row 1 needs a 1; its empty cells are r1c1, r1c3, r1c4, r1c5, r1c6 and r1c8.

- r1c3: column 3 has a 1 (r9c3).
- r1c4, r1c5, r1c6: box 2 has a 1 (r3c4).
- r1c8: the signs say **3 < r1c8 < 8** (it's between the 3 in r1c7 and the 8 in r1c9), so it's 4 to 7, not 1.

Only **r1c1** is left: **r1c1 = 1**.

**Step 3: the 1 in row 2.** Row 2 needs a 1 too:

- r2c1, r2c2: box 1 now has the 1 from step 2.
- r2c3: column 3 has a 1 (r9c3); r2c4, r2c5, r2c6: box 2 has a 1 (r3c4).
- r2c8: the sign below it says it's **bigger than r3c8**, which is a 2. So it's at least 3.

Only **r2c9** is left: **r2c9 = 1**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py greater-than: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 63 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick one from the list below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/greater-than-walkthrough/step-00.png)

1. [the 1 in box 1](greater-than/step-01.md)
2. [the 9 in box 1](greater-than/step-02.md)
3. [the 9 in box 2](greater-than/step-03.md)
4. [the 1 in box 3](greater-than/step-04.md)
5. [the 4 in box 3](greater-than/step-05.md)
6. [the 6 in box 3](greater-than/step-06.md)
7. [the 5 in box 3](greater-than/step-07.md)
8. [the 1 in box 4](greater-than/step-08.md)
9. [the 9 in box 4](greater-than/step-09.md)
10. [the 9 in box 5](greater-than/step-10.md)
11. [the 1 in box 6](greater-than/step-11.md)
12. [the 1 in box 5](greater-than/step-12.md)
13. [the 9 in box 6](greater-than/step-13.md)
14. [the 3 in box 7](greater-than/step-14.md)
15. [the 3 in box 1](greater-than/step-15.md)
16. [the 5 in box 1](greater-than/step-16.md)
17. [the 2 in box 1](greater-than/step-17.md)
18. [the 2 in box 2](greater-than/step-18.md)
19. [the 3 in box 2](greater-than/step-19.md)
20. [the 4 in box 2](greater-than/step-20.md)
21. [the 7 in box 2](greater-than/step-21.md)
22. [the 5 in box 2](greater-than/step-22.md)
23. [the 1 in box 8](greater-than/step-23.md)
24. [the 3 in box 8](greater-than/step-24.md)
25. [the 3 in box 5](greater-than/step-25.md)
26. [the 2 in box 5](greater-than/step-26.md)
27. [the 3 in box 6](greater-than/step-27.md)
28. [the 2 in box 6](greater-than/step-28.md)
29. [the 2 in box 4](greater-than/step-29.md)
30. [the 5 in box 6](greater-than/step-30.md)
31. [the 7 in box 6](greater-than/step-31.md)
32. [the 7 in box 5](greater-than/step-32.md)
33. [the 7 in box 4](greater-than/step-33.md)
34. [the 5 in box 4](greater-than/step-34.md)
35. [the 6 in box 4](greater-than/step-35.md)
36. [the 6 in box 1](greater-than/step-36.md)
37. [the 8 in box 4](greater-than/step-37.md)
38. [the 5 in box 5](greater-than/step-38.md)
39. [the 4 in box 5](greater-than/step-39.md)
40. [the 6 in box 5](greater-than/step-40.md)
41. [the 8 in box 5](greater-than/step-41.md)
42. [the 8 in box 2](greater-than/step-42.md)
43. [the 6 in box 2](greater-than/step-43.md)
44. [the 8 in box 6](greater-than/step-44.md)
45. [the 5 in box 7](greater-than/step-45.md)
46. [the 2 in box 8](greater-than/step-46.md)
47. [the 4 in box 8](greater-than/step-47.md)
48. [the 6 in box 8](greater-than/step-48.md)
49. [the 7 in box 8](greater-than/step-49.md)
50. [the 7 in box 7](greater-than/step-50.md)
51. [the 7 in box 1](greater-than/step-51.md)
52. [the 8 in box 1](greater-than/step-52.md)
53. [the 8 in box 7](greater-than/step-53.md)
54. [the 8 in box 8](greater-than/step-54.md)
55. [the 9 in box 8](greater-than/step-55.md)
56. [the 1 in box 9](greater-than/step-56.md)
57. [the 2 in box 9](greater-than/step-57.md)
58. [the 4 in box 9](greater-than/step-58.md)
59. [the 5 in box 9](greater-than/step-59.md)
60. [the 6 in box 9](greater-than/step-60.md)
61. [the 7 in box 9](greater-than/step-61.md)
62. [the 8 in box 9](greater-than/step-62.md)
63. [the 9 in box 9](greater-than/step-63.md)

Solved.

<!-- /walkthrough -->
