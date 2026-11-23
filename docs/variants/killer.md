# Killer

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Killer: variant-examples #6](../screenshots/variants/killer.png)

## The rules

[Classic](classic.md) rules, plus **cages**: groups of 2 to 5 connected cells outlined with a dashed line, with a small **sum** in the top-left corner. The digits in a cage add up to its sum, and **no digit repeats within a cage**. Killer puzzles have few givens, or none: the cages carry the information.

## On the board

- Each cage is a dashed outline; its sum is in its top-left cell. Pencil marks are drawn smaller so they stay clear of the sum.
- A cage whose digits go over its sum, or that's full and short of it, turns red; so does a digit repeated in a cage.
- **Hold a cell** to see which digits can still make up its cage (the cage helper).
- **Puzzles → ⋮ → New puzzle → Killer**: Easy and Medium use cages of 2 or 3 cells, Hard and Expert up to 5. It's Friday's daily puzzle (Medium).

## How to solve it

- **Two-cell cages with a given:** the other cell is the sum minus that digit.
- **Small and big sums have few combinations:** 3 in two cells is always 1 + 2; 4 is 1 + 3; 16 is 7 + 9; 17 is 8 + 9; 6 in three cells is 1 + 2 + 3; 24 in three is 7 + 8 + 9.
- **Impossible digits:** a digit that would need its partner to be 10 or more, or 0 or less, or a repeat, can't go in the cage. That's what the worked example uses.
- **The rule of 45:** a row, column or box adds up to 45. If cages cover a box except for one cell, that cell is 45 minus the cages' sums; if one cage pokes out of the box, the cell sticking out is the cages' total minus 45.

## Worked example

**variant-examples #6**, Killer Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The cages are in the screenshot; these three matter:

- the bottom-left cage: **15** over r9c1, r9c2 and r9c3 (r9c3 is a given 2);
- a two-cell cage of **13**: r4c7 (a given 8) and r5c7;
- a two-cell cage of **16**: r4c8 and r4c9 (a given 9).

**Step 1: the 3 in column 2.** Column 2 needs a 3. Go down it:

- r1c2, r2c2, r3c2: box 1 has a 3 (r2c1).
- r4c2, r5c2, r6c2: box 4 has a 3 (r6c3).
- r8c2: row 8 has a 3 (r8c9).
- r9c2: it's in the bottom-left cage of 15 with the given 2, so r9c1 + r9c2 = 13. If r9c2 were 3, r9c1 would have to be 10. Impossible.

Only **r7c2** is left: **r7c2 = 3**. Without the cage, r9c2 would still be open.

**Step 2: the 3 in column 7.** r1c7 and r4c7 are filled; r2c7, r6c7, r7c7 (the 3 just placed in row 7), r8c7 and r9c7 all see a 3 in their row or box. That leaves r3c7 and **r5c7**, and r5c7 is in the cage of 13 with the 8: it must be 13 − 8 = 5, not 3. **r3c7 = 3**.

**Step 3: the 3 in column 8.** r1c8 and r3c8 see the 3 just placed in r3c7 (box 3, row 3); r2c8, r6c8, r7c8, r8c8 and r9c8 see a 3 in their row or box. That leaves r5c8 and **r4c8**, and r4c8 is in the cage of 16 with the 9: it must be 7. **r5c8 = 3**.

The same cages give two more digits at once: **r5c7 = 5** and **r4c8 = 7**. Killer often goes like this: a few sums fixed early, then the Classic rules take over.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py killer: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 71 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick one from the list below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/killer-walkthrough/step-00.png)

1. [the 3 in box 3](killer/step-01.md)
2. [the 3 in box 6](killer/step-02.md)
3. [the 1 in box 6](killer/step-03.md)
4. [the 2 in box 6](killer/step-04.md)
5. [the 2 in box 3](killer/step-05.md)
6. [the 4 in box 6](killer/step-06.md)
7. [the 5 in box 6](killer/step-07.md)
8. [the 6 in box 6](killer/step-08.md)
9. [the 7 in box 6](killer/step-09.md)
10. [the 3 in box 7](killer/step-10.md)
11. [the 9 in row 6](killer/step-11.md)
12. [the 7 in row 6](killer/step-12.md)
13. [the 4 in row 5](killer/step-13.md)
14. [the 7 in box 5](killer/step-14.md)
15. [the 8 in box 5](killer/step-15.md)
16. [the 2 in row 5](killer/step-16.md)
17. [the 6 in row 5](killer/step-17.md)
18. [r2c7](killer/step-18.md)
19. [r2c8](killer/step-19.md)
20. [r3c6](killer/step-20.md)
21. [the 2 in box 1](killer/step-21.md)
22. [the 2 in box 5](killer/step-22.md)
23. [the 1 in box 2](killer/step-23.md)
24. [the 3 in box 5](killer/step-24.md)
25. [the 2 in box 8](killer/step-25.md)
26. [the 2 in box 9](killer/step-26.md)
27. [the 1 in row 7](killer/step-27.md)
28. [the 1 in row 9](killer/step-28.md)
29. [the 3 in box 8](killer/step-29.md)
30. [the 3 in box 2](killer/step-30.md)
31. [the 8 in column 4](killer/step-31.md)
32. [the 6 in column 4](killer/step-32.md)
33. [the 9 in column 4](killer/step-33.md)
34. [the 7 in column 4](killer/step-34.md)
35. [the 7 in column 7](killer/step-35.md)
36. [the 9 in column 7](killer/step-36.md)
37. [the 9 in box 8](killer/step-37.md)
38. [the 5 in row 7](killer/step-38.md)
39. [the 4 in row 7](killer/step-39.md)
40. [the 4 in box 2](killer/step-40.md)
41. [the 7 in box 2](killer/step-41.md)
42. [the 6 in row 7](killer/step-42.md)
43. [the 8 in row 7](killer/step-43.md)
44. [r1c1](killer/step-44.md)
45. [r3c3](killer/step-45.md)
46. [the 5 in box 4](killer/step-46.md)
47. [the 6 in box 4](killer/step-47.md)
48. [the 7 in box 1](killer/step-48.md)
49. [the 9 in box 1](killer/step-49.md)
50. [the 4 in box 1](killer/step-50.md)
51. [the 6 in box 1](killer/step-51.md)
52. [the 1 in box 1](killer/step-52.md)
53. [the 6 in box 2](killer/step-53.md)
54. [the 5 in box 2](killer/step-54.md)
55. [the 7 in box 3](killer/step-55.md)
56. [the 6 in box 3](killer/step-56.md)
57. [the 9 in box 3](killer/step-57.md)
58. [the 5 in box 3](killer/step-58.md)
59. [the 4 in box 4](killer/step-59.md)
60. [the 9 in box 4](killer/step-60.md)
61. [the 8 in box 4](killer/step-61.md)
62. [the 1 in box 7](killer/step-62.md)
63. [the 4 in box 7](killer/step-63.md)
64. [the 7 in box 7](killer/step-64.md)
65. [the 8 in box 7](killer/step-65.md)
66. [the 9 in box 7](killer/step-66.md)
67. [the 8 in box 8](killer/step-67.md)
68. [the 5 in box 8](killer/step-68.md)
69. [the 4 in box 9](killer/step-69.md)
70. [the 5 in box 9](killer/step-70.md)
71. [the 6 in box 9](killer/step-71.md)

Solved.

<!-- /walkthrough -->
