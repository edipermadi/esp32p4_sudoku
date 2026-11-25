# Home, settings & system

[← README](../README.md) · [Features](features.md) · [How to play](how-to-play.md) · [Data & storage](data.md) · [Building](building.md) · [Development](development.md) · [Remote control](devctl.md)

The home screen, Settings and its tabs, how the board looks and sounds, Gibberlink, Wi-Fi and the clock, and the tools: Files, the picture and video viewer and the system monitor. Settings → Data is in [Data & storage](data.md).

## Home screen

| **Home** | **Home, page 2** |
|---|---|
| ![Home](screenshots/home.png) | ![Home, the menu's second page](screenshots/home-page2.png) |

- **Home screen**: the time and date at the top (once Wi-Fi has fetched it), the solved count and big buttons. Small status icons at the top left: **Wi-Fi** (bright when connected, dim while joining, red when it failed; gone while Wi-Fi is switched off), the **microSD card** (bright with `sudoku.db` open, dim while it opens, red with no card) and **USB** (only while a computer is plugged into the OTG port). Tapping Wi-Fi opens **Settings → Network**; tapping the card or USB opens **Settings → Data**. **Continue** resumes your unfinished game; when there is none it stays in its place, greyed out, with "No game in progress"; with several (see [Unfinished games](features-puzzles.md)) its line says how many more, and it opens a list to pick from. **Players** (at the top, showing who is playing), **Puzzles**, **Daily puzzle**, **Statistics**, **History**, **Gameplays**, **Trophies**, **Time attack**, **Files**, **Settings** and **System** (the system monitor) open those screens. The buttons stand in one column while they fit, then in a grid of two per row; when even the grid doesn't fit (five rows: ten buttons), the menu has pages side by side: swipe left or right, and dots under it show which page is shown (with twelve buttons, **Settings** and **System** are on the second page).

With twelve buttons the menu has two pages: swipe left for **Settings** and **System** (right above).

## Settings

**Settings** is split into tabs under its title: **Network** (Wi-Fi, time zone; the tab Settings opens on), **Display** (dark mode, accent colour, brightness, screen off after, lock screen and PIN, screen saver), **Sound** (sound effects, volume, Gibberlink), **Game** (show timer, mistake check) and **Data** (microSD card, USB drive, firmware update, import puzzles, export to CSV, back up database, system monitor, factory reset).

### Network

| **Settings → Network** |
|---|
| ![Settings, Network tab](screenshots/settings.png) |

- **Wi-Fi and clock**: on **Settings → Network**, the Wi-Fi row says "Connected  ******" rather than the network's name, so a photo or screenshot doesn't give it away; the eye button next to it shows the name (hidden again the next time Settings opens). **Settings → Wi-Fi** opens a screen with an on/off switch, nearby networks (tap one; secured networks ask for the password on an on-screen keyboard) and **Forget**. Once connected, the time comes from NTP (pool.ntp.org) and the home screen shows it. The board remembers the network and reconnects at start-up. Wi-Fi goes through the board's ESP32-C6 co-processor (ESP-Hosted over SDIO); the Wi-Fi code is shared with the `esp32p4_86box` project.

- **Time zone**: **Settings → Time zone** lists IANA names such as `Asia/Dubai`, with daylight saving where the zone has it.

### Display

| **Settings → Display** |
|---|
| ![Settings, Display tab](screenshots/settings-display.png) |

- **Light and dark mode**: switch between colour schemes under **Settings → Dark mode**.
- **Accent colour**: **Settings → Accent colour** picks Blue (the default), Teal, Green, Purple or Pink for buttons, the selected cell, the same-digit and row/column highlights and your digits, in both schemes.
- **Colour-blind friendly**: **Settings → Display → Colour-blind friendly** swaps red and green, which look alike to most colour-blind people, for the Okabe-Ito colours: mistakes in vermillion (orange-red), solved puzzles and finished boards in bluish green, the game in progress in yellow-orange. They differ from each other and from the blue of your digits in lightness as well as hue. Both light and dark mode have their own shades.
- **Digit size**: **Settings → Display → Digit size** (see [Playing a game](features-game.md)).
- **Screen timeout**: after a while without a touch the game pauses, the screen goes black and the backlight turns off, so a still image can't burn in. Tap anywhere to wake it. The waking tap doesn't press anything. Set the timeout under **Settings → Screen off after**: 1, 2, 5 (default), 10, 15 or 30 minutes, or Never.
- **Lock screen**: **Settings → Display → Lock screen** (Off, Swipe or PIN). The lock button on the home screen (left of the power button) locks at once: the screen goes dark and comes back locked at the next tap (with the setting Off, it locks with a swipe). When the setting is on, the screen also comes back from its timeout locked: a lock icon, the time, the date and who is playing, and either "Swipe up to unlock" (the time follows the finger; swipe far enough) or a 4-digit PIN on a keypad. Choosing PIN asks for the PIN twice; **Lock PIN → Change** sets a new one. After 5 wrong PINs the keypad waits 30 seconds. It is meant to keep little fingers out of a game, not to secure anything: the PIN is kept as plain text in `sudoku.db` (`settings.lock_pin`, with `lock_mode` 0 Off, 1 Swipe, 2 PIN), and pressing **RESET** starts the board unlocked (the lock only comes with a timeout), which is also the way back in with a forgotten PIN.
- **Night mode**: **Settings → Display → Night mode** on: between the **Night hours** (22:00 to 07:00 by default, local time; the hours may span midnight) the screens turn dark and the backlight goes down to the **Night brightness** (20 % by default, never above the day's). At the end of the night the day's look comes back. It needs the clock (Wi-Fi). Only the switch-over changes things, so dark mode or brightness changed by hand during the night stay until morning; the Dark mode switch shows what's on screen. Waking from the screen saver uses the night brightness too.

#### Screen saver

| **Life** | **Analog clock** | **Rain** |
|---|---|---|
| ![The Game of Life screen saver](screenshots/saver-life.png) | ![The analog clock screen saver](screenshots/saver-analog.png) | ![The digital rain screen saver](screenshots/saver-rain.png) |
| **Attractor** | **Boids** | **Lissajous** |
| ![The strange attractor screen saver](screenshots/saver-attractor.png) | ![The boids screen saver](screenshots/saver-boids.png) | ![The Lissajous screen saver](screenshots/saver-lissajous.png) |
| **Pendulum** | **Langton's ant** | **Brian's Brain** |
| ![The double pendulum screen saver](screenshots/saver-pendulum.png) | ![The Langton's ant screen saver](screenshots/saver-ant.png) | ![The Brian's Brain screen saver](screenshots/saver-brain.png) |
| **Cyclic spirals** | **Rock paper scissors** | **Excitable medium** |
| ![The cyclic spirals screen saver](screenshots/saver-cyclic.png) | ![The rock paper scissors screen saver](screenshots/saver-rps.png) | ![The excitable medium screen saver](screenshots/saver-excitable.png) |
| **Forest fire** | **3D Crystal growth** | **3D Diamond growth** |
| ![The forest fire screen saver](screenshots/saver-forest.png) | ![The 3D crystal growth screen saver](screenshots/saver-3d-crystal.png) | ![The 3D diamond growth screen saver](screenshots/saver-3d-diamond.png) |
| **3D Pyroclastic** | **3D Builder** | **Starfield** |
| ![The 3D pyroclastic screen saver](screenshots/saver-3d-pyro.png) | ![The 3D builder screen saver](screenshots/saver-3d-builder.png) | ![The starfield screen saver](screenshots/saver-stars.png) |
| **Mandelbrot** | **Liquid mixing** | |
| ![The Mandelbrot screen saver](screenshots/saver-mandel.png) | ![The liquid mixing screen saver](screenshots/saver-fluid.png) | |

- **Screen saver**: **Settings → Display → Screen saver** chooses what the screen does at the screen timeout. All but Dark show at the screen's usual brightness, with the CPU at full speed, for as long as **Settings → Display → Screen saver for** says: 10, 20 or 30 minutes (30 to begin with), then the screen goes dark and the board saves power as with Dark; or Never: until a touch. A tap wakes it (and shows the lock screen first, if that's on). The home screen's lock button goes straight to dark. The animated ones use [3Blue1Brown](https://www.3blue1brown.com/)'s colours (Manim's palette) on black: its blue, teal, green, yellow, gold, red and pink.
  - **Dark**: dark at once, saving power (backlight off, panel asleep, CPU slowed to 180 MHz).
  - **Digital clock**: the time and date in dim grey on black, at a new random spot every minute so nothing burns in.
  - **Analog clock**: a round face in dim greys (minute ticks, the numbers 1 to 12, the hour and minute hands, a deep red second hand, the date under the centre), the hands moving every second; it too moves every minute.
  - **Life**: Conway's Game of Life on a 120 × 120 board of 6-pixel round dots whose edges wrap around, 8 generations a second: living cells blue, a cell just born yellow, a dying one fading through deep blue, the unlit dots faintly there. When the pattern settles (repeats within 16 generations), or after 3000 generations, it starts again from a new random board, the old cells fading out under the new ones.
  - **Rain**: digital rain of the digits 1 to 9: 30 columns of 24-pixel digits, each a falling trail in its own colour (blue, teal, green, yellow, gold or pink) with a nearly white head, dimmer further back, at its own speed and length; now and then a digit in a trail changes, and a column comes round again from the top, in a new colour, after a random pause.
  - **Attractor**: strange attractors (Clifford and de Jong: a point moved again and again by sine and cosine formulas) drawn point by point: from black and a few scattered sparks (each new point flashes white for a moment) the shape's outline gathers, then it fills in as more points come (150 a frame at first, up to 100,000 a second), each pixel going from deep blue through blue, green and yellow to white the more often it is hit (on a log scale). After 2 minutes the screen clears and the next one is drawn from nothing. The formulas' four numbers are random, kept only when the points spread out rather than settle into a loop.
  - **Boids**: a flock of 120 birds, 25 frames a second, each a bright dot drawing a fading trail. Each bird follows three rules about the neighbours it sees (keep a little apart, fly the way they fly, head for their middle), wanders a little and turns back before the edges; out of that, flocks form, merge, split and sweep around. Each bird is blue, green or yellow and now and then takes the colour of a neighbour it sees, so a flock comes to one colour and flocks that meet fight it out (a change blends over half a second); once in a while a bird takes up another colour, which spreads or dies out. Two red hawks, bigger and a little faster than a cruising bird, chase the bird nearest each; birds that see a hawk flee it, faster than they cruise, so flocks burst apart and regroup.
  - **Lissajous**: the figure an oscilloscope draws from two sine waves (x = sin(a t + d), y = sin(b t), for a ratio a:b such as 3:4, 5:6 or 3:7), in blue, 25 frames a second: the phase d drifts slowly so the figure seems to turn in space, and a yellow dot traces it with a tail fading into the blue; every 16 seconds the figure shrinks to the middle and grows back with another ratio.
  - **Pendulum**: three double pendulums, in blue, yellow and red, on grey rods hanging from the middle of the screen, 25 frames a second; each lower bob draws a trail that fades out over about 4 seconds, coloured by how fast the bob went there (its colour's deep shade when slow, its palest when fast). It is the real physics (gravity, no friction; the equations of motion stepped 24 times a frame with Runge-Kutta, so the energy stays put). The three start a hair apart (a hundred-thousandth of a radian), from random high angles where the motion is chaotic: for several seconds they swing as one (only the top one shows), then they part and go three different ways. Every 2 minutes they start again from new angles.
  - **Starfield**: flying through the stars, 25 frames a second: 640 stars come towards you out of the middle of the screen, each a streak from where it was a moment ago to where it is, dim and thin far away, bright and up to 3 pixels thick as it rushes past; most are pale blue-white, some tinted blue, yellow, gold or teal. The speed rises and falls in slow waves (one every 36 seconds), from a calm drift of points of light to a surge of long warp streaks and back, and the view rolls slowly about the way you fly. A star that has passed comes back far away at a new place.
  - **Mandelbrot**: a never-ending zoom into the [Mandelbrot set](https://en.wikipedia.org/wiki/Mandelbrot_set), its outside in bands of 3Blue1Brown's colours (deep blue, blue, teal, green, yellow, gold, red, pink and round again, flowing slowly) and its inside black, about 12 frames a second. Each level is twice as deep as the one before, and the board works out the next level on its second core while it zooms into the one shown, so the zoom goes as fast as that work keeps up: a level every 3 seconds near the top, slowing to half a minute at level 11 and a minute by level 18, where each point takes over a thousand steps. In a 30-minute session it gets some 25 levels down, tens of millions of times deeper. An autopilot steers each level a little towards the most detailed edge near the middle, so the zoom keeps to the spirals, seahorses and small copies of the set along the edge. The first 10 levels are worked out in ordinary single-precision numbers; deeper, where those can no longer tell neighbouring points apart, the middle of the view is kept in 128-bit fixed point and its orbit worked out in that, and each point follows only its small difference from that orbit (perturbation), so the zoom can go on to level 110 (10^33 times deeper). A dive ends there, or if the autopilot finds nothing but the set's black inside, and then fades out and starts again from the whole set, somewhere else.
  - **Liquid mixing**: a tank of three liquids, in 3Blue1Brown's blue, yellow and red, starting in three slanted bands, stirred by three invisible spoons that wander round on slow looping paths and pour more of their own colour as they go, about 11 frames a second. The colours fold into each other in swirls, curls and marbling; where they mix the colours blend (blue and yellow make a light green), and fresh pours cut through in pure colour. It is a real fluid simulation (Jos Stam's "stable fluids" on a 96 × 96 grid, the board's second core stepping it): the water can't be squeezed, so a spoon pushing one way sends eddies curling off behind it. Mixing can't be undone, so after 100 seconds, before it has all turned one colour, the tank fades out and a fresh one, with new bands and new paths, fades in.
  - **Langton's ant**: a yellow ant on the same 120 × 120 grid of dots as Life, 600 steps a second: each step it turns right on a dark cell and left on a blue one, flips the cell and steps forward; the edges wrap around. It wanders chaotically for about 10,000 steps (some 17 seconds) and then suddenly builds a straight diagonal "highway" that runs off one edge and back in at the other. After 60,000 steps (about 100 seconds) the grid clears and it starts again.
  - **Brian's Brain**: a cellular automaton on the same 120 × 120 grid of dots as Life, 10 generations a second. A cell is off, firing or dying: an off cell fires when exactly two of its eight neighbours fire, a firing cell is dying a generation later and off the one after; the edges wrap around. It never settles: little spaceships stream out every way, collide and make more. Firing cells are yellow, dying ones blue, then they fade out through deep blue. If the activity dies down to almost nothing, or after 5000 generations, it starts again from a random soup.
  - The other cellular automata, each its own choice, on the same 120 × 120 grid of dots, 10 generations a second, each starting again from a new random start after 900 generations (90 seconds), or sooner if it dies out:
    - **Cyclic spirals** (8 states; a cell moves on to the next state when at least 5 of the 48 cells within 3 of it are already there): random noise organises itself into big turning spirals, in a wheel of colours.
    - **Rock paper scissors** (3 states; 3 of the 8 neighbours beat a cell): turbulent spiral waves of three colours chasing each other.
    - **Excitable medium** (Greenberg-Hastings: a resting cell is excited by an excited neighbour, or now and then by itself, a spark; then it goes through 10 refractory states back to rest): each spark sends out a yellow ring with a tail from gold through red, pink and purple to deep blue; rings meet and annihilate, like ripples from raindrops.
    - **Day & Night** (B3678/S34678): blobs of day and night that grow, merge and dissolve.
    - **Seeds** (B2/S: every live cell dies at once): it explodes from a small square in the middle into ever-spreading chaos.
    - **Maze** (B3/S12345): a maze grows out of a small square in the middle.
    - **Star Wars** (Generations S345/B2/C4): busy little ships with tails.
    - **Forest fire** (trees grow back quickly on bare ground, lightning strikes rarely, fire spreads to the trees next to it): a dense green forest, a ring of yellow fire spreading out from each strike, red embers behind it.

    Life-like ones are blue with yellow newborns and dead cells fading through deep blue. Game of Life, Brian's Brain, Langton's ant and these all run on one engine (`saver_automata_core.c`).
  - The 3D cellular automata, each its own choice, in a 32 × 32 × 32 cube that turns slowly in space (a full turn about every 50 seconds, nodding a little up and down), 12 frames a second, each live cell a small bevelled block: deep blue at the bottom through blue, teal and green to yellow at the top, dying cells from red through pink and purple to deep blue, the far side dimmer. The cube's faces are walls (nothing wraps around).
    - **3D Crystal growth** (0-6/1,3/2/VN: born with 1 or 3 of the 6 face neighbours): one cell grows a spiky crystal, 2 generations a second, which then stands still for 8 seconds, turning.
    - **3D Diamond growth** (5-6/1-3/7/VN): one cell sends out a shell that grows into a cut diamond against the walls and burns away through its 5 dying states.
    - **3D Pyroclastic** (4-7/6-8/10/M: the 26 neighbours around, 8 dying states): a boiling mass, for 75 seconds.
    - **3D Builder** (Builder 1, 2,6,9/4,6,8-9/10/M): grows out of a patch of noise in the middle, then crumbles away.

    Each starts again from a new random start (for the crystal and the diamond, the same single cell, the cube at a new angle) when it dies out, stands still for 20 generations or has run its time. They run on their own engine (`saver_automata3d_core.c`).

### Sound

| **Settings → Sound** |
|---|
| ![Settings, Sound tab](screenshots/settings-sound.png) |

- **Sound effects**: synthesised on the device, with no audio files. A background task mixes up to four at once.
  - A short tick on every touch.
  - A low buzz when a digit you place breaks a row, column or box rule.
  - A rising chime when all nine of a digit are placed without conflicts.
  - A quick bright sparkle when a placed digit completes a row, column or 3×3 box (1–9, each once), and a longer one when it completes two or three of them at once.
  - A fanfare when the puzzle is solved.
  - Turn them on or off under **Settings → Sound effects**.
- **Gibberlink (remote control by sound)**: **Settings → Sound → Gibberlink** (off by default; also how a board takes in a puzzle another one sends with Share → **Send by sound**, see [Playing a game](features-game.md)) has the board's two microphones (the ES7210) listen for [ggwave](https://github.com/ggerganov/ggwave) transmissions, the data-over-sound protocol AI agents switch to in [Gibberlink](https://github.com/PennyroyalTea/gibberlink). Each message is one [remote-control command](devctl.md): `tap X Y`, `long X Y [MS]`, `swipe X1 Y1 X2 Y2 [MS]`, `press`/`move`/`release`, `wait MS` (up to 10 s), `fill CELL:DIGIT ...` (digits straight into the game: about 27 cells per message, as a message holds 140 bytes), `state` (the board as 81 characters, answered as `P4$OK state s0 <board>`), `ping`, `heap` or `screenoff` (not screenshots or files: sound carries about 16 bytes a second). A Gibberlink sender id in front (`AB$tap 360 400`) is fine. The board runs it as if it came over serial and answers by sound: `P4$OK tap`, or `P4$ERR ...`. A message may end in a check, `*` and 4 hex digits: the CRC-16/CCITT-FALSE of the text before it (Python: `binascii.crc_hqx(text, 0xFFFF)`), e.g. `fill 1:9 4:2*7C3E`. ggwave's error correction now and then lets a garbled message through as a different one (seen right after a restart); with the check the board refuses it (`ERR checksum`) instead of running it, and its answer carries a check too (`P4$OK fill 2*XXXX`), so a client can trust what `state` reads. A whole puzzle goes in about a minute this way: `state`, solve, two or three `fill` messages, `state` to confirm (tested: Easy #17 in 76 s, 45 cells in 2 messages). It understands ggwave's audible Normal, Fast and Fastest protocols (Gibberlink uses Fast, or Fastest in its fast mode) and answers in Fast; any ggwave sender works, such as the Waver app or ggwave's web demos. While it listens, a small note icon shows at the bottom right of every screen and lights up while a command runs. The board doesn't decode while it is answering (its own reply), and its replies play even with sound effects off. Listening takes a worker task on core 0 and about 9 MB of PSRAM; switched off, the memory is given back and the task ends (once a reply still playing has finished), and switching it on starts it again.

### Game

| **Settings → Game** |
|---|
| ![Settings, Game tab](screenshots/settings-game.png) |

How a game plays: **Show timer**, **Mistake check**, **No-notes challenge**, **Left-handed**, **Hints** (Nudge only) and **Game screen tour** (Show again). Each is described in [Playing a game](features-game.md).

### Data

| **Settings → Data** |
|---|
| ![Settings, Data tab](screenshots/settings-data.png) |

The microSD card, the USB drive, firmware update, importing puzzles, export to CSV, backing up the database and factory reset: see [Data & storage](data.md).

## Tools

| **Files** | **Puzzle file preview** |
|---|---|
| ![Files](screenshots/files.png) | ![A puzzle file's preview, with Import](screenshots/file-preview.png) |

- **Files**: **Files** on the home screen lists the microSD card's folders and files (folders first, then A–Z; up to 200 per folder) with each file's size and date. Tap a folder to open it, **Up** to go back. Each row's **⋮** button opens its menu: **Rename** (an on-screen keyboard; the name must be new in that folder and can't hold `\ / : * ? " < > |`) and **Delete** (asks first). Deleting moves a file or a whole folder to the **Recycle bin** (`.trash` on the card, shown at the top of the card's root): inside it each item shows where it came from, and its **⋮** menu has **Restore** (puts it back, making its folders again if they're gone) and **Delete for good**; **Empty** clears the bin. Deleted things still take space on the card until the bin is emptied. The game's database is protected: `sudoku.db` and SQLite's files next to it (`sudoku.db-journal`, `-wal`, `-shm`) show a lock and can't be renamed or deleted, and nothing can be renamed to those names (a stray file named like the journal would be rolled back into the database). While the card is lent to a computer (**USB drive**), the screen says it can't be read. Folders are read by a worker task on core 0, so a folder of many replays doesn't hold up the screen.
- **Puzzle file preview**: in **Files**, tap an `.sdm`, `.sdx` or `.opensudoku` puzzle file (the list icon) to see what's in it: its name, how many puzzles and of which variants (and how many lines or entries aren't puzzles), and the puzzles one at a time on a board, with ‹ › to step through them and the variant and level under it. The board shows the givens, the X-Sudoku diagonals and Hyper windows tinted, Jigsaw regions as thick lines and Killer cages dashed with their sums (Even-Odd, Consecutive, Thermo, Sandwich, Kropki, XV and Greater-Than marks, Quadruple circles and Arrow, Renban and Whispers lines aren't drawn). **Close** goes back to the folder; **Import** imports that file, wherever it is, as **Settings → Data → Import puzzles** does for `puzzles/` (each puzzle checked for exactly one solution, a collection named after the file in **Puzzles → Imported**; a file imported already is skipped, by its name), and a notice says how it went. Import needs `sudoku.db` and the clock (set over Wi-Fi: each collection is dated), and is dimmed, with the reason under the board, without them. Up to 2000 puzzles (512 KB of the file) are shown.
- **Picture and video viewer**: in **Files**, tap a `.jpg` (the screenshots) or an `.mp4` (the exported replays) to see it full screen. Swipe left or right, or use the arrows at the sides, to go through the other pictures and videos in the same folder; a tap shows or hides the bars; **✕** closes it. Pictures bigger than the screen are scaled down to fit. JPEGs are decoded by the ESP32-P4's hardware JPEG decoder (baseline JPEGs; progressive ones can't be shown). Videos are H.264 Baseline in MP4, decoded in software (tinyh264): **play/pause**, a slider to jump anywhere and the time. A 720×720 replay plays at its real speed, showing about 8 of its 15 frames a second: one core decodes while the other converts the frame for the screen (only the rows that changed), but decoding alone nearly fills the time a frame lasts, so some frames are decoded but not shown; other phones' or cameras' videos (Main or High profile) say they can't be played.
- **System monitor**: **System** on the home screen shows, refreshed every second, in five tabs (**System**, **Memory**, **Storage**, **Graphics**, **Tasks**; it reopens on the tab last used): uptime, CPU temperature, the load on each core and the last reset reason; internal RAM (free, lowest free, largest block), PSRAM (free, used) and DMA-capable RAM; the firmware's size in its app partition, NVS entries and the microSD card's free space; LVGL's frames drawn per second, how busy it is, draw time per frame, objects and timers; and every FreeRTOS task with its core, priority, state, CPU share (of one core) and free stack; tap **Name**, **Core**, **CPU** or **Stack free** above the list to sort by it (busiest first to begin with; tap again to reverse). Nothing on it may hold interrupts off on the display's core for long, or the whole screen blinks cyan: so no largest free PSRAM block (walking the 28 MB heap took 4 ms), the CPU load comes from the idle tasks' counters, and the full task list (1.5 ms to collect) is read only while the **Tasks** tab is open, by a small task on the other core (core 0), whose interrupts the display doesn't use. The **System** tab also shows the board's IP address (while Wi-Fi is connected), its Wi-Fi MAC address (the ESP32-C6's, read once Wi-Fi has connected) and its Ethernet MAC address (the ESP32-P4's own), and graphs of the last hour: CPU load (core 0 and core 1), temperature and the Wi-Fi signal strength (dBm, with a word for it: excellent, good, fair or weak; a gap while not connected); the **Memory** tab one of free internal RAM. A point is taken every 10 seconds from start-up, whether the page is open or not, so the graphs are there when you open it. It only reads, nothing is written.

### System monitor tabs

**System** on the home screen (the menu's second page), refreshed every second (see **System monitor** above).

| **System** | **Memory** |
|---|---|
| ![System tab](screenshots/sysmon.png) | ![Memory tab](screenshots/sysmon-memory.png) |
| **Storage** | **Graphics** |
| ![Storage tab](screenshots/sysmon-storage.png) | ![Graphics tab](screenshots/sysmon-graphics.png) |
| **Tasks** (sorted by CPU) | |
| ![Tasks tab](screenshots/sysmon-tasks.png) | |
