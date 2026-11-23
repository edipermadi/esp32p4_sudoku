# Rossini

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Rossini: variant-examples #36](../screenshots/variants/rossini.png)

## The rules

[Classic](classic.md) rules, and **arrows** left of the rows and above the columns. An arrow says the **first three digits** from there (the cells of that row or column in the first box) **rise in the direction it points**: an arrow pointing into the grid means they rise away from the edge (2, 4, 7), one pointing out that they fall (8, 5, 1). A row or column **with no arrow** has first three digits that **neither rise nor fall** (like 4, 9, 2): every row and column is clued, the absence of an arrow too.

## On the board

- The arrows are in blue in a margin left of the rows and above the columns, pointing toward the bigger digits.
- Two of the first three digits out of their arrow's order turn red, and so do three that rise or fall where there's no arrow.
- The grader and hints keep each of the first three cells to the digits its arrow leaves room for (a rising first cell is at most 7), and narrow them by the digits already there: "The first three digits of column 6 fall from its arrow's side, so ...".
- **Puzzles → ⋮ → New puzzle → Clues → Rossini**.

## How to solve it

- **A rising three needs room:** the first is 1–7, the second 2–8, the third 3–9; falling, the other way round.
- **One known digit bounds the others:** with a rising arrow and a 6 in the middle, the first is 1–5 and the third 7–9.
- **No arrow is a clue too:** with no arrow and the first two digits 3 and 5, the third is not 6–9 (that would rise).
- **Rows and columns meet in a corner box,** so the top-left box often settles early.

## Worked example

**variant-examples #36**, Rossini Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The arrows are in the screenshot: column 1's and column 3's point down (rising), column 6's up (falling); no row has one.

**Step 1: r1c6, column 6 (falling).** Row 1 has 3, 4, 6 and 8; column 6 has 5 and 7; box 2 has a 1: that leaves 2 and 9. Column 6's first three fall from the top, so r1c6, the biggest of them, is at least 3. **r1c6 = 9**.

**Step 2: the 9 in row 2.** Row 2 has a 7, and the 9s in r7c1, r1c6 (step 1) and r3c9 rule out r2c1, r2c4 to r2c6, r2c8 and r2c9. That leaves r2c2 and r2c3. r2c3 is the middle of column 3's first three, which rise: it's at most 8. **r2c2 = 9**.

**Step 3: the 1 in box 1.** The 1s in r9c3 and r3c4 rule out r1c3, r2c3 and row 3; r1c2 and r2c2 are filled. That leaves r1c1 and r2c1. r2c1 is the middle of column 1's first three, which rise: it's at least 2. **r1c1 = 1**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py rossini: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 51 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick one from the list below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/rossini-walkthrough/step-00.png)

1. [the 1 in box 1](rossini/step-01.md)
2. [the 9 in box 1](rossini/step-02.md)
3. [the 1 in box 3](rossini/step-03.md)
4. [the 4 in box 3](rossini/step-04.md)
5. [the 9 in box 4](rossini/step-05.md)
6. [the 1 in box 5](rossini/step-06.md)
7. [the 9 in box 5](rossini/step-07.md)
8. [the 8 in box 5](rossini/step-08.md)
9. [the 3 in box 6](rossini/step-09.md)
10. [the 9 in box 6](rossini/step-10.md)
11. [the 8 in box 6](rossini/step-11.md)
12. [the 8 in box 4](rossini/step-12.md)
13. [the 6 in box 4](rossini/step-13.md)
14. [the 6 in box 1](rossini/step-14.md)
15. [the 7 in box 1](rossini/step-15.md)
16. [the 3 in box 1](rossini/step-16.md)
17. [the 2 in box 1](rossini/step-17.md)
18. [the 5 in box 1](rossini/step-18.md)
19. [the 8 in box 1](rossini/step-19.md)
20. [the 3 in box 2](rossini/step-20.md)
21. [the 4 in box 2](rossini/step-21.md)
22. [the 2 in box 2](rossini/step-22.md)
23. [the 5 in box 2](rossini/step-23.md)
24. [the 7 in box 2](rossini/step-24.md)
25. [the 8 in box 2](rossini/step-25.md)
26. [the 6 in box 2](rossini/step-26.md)
27. [the 9 in box 2](rossini/step-27.md)
28. [the 5 in box 4](rossini/step-28.md)
29. [the 2 in box 4](rossini/step-29.md)
30. [the 7 in box 4](rossini/step-30.md)
31. [the 2 in box 6](rossini/step-31.md)
32. [the 5 in box 6](rossini/step-32.md)
33. [the 7 in box 6](rossini/step-33.md)
34. [the 3 in box 7](rossini/step-34.md)
35. [the 8 in box 7](rossini/step-35.md)
36. [the 1 in box 8](rossini/step-36.md)
37. [the 2 in box 8](rossini/step-37.md)
38. [the 3 in box 8](rossini/step-38.md)
39. [the 6 in box 8](rossini/step-39.md)
40. [the 7 in box 8](rossini/step-40.md)
41. [the 1 in box 9](rossini/step-41.md)
42. [the 2 in box 9](rossini/step-42.md)
43. [the 5 in box 9](rossini/step-43.md)
44. [the 6 in box 9](rossini/step-44.md)
45. [the 4 in box 9](rossini/step-45.md)
46. [the 4 in box 8](rossini/step-46.md)
47. [the 8 in box 8](rossini/step-47.md)
48. [the 9 in box 8](rossini/step-48.md)
49. [the 7 in box 9](rossini/step-49.md)
50. [the 8 in box 9](rossini/step-50.md)
51. [the 9 in box 9](rossini/step-51.md)

Solved.

<!-- /walkthrough -->
