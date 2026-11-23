# Variants

[← README](../../README.md) · [Features](../features.md) · [Puzzles](../features-puzzles.md) · [How to play](../how-to-play.md)

Every Sudoku variant the board can make, each on its own page: the rules, what it looks like on the board, how to solve it, and a worked example step by step. Make any of them with **Puzzles → ⋮ → New puzzle**, at Easy, Medium, Hard or Expert (Kids 4×4: Easy and Medium).

The worked examples use the 45 puzzles in [`variant-examples.sdx`](../variant-examples.sdx), one per variant, and each page's screenshot is that puzzle on the board. To follow along, copy the file to the `puzzles` folder on the card (over **USB drive**, or the card in a computer), then **Files → puzzles → variant-examples.sdx → Import**: they land in **Puzzles → Imported → variant-examples**, numbered as below. The file is made by `make -C tests examples` ([tools/variant_examples.c](../../tools/variant_examples.c)), which picks each puzzle so that its first steps need the variant's own rule, and prints the steps with their reasons. The screenshots come from `python3 tools/variant_shots.py --port /dev/ttyACM1` (all of them, or `... kropki` for one), with the file imported. Every page also has the whole puzzle solved step by step to the end, a picture per step, written and drawn by `python3 tools/walkthrough.py --all`.

| # | Variant | The extra rule |
|---|---|---|
| 1 | [Classic](classic.md) | Rows, columns and 3×3 boxes hold 1–9 once each (the base for all the others) |
| 2 | [X-Sudoku](x-sudoku.md) | Both long diagonals too |
| 3 | [Mini 6×6](mini-6x6.md) | 6×6 grid, 2×3 boxes, digits 1–6 |
| 4 | [Hyper](hyper.md) | Four extra 3×3 windows |
| 5 | [Jigsaw](jigsaw.md) | Irregular regions instead of boxes |
| 6 | [Killer](killer.md) | Cages with sums, no repeats in a cage |
| 7 | [Anti-Knight](anti-knight.md) | No repeat a knight's move apart |
| 8 | [Even-Odd](even-odd.md) | Shaded cells even, ringed cells odd |
| 9 | [Consecutive](consecutive.md) | A bar between every pair 1 apart, none elsewhere |
| 10 | [Thermo](thermo.md) | Digits rise along each thermometer |
| 11 | [Sandwich](sandwich.md) | Sum of the digits between each line's 1 and 9 |
| 12 | [Kids 4×4](kids-4x4.md) | 4×4 grid, 2×2 boxes, digits 1–4 |
| 13 | [Anti-King](anti-king.md) | No repeat in touching cells, corners included |
| 14 | [Non-Consecutive](non-consecutive.md) | No side-by-side digits 1 apart |
| 15 | [Disjoint Groups](disjoint-groups.md) | The same place in every box holds 1–9 |
| 16 | [Asterisk](asterisk.md) | Nine cells in an asterisk shape |
| 17 | [Kropki](kropki.md) | White dot: 1 apart; black dot: double; no dot: neither |
| 18 | [XV](xv.md) | X: sum 10; V: sum 5; no mark: neither |
| 19 | [Greater-Than](greater-than.md) | Signs inside each box point to the smaller digit |
| 20 | [Quadruple](quadruple.md) | Circles list digits among the four cells around them |
| 21 | [Arrow](arrow.md) | The digits on an arrow's shaft add up to its circle |
| 22 | [Renban](renban.md) | Each purple line holds a run of consecutive digits, in any order |
| 23 | [German Whispers](german-whispers.md) | Neighbours on a green line differ by 5 or more |
| 24 | [Skyscraper](skyscraper.md) | A clue says how many buildings are seen from it |
| 25 | [Little Killer](little-killer.md) | A clue with an arrow is the sum along that diagonal |
| 26 | [Palindrome](palindrome.md) | Each grey line reads the same from both ends |
| 27 | [Region Sum Lines](region-sum-lines.md) | A blue line adds up to the same in each box it passes through |
| 28 | [X-Sums](x-sums.md) | A clue is the sum of a line's first X digits, X being the first |
| 29 | [Zipper Lines](zipper-lines.md) | Cells mirrored about a pink line's middle add up to the same total |
| 30 | [Frame](frame.md) | A clue is the sum of a line's first three digits |
| 31 | [Between Lines](between-lines.md) | Digits on a line lie strictly between its two circles' |
| 32 | [Entropic Lines](entropic-lines.md) | Any three in a row on a line: a low, a middle and a high digit |
| 33 | [Modular Lines](modular-lines.md) | Any three in a row on a line: one each of 1 4 7, 2 5 8 and 3 6 9 |
| 34 | [Center Dot](center-dot.md) | The nine box centres hold 1–9 |
| 35 | [Outside Sudoku](outside-sudoku.md) | A clue lists digits among a line's first three cells |
| 36 | [Rossini](rossini.md) | An arrow: a line's first three rise that way; no arrow: they don't |
| 37 | [Diagonal Non-Consecutive](diagonal-non-consecutive.md) | No digits 1 apart in cells touching at a corner |
| 38 | [Dutch Whispers](dutch-whispers.md) | Neighbours on an amber line differ by 4 or more |
| 39 | [Parity Lines](parity-lines.md) | Neighbours on a red line are one odd, one even |
| 40 | [Slow Thermo](slow-thermo.md) | Digits rise or stay the same along each thermometer |
| 41 | [Fortress](fortress.md) | Each grey cell is bigger than its white neighbours |
| 42 | [Argyle](argyle.md) | No repeat along eight diagonal lines in an argyle pattern |
| 43 | [Nabner](nabner.md) | No two digits on a brown line the same or 1 apart |
| 44 | [Numbered Rooms](numbered-rooms.md) | A clue is the digit in a line's X-th cell, X being the first |
| 45 | [Sum Skyscrapers](sum-skyscrapers.md) | A clue is the sum of the buildings seen from it |

Cells are written r*row*c*column*, counted from the top left: r1c3 is row 1, column 3.
