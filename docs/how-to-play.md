# How to play

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

Each table is one screen: what you can do there and how. The general rules of play are at the end.

## Home screen

The first screen, and its menu (swipe it to the second page for Files, System and Settings).

| Action | How |
|---|---|
| Open the puzzle list            | **Puzzles** on the home screen                   |
| Resume an unfinished game       | **Continue** on the home screen (a list when there are several), or tap the puzzle |
| Ten minutes of Easy puzzles     | **Time attack** on the home screen, then **Start** |
| This week against last week     | Tap the "puzzles solved" line on the home screen  |
| Lock the screen now             | The lock button on the home screen (top right)   |
| Back to the home screen         | **Home** (top left of the list, statistics, players or settings) |
| Settings                         | **Settings** on the home screen (the menu's second page) |
| Memory, temperature, tasks       | **System** on the home screen (swipe the menu to its second page) |

## Puzzles screen

The collections and their puzzles, and its ⋮ menu.

| Action | How |
|---|---|
| Pick a collection               | Tap it in the column on the left; the icons above it switch between Built-in, Saved and Imported |
| Start or resume a puzzle        | Tap its number                                   |
| Make a new puzzle               | **Puzzles → ⋮ → New puzzle**, pick a variant, then a level |
| Type in a puzzle from a book    | **Puzzles → ⋮ → Enter puzzle**, then **Check & play** |
| Practise a technique            | **Puzzles → ⋮ → Learn**                          |
| Learn a variant's rule          | **Puzzles → ⋮ → Learn**, then its tab (Grids, Marks, Lines, Clues) |
| Bookmark a puzzle               | **⋮ → Star** in the game; **⋮ → Starred puzzles** on the Puzzles screen lists them |
| Export or delete a collection   | **Puzzles → ⋮ → Export collection** (saved ones) or **Delete collection** |

## Game screen

Playing a puzzle: the board, the number pad on the right (or left) and the buttons above.

| Action | How |
|---|---|
| Select a cell                   | Tap it                                           |
| Enter a digit                   | Tap 1–9 on the right. Tap the same digit again to clear it |
| Pencil marks                    | Switch **Notes** on, then tap digits to toggle marks |
| All candidates as notes         | **⋮ → Auto notes**                               |
| Place one digit in many cells   | Hold the digit on the pad (it locks), then tap cells; tap the digit to unlock |
| Clear a cell                    | **Erase**                                        |
| Take back a move                | **Undo**; **Redo** puts it back                  |
| Start the puzzle over           | **⋮ → Restart** (asks first if there is progress) |
| Pause                           | Tap the timer; tap the board to resume           |
| Back to the list                | **List** (top left); the game is saved           |
| Paint a cell / Killer cage sums | Press and hold the cell                          |
| A variant's rules               | **⋮ → Rules** in the game (left of Hint)          |
| Keep a puzzle in your own collection | **⋮ → Save to collection** in the game (puzzles made on the board; needs the clock) |
| Save a screenshot               | The picture button in the Solved dialog or on Replay |
| Share today's daily result       | Solve the daily puzzle, then the QR button in the Solved dialog |

## Statistics, Trophies and Gameplays

Your progress: solve times, achievements and recorded games.

| Action | How |
|---|---|
| See solve times and mistakes    | **Statistics** on the home screen; pick a level on the left |
| Solve times as a chart          | **Statistics → ⋮ → Chart**; pick a level on the left |
| See where your mistakes are     | **Statistics → ⋮ → Insights**                    |
| Achievements, daily leaderboard | **Trophies** on the home screen                  |
| Watch a recorded game again     | **Gameplays** on the home screen, tap a game     |

## Players and History

Who plays, and when.

| Action | How |
|---|---|
| Switch or add players          | **Players** on the home screen; ⋮ to add, rename or delete |
| Pick an avatar                 | **Players → ⋮ → Avatar**                         |
| A player's colour               | Tap the dot by their name on **Players**         |
| See who played on a day        | **History** on the home screen, tap the day      |

## Settings

**Settings** on the home screen; its sections are in tabs.

| Action | How |
|---|---|
| Hide the clock                  | **Settings → Show timer**                        |
| Lock the screen on its timeout  | **Settings → Display → Lock screen** (Swipe or PIN) |
| A dim clock when the screen is off | **Settings → Display → Screen-saver clock**   |
| Accent colour                   | **Settings → Accent colour**                     |
| Bigger digits on the board      | **Settings → Display → Digit size** → Large      |
| Hints that only point the way   | **Settings → Game → Hints** → Nudge only          |
| See the game screen tour again  | **Settings → Game → Game screen tour → Show again** |
| Number pad on the left          | **Settings → Game → Left-handed**                |
| Play without notes             | **Settings → Game → No-notes challenge**, then a new puzzle |
| Sound on/off                    | **Settings → Sound effects**                     |
| Screen brightness               | **Settings → Brightness** (10–100 %)             |
| What turns red                  | **Settings → Mistake check** (Off / Rules / Solution) |
| Speaker volume                  | **Settings → Volume** (0–100 %; a tick plays at the new level) |
| Light / dark mode               | **Settings → Dark mode**                         |
| Dark and dim at night           | **Settings → Display → Night mode**, then the hours |
| Colours for colour blindness    | **Settings → Display → Colour-blind friendly**   |
| Screen timeout                  | **Settings → Screen off after**                  |
| Connect to Wi-Fi                | **Settings → Wi-Fi**, switch on, tap a network   |
| Time zone                       | **Settings → Time zone**                         |

## Files, the card and updates

The microSD card: puzzle files, exports, backups and firmware updates.

| Action | How |
|---|---|
| Add puzzle files                | Copy them to `puzzles/` on the card, then **Settings → Data → Import puzzles** (needs the clock, over Wi-Fi) |
| Export to a spreadsheet         | **Settings → Export to CSV**, then **USB drive** |
| Back up / restore `sudoku.db`  | **Settings → USB drive → Connect**; plug into USB OTG |
| Format the microSD card         | **Settings → microSD card → Format**, confirm    |
| Rename or delete a file on the card | **Files** on the home screen, then the pencil or the bin on its row |
| Undo a delete on the card       | **Files → Recycle bin**, then the restore button |
| Update the firmware             | `update.bin` on the card, **Settings → Data → Firmware update** |

## While you play

The small number on each pad button is how many of that digit are still to place. Red digits break a row, column or box rule. The puzzle is solved when every cell is filled and nothing is red. Each puzzle has exactly one solution, so the solution is always the intended one.

Up to 5 unfinished games are kept per player (with a microSD card); starting a 6th drops the one played longest ago. Without a card only one is kept, and starting a different puzzle asks whether to resume the saved one or discard it.
