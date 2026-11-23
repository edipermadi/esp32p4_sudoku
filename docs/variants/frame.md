# Frame

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Frame: variant-examples #30](../screenshots/variants/frame.png)

## The rules

[Classic](classic.md) rules, and **clues** left of some rows and above some columns. A clue is **the sum of the first three digits** from there: the three cells of that row or column in the first box. In 6, 2, 5, 1, 9, 8 the clue is 6 + 2 + 5 = 13. A line without a clue has no such rule.

The three cells are in one box, so they never repeat: a clue is 6 (1 + 2 + 3) to 24 (7 + 8 + 9).

## On the board

- The clues are in blue in a margin left of the rows and above the columns; a line without a clue has none.
- The first three cells of a line turn red when they already add up to more than its clue, or (all filled) to something else.
- The grader and hints keep, for each of the three cells, the digits that make the clue with two others the cell's neighbours can still hold: "The first three digits of row 1 add up to its clue 7, so row 1, column 3 can only be 2".
- **Puzzles → ⋮ → New puzzle → Clues → Frame**: about three quarters of the lines have a clue (Hard and Expert: about 60 %).

## How to solve it

- **Extreme clues have one set:** 6 is 1, 2, 3; 7 is 1, 2, 4; 23 is 6, 8, 9; 24 is 7, 8, 9.
- **A clue is a cage without the outline:** treat the three cells like a Killer cage with that sum.
- **Two clues meet in a corner box:** the top-left box has the clue of rows 1–3 and of columns 1–3. Together the row clues of a box add up to 45, and so do its column clues, so one missing clue can be worked out from the other two.
- **A known digit and a clue leave a pair:** with a 4 in the three and a clue of 7, the other two are 1 and 2.

## Worked example

**variant-examples #30**, Frame Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*). The clues are in the screenshot; from the left, row 1's is **7**; from the top, column 3's is **15** and column 7's **15**.

**Step 1: r2c7, column 7 (clue 15).** Row 2 has 4, 5 and 8; column 7 has 1, 2, 3 and 6; that leaves 7 and 9. Column 7 starts with r1c7, a 3, so r2c7 and r3c7 add up to 12. With a 9 in r2c7, r3c7 would be another 3. **r2c7 = 7**.

**Step 2: r3c7, the same column.** Now r3c7 is 15 − 3 − 7: **r3c7 = 5** (by the Classic rules it could be 5 or 9).

**Step 3: r1c3, row 1 (clue 7).** Row 1's first three cells are r1c1, a 4 (r1c2) and r1c3: the two empty ones add up to 7 − 4 = 3, so they are 1 and 2. Column 3 has a 1. **r1c3 = 2**. Column 3's clue agrees: 2 + 5 + 8 = 15.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py frame: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 51 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick one from the list below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/frame-walkthrough/step-00.png)

1. [the 1 in box 1](frame/step-01.md)
2. [the 7 in box 1](frame/step-02.md)
3. [the 2 in box 1](frame/step-03.md)
4. [the 3 in box 1](frame/step-04.md)
5. [the 6 in box 1](frame/step-05.md)
6. [the 9 in box 1](frame/step-06.md)
7. [the 3 in box 2](frame/step-07.md)
8. [the 2 in box 2](frame/step-08.md)
9. [the 5 in box 2](frame/step-09.md)
10. [the 6 in box 2](frame/step-10.md)
11. [the 7 in box 2](frame/step-11.md)
12. [the 9 in box 2](frame/step-12.md)
13. [the 1 in box 3](frame/step-13.md)
14. [the 2 in box 3](frame/step-14.md)
15. [the 5 in box 3](frame/step-15.md)
16. [the 7 in box 3](frame/step-16.md)
17. [the 8 in box 3](frame/step-17.md)
18. [the 9 in box 3](frame/step-18.md)
19. [the 1 in box 4](frame/step-19.md)
20. [the 3 in box 4](frame/step-20.md)
21. [the 2 in box 4](frame/step-21.md)
22. [the 6 in box 4](frame/step-22.md)
23. [the 7 in box 4](frame/step-23.md)
24. [the 9 in box 4](frame/step-24.md)
25. [the 1 in box 5](frame/step-25.md)
26. [the 1 in box 6](frame/step-26.md)
27. [the 5 in box 6](frame/step-27.md)
28. [the 2 in box 6](frame/step-28.md)
29. [the 4 in box 6](frame/step-29.md)
30. [the 7 in box 6](frame/step-30.md)
31. [the 8 in box 6](frame/step-31.md)
32. [the 2 in box 7](frame/step-32.md)
33. [the 4 in box 7](frame/step-33.md)
34. [the 6 in box 7](frame/step-34.md)
35. [the 7 in box 7](frame/step-35.md)
36. [the 8 in box 7](frame/step-36.md)
37. [the 9 in box 7](frame/step-37.md)
38. [the 6 in box 8](frame/step-38.md)
39. [the 2 in box 8](frame/step-39.md)
40. [the 2 in box 5](frame/step-40.md)
41. [the 7 in box 5](frame/step-41.md)
42. [the 7 in box 8](frame/step-42.md)
43. [the 5 in box 9](frame/step-43.md)
44. [the 6 in box 9](frame/step-44.md)
45. [the 4 in box 9](frame/step-45.md)
46. [the 4 in box 8](frame/step-46.md)
47. [the 8 in box 8](frame/step-47.md)
48. [the 9 in box 8](frame/step-48.md)
49. [the 7 in box 9](frame/step-49.md)
50. [the 8 in box 9](frame/step-50.md)
51. [the 9 in box 9](frame/step-51.md)

Solved.

<!-- /walkthrough -->
