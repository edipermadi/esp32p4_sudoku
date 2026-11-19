# How to play

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

| Action                          | How                                              |
|---------------------------------|--------------------------------------------------|
| Open the puzzle list            | **Puzzles** on the home screen                   |
| Resume an unfinished game       | **Continue** on the home screen (a list when there are several), or tap the puzzle |
| Pick a collection               | Tap it in the row at the top (swipe for more)    |
| Start or resume a puzzle        | Tap its number                                   |
| Select a cell                   | Tap it                                           |
| Enter a digit                   | Tap 1–9 on the right. Tap the same digit again to clear it |
| Pencil marks                    | Switch **Notes** on, then tap digits to toggle marks |
| All candidates as notes         | **Auto**                                         |
| Place one digit in many cells   | Hold the digit on the pad (it locks), then tap cells; tap the digit to unlock |
| Clear a cell                    | **Erase**                                        |
| Take back a move                | **Undo**; **Redo** puts it back                  |
| Start the puzzle over           | **Restart** (asks first if there is progress)    |
| Pause                           | Tap the timer; tap the board to resume           |
| Back to the list                | **List** (top left); the game is saved           |
| Back to the home screen         | **Home** (top left of the list, statistics, players or settings) |
| Solve times as a chart          | **Statistics → Chart** (top right); pick a level on the left |
| Achievements, daily leaderboard | **Trophies** on the home screen                  |
| Make a new puzzle               | **Puzzles → New puzzle**, pick a variant, then a level |
| Paint a cell / Killer cage sums | Press and hold the cell                          |
| Hide the clock                  | **Settings → Show timer**                        |
| Lock the screen on its timeout  | **Settings → Display → Lock screen** (Swipe or PIN) |
| Lock the screen now             | The lock button on the home screen (top right)   |
| Type in a puzzle from a book    | **Puzzles → Enter**, then **Check & play**        |
| A dim clock when the screen is off | **Settings → Display → Screen-saver clock**   |
| Update the firmware             | `update.bin` on the card, **Settings → Data → Firmware update** |
| A variant's rules               | The light bulb button in the game (next to Hint) |
| Accent colour                   | **Settings → Accent colour**                     |
| A player's colour               | Tap the dot by their name on **Players**         |
| Practise a technique            | **Puzzles → Learn**                              |
| Add puzzle files                | Copy them to `puzzles/` on the card, then **Settings → Data → Import puzzles** |
| Export to a spreadsheet         | **Settings → Export to CSV**, then **USB drive** |
| Switch or add players          | **Players** on the home screen                   |
| See who played on a day        | **History** on the home screen, tap the day      |
| Watch a recorded game again     | **Gameplays** on the home screen, tap a game     |
| See solve times and mistakes    | **Statistics** on the home screen; pick a level on the left |
| Sound on/off                    | **Settings → Sound effects**                     |
| Screen brightness               | **Settings → Brightness** (10–100 %)             |
| What turns red                  | **Settings → Mistake check** (Off / Rules / Solution) |
| Speaker volume                  | **Settings → Volume** (0–100 %; a tick plays at the new level) |
| Light / dark mode               | **Settings → Dark mode**                         |
| Screen timeout                  | **Settings → Screen off after**                  |
| Connect to Wi-Fi                | **Settings → Wi-Fi**, switch on, tap a network   |
| Time zone                       | **Settings → Time zone**                         |
| Back up / restore `sudoku.db`  | **Settings → USB drive → Connect**; plug into USB OTG |
| Format the microSD card         | **Settings → microSD card → Format**, confirm    |
| Rename or delete a file on the card | **Settings → Data → Files**, then the pencil or the bin on its row |

The small number on each pad button is how many of that digit are still to place. Red digits break a row, column or box rule. The puzzle is solved when every cell is filled and nothing is red. Each puzzle has exactly one solution, so the solution is always the intended one.

Up to 5 unfinished games are kept per player (with a microSD card); starting a 6th drops the one played longest ago. Without a card only one is kept, and starting a different puzzle asks whether to resume the saved one or discard it.
