# Product Killer

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Product Killer: variant-examples #53](../screenshots/variants/product-killer.png)

## The rules

[Classic](classic.md) rules, plus **cages** (dashed outlines): the digits in a cage **multiply** to the number in its corner, and no digit repeats in a cage. A cage of 12 over two cells is 3 and 4 (or 2 and 6); one of 70 over three cells is 2, 5 and 7.

## On the board

- Cages are drawn as in [Killer](killer.md), with their product in the top-left corner.
- A cage turns red when a digit in it doesn't divide its number, or when it's full and the digits multiply to something else.
- The grader and hints try every way to fill a cage: "The cage at row 5, column 6 multiplies to 224: with what's in it and around it, its empty cells can only be 4, 7, 8."
- **Puzzles → ⋮ → New puzzle → Clues → Product**: Easy, Medium and **Hard**. There's no Expert: products pin the digits down so well that Expert puzzles are rare (hundreds of tries each), so the button is greyed out, as Kids 4×4's are.

## How to solve it

- **Factor the number:** 224 = 2 × 2 × 2 × 2 × 2 × 7 over three cells can only be 4 × 7 × 8; a 7 (or 5) in a product puts a 7 (or 5) in the cage.
- **Primes are strong:** a two-cell cage of 5 is 1 and 5; of 7, 1 and 7.
- **What isn't a factor:** a cage whose number is odd has no even digit; one not divisible by 3 has no 3, 6 or 9.

## Worked example

**variant-examples #53**, Product Killer Easy (see [Classic](classic.md#worked-example-the-whole-puzzle) for how to import the examples; cells are written r*row*c*column*).

**Step 1: the 2 in row 5.** r5c1, r5c2 and r5c3 see the 2 in r6c1; r5c5 the 2 in r2c5; r5c7 to r5c9 the 2 in r4c9. r5c6 is in a cage of 224 with r5c7 and r6c6: three different digits multiplying to 224 can only be 4, 7 and 8, so no 2. Only **r5c4** is left: **r5c4 = 2**.

**Step 2: the 7 in row 5.** r5c1 to r5c3 see the 7 in r4c2, r5c7 the 7 in r2c7, r5c8 and r5c9 the 7 in r6c8. r5c5 is in a cage of 5 with r6c5: that's 1 and 5. So the 7 is in **r5c6** (one of the 224 cage's 4, 7, 8): **r5c6 = 7**.

**Step 3: the 2 in row 1.** r1c2 is in a cage of 36 with r2c2: two different digits making 36 are 4 and 9, so no 2. r1c7 is in a cage of 18 with the 6 in r1c8: it's 3. The others see a 2 in their column or box. **r1c3 = 2**.

## The whole puzzle, step by step

<!-- walkthrough -->
<!-- Written by tools/walkthrough.py product-killer: edit the script, not this part. -->

The whole puzzle, from the start to the last cell: 71 steps, each one a single (one place left for a digit, or one digit left for a cell), taken in the order a box's single comes first, so not quite the order of the worked example above. Each step has a page of its own, with **Previous** and **Next**: pick one from the list below. In each picture, the unit the step looks at is tinted yellow (for a cell with one digit left: its row, column and box), the new digit is on green, the digits that rule cells out have an **orange ring**, and the empty cells they rule out a **red cross**. Givens are on grey, digits from earlier steps in blue.

![The start](../screenshots/variants/product-killer-walkthrough/step-00.png)

1. [the 2 in box 1](product-killer/step-01.md)
2. [the 7 in box 1](product-killer/step-02.md)
3. [the 6 in box 1](product-killer/step-03.md)
4. [the 6 in box 2](product-killer/step-04.md)
5. [the 8 in box 2](product-killer/step-05.md)
6. [the 8 in box 1](product-killer/step-06.md)
7. [the 5 in box 1](product-killer/step-07.md)
8. [the 8 in box 3](product-killer/step-08.md)
9. [the 5 in box 4](product-killer/step-09.md)
10. [the 6 in box 4](product-killer/step-10.md)
11. [the 8 in box 4](product-killer/step-11.md)
12. [the 3 in box 4](product-killer/step-12.md)
13. [the 4 in box 4](product-killer/step-13.md)
14. [the 2 in box 5](product-killer/step-14.md)
15. [the 6 in box 5](product-killer/step-15.md)
16. [the 7 in box 5](product-killer/step-16.md)
17. [the 8 in box 5](product-killer/step-17.md)
18. [the 4 in box 5](product-killer/step-18.md)
19. [the 9 in box 5](product-killer/step-19.md)
20. [the 3 in box 5](product-killer/step-20.md)
21. [the 4 in box 6](product-killer/step-21.md)
22. [the 6 in box 6](product-killer/step-22.md)
23. [the 3 in box 6](product-killer/step-23.md)
24. [the 2 in box 7](product-killer/step-24.md)
25. [the 1 in box 7](product-killer/step-25.md)
26. [the 1 in box 4](product-killer/step-26.md)
27. [the 9 in box 4](product-killer/step-27.md)
28. [the 1 in box 5](product-killer/step-28.md)
29. [the 1 in box 2](product-killer/step-29.md)
30. [the 3 in box 2](product-killer/step-30.md)
31. [the 3 in box 3](product-killer/step-31.md)
32. [the 3 in box 1](product-killer/step-32.md)
33. [the 1 in box 1](product-killer/step-33.md)
34. [the 5 in box 5](product-killer/step-34.md)
35. [the 5 in box 2](product-killer/step-35.md)
36. [the 7 in box 2](product-killer/step-36.md)
37. [the 3 in box 7](product-killer/step-37.md)
38. [the 7 in box 7](product-killer/step-38.md)
39. [the 9 in box 7](product-killer/step-39.md)
40. [the 3 in box 8](product-killer/step-40.md)
41. [the 4 in box 8](product-killer/step-41.md)
42. [the 6 in box 8](product-killer/step-42.md)
43. [the 7 in box 8](product-killer/step-43.md)
44. [the 8 in box 8](product-killer/step-44.md)
45. [the 8 in box 7](product-killer/step-45.md)
46. [the 4 in box 7](product-killer/step-46.md)
47. [the 3 in box 9](product-killer/step-47.md)
48. [the 4 in box 9](product-killer/step-48.md)
49. [the 4 in box 3](product-killer/step-49.md)
50. [the 4 in box 1](product-killer/step-50.md)
51. [the 9 in box 1](product-killer/step-51.md)
52. [the 4 in box 2](product-killer/step-52.md)
53. [the 9 in box 2](product-killer/step-53.md)
54. [the 1 in box 3](product-killer/step-54.md)
55. [the 6 in box 9](product-killer/step-55.md)
56. [the 7 in box 9](product-killer/step-56.md)
57. [the 8 in box 9](product-killer/step-57.md)
58. [the 8 in box 6](product-killer/step-58.md)
59. [the 1 in box 6](product-killer/step-59.md)
60. [the 5 in box 9](product-killer/step-60.md)
61. [the 5 in box 6](product-killer/step-61.md)
62. [the 5 in box 3](product-killer/step-62.md)
63. [the 2 in box 3](product-killer/step-63.md)
64. [the 9 in box 3](product-killer/step-64.md)
65. [the 9 in box 6](product-killer/step-65.md)
66. [the 5 in box 8](product-killer/step-66.md)
67. [the 1 in box 8](product-killer/step-67.md)
68. [the 2 in box 8](product-killer/step-68.md)
69. [the 1 in box 9](product-killer/step-69.md)
70. [the 2 in box 9](product-killer/step-70.md)
71. [the 9 in box 9](product-killer/step-71.md)

Solved.

<!-- /walkthrough -->
