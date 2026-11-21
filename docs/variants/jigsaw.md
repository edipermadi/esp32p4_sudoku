# Jigsaw

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Jigsaw: variant-examples #5](../screenshots/variants/jigsaw.png)

## The rules

[Classic](classic.md) rules, but the 3×3 boxes are replaced by nine irregular **regions** of nine connected cells, outlined with thick lines. Every row, every column and every region holds 1 to 9, each once. Every puzzle has its own regions.

## On the board

- The regions are drawn with thick borders over a plain grid.
- Hints call them "region 1" to "region 9", numbered by their first cell, left to right, top to bottom.
- **Puzzles → ⋮ → New puzzle → Jigsaw**; it's also Saturday's daily puzzle (Hard). The generator starts from the 3×3 boxes and trades cells across region borders, keeping every region in one piece.

## How to solve it

- Read the shapes first: a region can stretch across several rows and columns, so one placed digit can rule out cells far apart in it.
- **The rule of 45**, without the arithmetic: where a few rows (or columns) together cover whole regions except for a cell or two sticking out, those cells must hold exactly the digits the rows still need. Look for regions that poke one cell into a neighbouring row.
- Naked singles come often in Jigsaw, because a cell sees a different set of cells than its box would give it.

## Worked example

**variant-examples #5**, Jigsaw Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*).

```
3 2 . . . . . . 9
. . 6 . . 1 2 3 .
. 1 7 . . . . . .
4 3 9 8 6 2 1 . 5
2 . 1 . . . 8 . 3
1 . 5 9 3 8 6 2 4
. . . . . . 7 4 .
. 4 3 1 . . 9 . .
6 . . . . . . 9 1
```

The regions are in the screenshot. Two of them matter here:

- **Region 1** (top left): r1c1, r1c2, r1c3, r2c2, r2c3, r3c2, r3c3, r4c3 and r4c4.
- **Region 6** (the right edge): r1c8, r1c9, r2c9, r3c9, r4c9, r5c9, and r6c7, r6c8, r6c9.

**Step 1: r2c2, in region 1.** Region 1 already has 3, 2, 6, 1, 7, 9 and 8: only 4 and 5 are missing, for its two empty cells r1c3 and r2c2. Column 2 has a 4 (r8c2), so **r2c2 = 5** (and r1c3 will be the 4).

**Step 2: r3c9, in region 6.** Region 6 has 9, 5, 3, 6, 2 and 4, so its three empty cells (r1c8, r2c9, r3c9) share 1, 7 and 8. Row 3 has a 1 (r3c2) and a 7 (r3c3). **r3c9 = 8**.

**Step 3: the 8 in row 1.** Row 1 needs an 8, and has six empty cells:

- r1c3: region 1 has an 8 (r4c4).
- r1c4: column 4 has that 8 too.
- r1c6: column 6 has an 8 (r6c6).
- r1c7: column 7 has an 8 (r5c7).
- r1c8: region 6 now has the 8 from step 2 (r3c9).

Only **r1c5** is left: **r1c5 = 8**. In a Classic grid r1c3 and r1c8 would have been ruled out by their boxes; here it takes the regions.
