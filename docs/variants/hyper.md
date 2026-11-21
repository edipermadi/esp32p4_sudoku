# Hyper

[← All variants](README.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

![Hyper: variant-examples #4](../screenshots/variants/hyper.png)

## The rules

[Classic](classic.md) rules, and four extra 3×3 **windows** must also hold 1 to 9, each once. The windows sit one cell in from the edges, between the boxes: rows 2–4 and 6–8 crossed with columns 2–4 and 6–8. Also known as Windoku.

## On the board

- The 36 window cells are tinted, like the X-Sudoku diagonals.
- A repeat in a window turns red; hints name the windows ("Look at the top-left window").
- **Puzzles → ⋮ → New puzzle → Hyper**; it's also Thursday's daily puzzle (Hard).

## How to solve it

- Each window is a tenth to thirteenth "box": a hidden single can be in a window.
- A window overlaps four boxes, so the cells *outside* a window in those boxes are tied to it: if a box's digit must be inside the window, it can't be anywhere else in the window, and the other way round. That gives hidden singles in the thin strips between windows.
- A cell inside a window has 26 neighbours instead of 20, so window cells run out of candidates first: look there for naked singles.

## Worked example

**variant-examples #4**, Hyper Easy (see [Classic](classic.md#worked-example) for how to import the examples; cells are written r*row*c*column*).

```
. 4 . | 5 . 9 | . . 8
3 9 . | . . . | . . .
7 . 8 | 2 . . | 9 4 .
------+-------+------
4 . 7 | . 8 2 | 6 5 .
. . 3 | 6 . 7 | 2 . .
. 2 9 | 1 5 . | 8 . 7
------+-------+------
. 5 6 | . . 1 | 7 . 3
. . . | . . . | . 9 1
2 . . | 9 . 5 | . 8 .
```

The top-left window is rows 2–4, columns 2–4; the top-right one is rows 2–4, columns 6–8.

**Step 1: r2c4, in the top-left window.** Row 2 has 3 and 9; column 4 has 5, 2, 6 and 1; box 2 has 5, 9 and 2. By the Classic rules r2c4 could still be 4, 7 or 8. But it's in the top-left window, which has a 7 (r4c3) and an 8 (r3c3). **r2c4 = 4**.

**Step 2: r2c7, in the top-right window.** Row 2 now has 3, 9 and 4; column 7 has 9, 6, 2, 8 and 7; box 3 has 8, 9 and 4. That leaves 1 or 5, and the top-right window has a 5 (r4c8). **r2c7 = 1**.

**Step 3: r2c8, in the top-right window too.** Row 2 has 3, 9, 4 and 1; column 8 has 4, 5, 9 and 8; so 2, 6 or 7 are left. The window has a 2 (r4c6) and a 6 (r4c7). **r2c8 = 7**.

Three cells in a row, each settled by a window: that's typical of Hyper, where the windows do as much work as the boxes.
