# Forcetris

A Tetris trainer that takes the placement decision away from you. Every piece gets a
fixed budget of real time from the moment it spawns; when the budget runs out the piece
is hard dropped wherever its ghost happens to be, whether you were ready or not.

The point is to put a hard ceiling on deliberation. Set the budget slightly below the
speed you can comfortably play at, and midgame downstacking stops being a thinking
problem and starts being a reaction problem.

TETR.IO has no equivalent knob — Zenith's shrinking lock delay is the closest thing, and
it isn't adjustable.

## Install

Requires Python 3.10+ and pygame.

```bash
pip install -r requirements.txt
```

## Run

```bash
python main.py                      # 1.0s per piece (the default)
python main.py --forced-delay 0.8   # 0.8s per piece
python main.py -f 0.5               # short form
python main.py --forced-delay 0     # timer off, plain Tetris
python main.py -f 0.8 --volume 30   # quieter music
python main.py -v 0 -s 60           # no music, effects at 60%
```

`--forced-delay` takes seconds as a float, so `0.45` is fine. `--volume` and `--sfx-volume` take a
percentage from 0 to 100, matching what the settings menu shows. All three clamp rather
than complain.

A flag left off the command line keeps whatever the saved profile holds, so the usual way
to run the game is `python main.py` with everything already set from the last session. A
flag that *is* passed wins for that launch only — it is not written back, so
`-f 0.5` for one run does not become the value every later run starts from.

The HUD shows the time the active piece has left under **Forced Drop**, and turns red
over the last quarter of the budget.

## Default keys

All of these are reboundable under **Game Settings → Controls**. Menu navigation is not:
arrow keys to move, `Z` or `Enter` to confirm, `X` or `Esc` to go back, always.

| Key | Action |
| --- | --- |
| Left / Right | Shift |
| Down | Soft drop |
| Z or Left Ctrl | Rotate counter-clockwise |
| X or Up | Rotate clockwise |
| A | Rotate 180 |
| Space | Hard drop |
| Left Shift | Hold |
| Escape | Pause |

## Starting a game

**Start Game** picks the mode and, on the same screen, whether the forced drop timer is
running at all. Up and Down pick a row, `Z` starts the mode you are on.

| Row | Does |
| --- | --- |
| Arcade / Timed / Free | `Z` starts that mode |
| Forced Drop | Left, Right or `Z` switches the timer on and off |

Whether the timer is running is the difference between practice and a plain game, so it
sits where that decision is actually made rather than two screens deep. Only the switch
lives here — the length of the budget is **Game Settings**' job, and switching the timer
back on returns the last budget you played with rather than the one the game shipped
with. It is remembered across restarts like everything else.

## Game Settings

**Game Settings** on the main menu — also reachable from the pause menu and the game
over screen — adjusts the forced delay and the base game's options. Up and Down pick a
row, Left and Right change it, and holding the key repeats.

| Setting | Range |
| --- | --- |
| Forced Drop | Off, then 0.05s to 5.00s in 0.05s steps |
| Ghost Piece | On / Off |
| Wall Kicks | On / Off |
| Linked Tiles | On / Off |
| Line Clears | Naive / Sticky Cascade / Linked Cascade. Defaults to Naive |
| Spins | Off / T-Spin / All-Spin / All-Spin + Mini |
| Finesse | Off / Count / Retry |
| Music | Off, then 5% to 100% in 5% steps |
| Sound | Off, then 5% to 100% in 5% steps |
| Controls... | Rebind the gameplay keys |
| Handling... | DAS, ARR, DCD, SDF and ARE |

Changes apply immediately, including to the piece already falling and to the music
playing behind the pause menu, so you can pause mid-run, shave 0.05s off, and feel it on
the very next piece. They are also saved as you make them — see
[Saved settings](#saved-settings).

**Line Clears** defaults to Naive: rows vanish, everything above drops by the number of
rows that went, and nothing cascades. That is how TETR.IO and every other guideline game
clears, so it is what a trainer for one has to do. The base game defaulted to a cascade,
where blocks left hanging fall into the gap and can set off further clears; both cascade
modes are still there under the same row if you want them — and they now actually work.
The machinery behind them shipped broken upstream (fills that crashed on the walls, a
sticky mode that never dropped anything, and a garbage-row clear that hung the game);
[cpp/README.md](cpp/README.md#the-cascade-repairs) lists the repairs, and
`tools/test_cascade.py` pins them.

If you have played before, the settings you already saved are yours and are left alone —
this default only applies to a profile that does not exist yet. **Line Clears** in the
menu switches it either way.

## Controls and handling

**Controls** rebinds the gameplay keys.

| On a row | Does |
| --- | --- |
| `Z` / `Enter` | Replace the binding with the next key pressed |
| `→` | Add another key, so one action answers to several |
| `←` | Remove the key added most recently |

So binding counter-clockwise rotation to both `A` and the up arrow is `Z` `A`, then `→`
`↑`. The defaults already ship that way — rotation sits on `Z` and `Left Ctrl`.

Taking a key that another action holds removes it from that action rather than leaving
one key firing two things, so the up arrow above stops turning clockwise. An action left
with nothing reads as *Unbound* until you give it a key. Four keys per action is the
limit, and a row too long for the panel is shortened to `Z, Left Ctrl +2`.

Menu navigation is deliberately not in that list. The arrow keys, `Z`, `X`, `Enter` and
`Escape` always drive the menus, so no set of bindings can strand you outside the screen
that would undo them. That is also why `Escape` cancels the *press a key* prompt and so
cannot itself be bound — **Reset to Defaults** is how Pause gets it back.

**Handling** carries the TETR.IO knobs:

| Setting | Range | Meaning |
| --- | --- | --- |
| DAS | 0–500ms | How long a held direction waits before it repeats |
| ARR | 0–200ms | Time between repeats. `0` slides to the wall in one frame |
| DCD | 0–200ms | A charged auto-shift is cut back to this on spawn and on rotation. `0` leaves it alone |
| SDF | 5–40x | Soft drop as a multiple of gravity. `40` drops to the floor at once, without locking |
| ARE | 0–500ms | The pause between one piece locking and the next appearing. Defaults to none |

180 rotation gets its own binding, defaulting to `A`. SRS defines no kicks for a 180,
so it uses the SRS+ table modern guideline games settled on; where that finds nothing,
the rotation is refused rather than forced through.

The millisecond settings land on a 20ms grid, because the game runs at a fixed 50 frames
per second and auto-shift can only act on a frame boundary. Arcade mode's difficulty ramp
no longer touches auto-shift, soft drop or the spawn pause — handling is yours, and only
gravity climbs with the level.

ARE is the pause between one piece locking and the next appearing. The base game held the
board for 400ms there, which is dead time in a trainer built on reaction speed, so it
defaults to none.

## Saved settings

Everything the settings menu can change is written to `data/settings.json` the moment you
change it — the forced delay, both volumes, the ghost, kicks, tiles, clear and spin
options, the finesse rule, every handling number, and every key binding. Nothing needs
saving by hand and there is no Apply button; closing the game keeps what you last had.

Three things set those values, in this order:

1. the built-in defaults,
2. `data/settings.json`, if it is there,
3. whatever you typed on the command line, for that launch only.

So `--forced-delay 0.8` overrides the saved delay without replacing it, and the next
plain `python main.py` is back to what the menu says.

A missing, corrupt or unwritable file falls back to the defaults rather than refusing to
start, and a single unusable value inside an otherwise good file is skipped rather than
taking the rest of the file down with it. Out-of-range numbers are clamped into the range
the menus can express, so a hand-edited file cannot put the game somewhere its own screens
cannot get it back from.

Upgrading from a version that only saved bindings, in `data/controls.json`, reads that
file once if there is no `settings.json` yet; the bindings carry over and the old file is
then left alone.

Set `FORCETRIS_CONFIG` to a path to keep a second profile — a different delay and handling
for a different kind of practice — without disturbing the first:

```bash
FORCETRIS_CONFIG=~/forcetris-sprint.json python main.py
```

The browser build gets an in-memory filesystem, so it saves happily and starts from the
defaults again on the next reload.

## Spins

A spin puts its name up to the left of the board for a couple of seconds — `T-SPIN`,
`MINI S-SPIN` — and the line count joins it underneath once the clear resolves, so a spin
and what it earned read as one event. **PERFECT CLEAR** joins them on a third line when
the placement empties the board, and raises the banner on its own when nothing else did,
which is the usual case.

To the right of the board, under the queue, **B2B** and **COMBO** count the runs. Back to
back holds through a quad or any clear that came out of a spin, and breaks on a smaller
one; a placement that clears nothing leaves it alone. The combo is the base game's, and
counts consecutive clears. Neither is drawn until it means something.

Nothing counts unless the last thing you did to the piece was rotate it; shifting or
auto-shifting disarms it, falling does not. **Spins** in Game Settings picks how
generously the rest is judged:

| Setting | Counts as a spin |
| --- | --- |
| Off | Nothing |
| T-Spin | T pieces only, by the three-corner rule. Full when both corners the T faces are filled, mini otherwise |
| All-Spin | Any piece that ends up wedged in — unable to move up, left or right. All of them score as full |
| All-Spin + Mini | The same, except T still goes by the corner rule and other pieces are minis unless the rotation needed a kick |

The default is All-Spin, since TETR.IO plays that way. Cells outside the matrix count as
filled for both rules — a wall wedges a piece as well as a block does.

## Finesse

Finesse is how few key presses a placement took. Every placement you can reach by
dropping a piece straight down has a minimum — turn it, move it, let go — and using more
than that minimum is a fault. Tapping left four times where one held key would have walked
the piece into the wall is the classic one: same placement, four times the work.

**FINESSE** sits under the queue with the running percentage, and the faults and presses
thrown away underneath it. A fault also puts `FINESSE +2` up beside the board, naming what
that particular placement cost you.

| Setting | Does |
| --- | --- |
| Off | Nothing is counted and nothing is drawn |
| Count | Faults are counted and shown. The placement stands |
| Retry | As above, and the piece is handed straight back to be placed again |

Retry is the one that actually trains it. A faulted piece returns to spawn, the board is
untouched, and you place it again — as many times as it takes. It keeps the time it had
already spent falling, so a deliberate fault cannot be used to buy another full forced
drop budget.

What counts as a press: shifting and rotating, one per press. Holding a direction is one
press however far auto-shift carries the piece, which is the whole point of the measure.
Soft drop, hard drop and hold are not counted — hold starts the count over, since the
piece it hands you comes from spawn like any other.

Three kinds of placement are **not judged at all**:

- **Tucks.** A piece slid under an overhang did not get there by anything the finesse
  tables describe, so every press spent on it would read as waste.
- **Spins.** Same reason. A spin bonus and a finesse fault never arrive together.
- **Forced drops.** The timer chose that placement, not you. Charging you for presses you
  had not finished making would be scoring the clock.

The test for the first two is the honest one: put a fresh copy of the piece back at the
spawn row in the same column and orientation, drop it, and see whether it lands where the
real one did. If it does not, the placement is off the tables and is left alone. That is
what makes Retry usable while downstacking — it enforces finesse on ordinary placements
and stays out of the way of the ones that need a tuck or a spin.

The minimums are not a hard-coded table. They are searched out from the spawn position
over an empty field, once per piece, which is what the published tables are. Nothing needs
more than three presses; a piece that needs no turning needs no more than two. Rotations
that would put a piece through a wall are refused rather than kicked, because the tables
every guideline game measures against are built that way.

## Analysis and replays

**Analysis** on the game over screen breaks the run down, and every game long enough to
be worth reading is written to `data/replays` as it ends. Nothing to press, nothing to
name.

| | |
| --- | --- |
| Score, Pieces, Lines, Time | with pieces per second beside the count |
| Finesse, Faults | the percentage, and how many placements it came from |
| Attack, VS | what the run would have sent, with APM, and the VS score |
| Key presses | what the run cost, and per piece |
| **Without faults** | what it would have cost made cleanly |
| Wasted | the difference |
| Clears, Spins, Perfect clears, Best B2B / combo | what the run was made of |

Those two press counts either side of each other are the point of the screen. One is what
you did; the other is the same run, same pieces, same placements, played with no wasted
motion.

**Replays** on the main menu lists what has been saved, newest first, and **Watch Replay**
on the analysis screen opens the one that just finished.

The replay is a **re-enactment**, not a slideshow. The piece is walked to where it went,
stop by stop — spawn, then wherever each press left it, then the drop — over the board the
placement was actually made onto. Sloppy play looks sloppy: the piece inches across in four
hops where one would have done.

| Key | Does |
| --- | --- |
| `←` `→` | One stop, and repeats when held. Runs on into the next piece |
| `↑` `↓` | Jump a whole piece |
| `Z` | Play and pause. Playing from the end starts over |
| `S` | Speed, 1x to 8x |
| `F` | Play it by the book — see below |
| `X` | Back |

Between the board and the readout sit **Hold** and **Next**, showing what the player could
see coming while that placement was being made. Those are recorded with the placement
rather than read off the pieces that follow it: a hold reorders those, so the piece played
next is not the piece that was shown next, and taking the shortcut would quietly
misrepresent the decision you are watching.

The panel beside them names the piece, the column, the presses, which one is being made
right now, and what finesse made of the placement.

### Play it by the book

`F` re-enacts the same run with the finesse **route** in place of what you pressed.

The piece is the same piece. It arrives in the same column, in the same orientation, and
the board it leaves behind is the same board — it simply stops fewer times on the way.
`Tap Left, Tap Left, Tap Left, Tap Left, Rotate CCW` becomes `Hold Left, Rotate CCW`, four
stops become one, and the run totals underneath re-read at 100% and the lower press count.

Because a stop is the unit of time, a corrected replay is visibly *shorter* to watch. That
is the difference the screen exists to show.

Placements finesse has no opinion about — **tucks, spins, and any the timer took** — keep
your own path in both views. There is no route to hold them to, and inventing one would be
inventing a mistake.

Nothing else moves. Correcting finesse changes what it cost to put a piece somewhere, never
where the piece went, so the score, the clears and the boards are untouched. That is the
whole claim the screen makes, and it is what the tests check hardest.

### The files

A replay is a list of placements, not a list of keystrokes. Keystrokes would be smaller,
but playing them back means re-simulating gravity, auto-shift and the bag, and a replay
that drifts from the game it recorded is worse than no replay. Each entry carries the
piece, where it ended up, what was pressed, where the piece stood after each of those
presses, the queue and held piece at the time, and a snapshot of the board once the clear
had resolved.

Replays written by an older build are still read; the fields they never carried come back
empty and the screens do without them. One written by a *newer* build is refused, since
there is no telling what its fields mean.

The snapshots keep only from the highest occupied row down, which is most of the size of a
file, since most of a Tetris board is empty most of the time. The newest 30 replays are
kept and older ones are pruned. Set `FORCETRIS_REPLAYS` to keep them somewhere else.

## Attack, APM and VS

Forcetris has no opponent, but the numbers TETR.IO players train by — APM and VS — are
made of the garbage a placement *would* send, so every placement is scored by TETR.IO's
table: quads send four, spins double their lines, back to back adds one, combos climb the
classic combo ladder, and a perfect clear adds ten on top. Under the all-spin rules a
full spin of any piece uses the spin line, which is what all-spin means competitively.

| | |
| --- | --- |
| APM | attack per minute |
| VS | attack plus garbage rows dug out, per hundred seconds |

The HUD shows the running total and APM under the combo counters while you play; the
analysis screen shows the totals, and the replay names what each placement sent. Garbage
only exists in arcade mode, so in free mode VS is simply attack per second times a
hundred — the number still moves the way it would, it just has no digging in it.

## Sound

| Cue | When |
| --- | --- |
| move / rotate / hold | Shifting, rotating, swapping. Shifting speaks on the initial press only, not on every auto-shift step |
| lock | The piece settles under gravity |
| drop | You hard dropped it yourself |
| **forced** | The timer took the placement away from you |
| clear / tetris | One to three lines, or four |
| combo1..combo10 | A clear extending a combo. Each rung is a semitone above the last, so the run can be heard climbing |
| b2b | A clear keeping back to back alive |
| finesse | A placement that took more presses than it needed |
| tspin | A spin landed |
| perfect | The placement emptied the board |
| gameover | The stack topped out |

`drop` and `forced` are deliberately unalike — a low sawtooth buzz against the hard
drop's thud — because the whole point is hearing, without looking, that you ran out of
time rather than chose to place.

The combo ladder is why there are ten of one cue: pitch is the only channel that says
*how long* the run is without asking you to look away from the stack. It stops climbing
at the tenth rung.

The effects are synthesised, not sampled, so the repository carries no third-party
audio. `tools/make_sounds.py` regenerates every file from the parameters at the bottom
of that script, so retuning a cue means changing a number and re-running it:

```bash
python tools/make_sounds.py
```

## How the timer behaves

- **It can be switched off entirely**, from the mode screen or by setting the delay to
  Off in the settings. With it off this is plain Tetris, and nothing below applies.
- **It starts when a piece spawns**, not when it becomes movable.
- **Holding restarts it.** The swapped-in piece gets a full budget. Since the base game
  only allows one hold per piece, this caps out at two budgets per piece — hold is an
  escape hatch for a placement you can't read in time, not an indefinite stall.
- **Soft dropping does not reset it.**
- **Wall kicks do not reset it.** They still reset the gravity counter, as in the base
  game — the drop timer is a separate clock.
- **It stops between pieces**, so the spawn delay is not charged against you.
- **It stops while the game is paused, while a menu is up, and while the window is out
  of focus.** Coming back from a pause does not instantly slam the piece down.

The timer is driven by `time.perf_counter()`, so it does not drift with the frame rate.
It is still only checked once per frame, so a drop lands on the first frame at or after
the deadline — up to 20 ms late at the game's 50 fps.

The program loop is a coroutine that yields once per frame. On the desktop that costs
nothing; in a browser tab it is the only reason the page stays responsive.

Everything else is the base game: 7-bag randomiser, SRS with Arika I kicks, hold with a
per-piece lock, ghost piece, T-spin detection, and the free / arcade / timed modes.
180 rotation is the one addition to the piece mechanics.

## Running on Android

The game builds to WebAssembly with [pygbag](https://pypi.org/project/pygbag/) and runs
in a mobile browser, which is the shortest path onto a phone. A connected keyboard works
as it does on the desktop; the on-screen keyboard does not, since the game reads real key
events.

```bash
pip install pygbag
python -m pygbag --build --disable-sound-format-error main.py
```

That writes `build/web/`. Serve it over HTTP — opening `index.html` from the filesystem
will not work, browsers refuse WebAssembly from `file://`:

```bash
python -m pygbag main.py     # builds, then serves on http://localhost:8000
```

Point the phone at that address on the same network, or push `build/web/` to any static
host (GitHub Pages will do).

`--disable-sound-format-error` is there because pygbag prefers OGG for audio. The effects
are WAV, which browsers play fine and which totals well under a megabyte, so the warning
does not apply here.

`pygbag.ini` keeps the test suite, the screenshots folder and the runtime save folder out
of the bundle — pygbag packages the whole directory otherwise, and none of the three can
do anything in a tab. The build is about 2.2MB, most of which is the music. It is picked
up automatically; there is no flag to pass.

Two things differ in the browser:

- **There is no command line.** The build runs on the defaults, so set the delay and the
  volumes in **Game Settings** instead.
- **Nothing survives a reload.** The page gets an in-memory filesystem, so high scores,
  settings and replays alike are written and then thrown away when the tab closes. The
  analysis screen and the replay viewer both still work within a session, and a write the
  filesystem refuses is shrugged off rather than taking the tab down.

Audio will not start until you have pressed a key, which browsers require. Since the
music starts when you pick a mode, this happens on its own.

### Native instead of the browser

Termux with an X11 server can in principle run the desktop version unchanged, but getting
pygame's SDL2 built there is its own project. The browser build is the supported path.

## Tests

All twelve suites run headlessly — no display, sound card, or browser needed.

```bash
python tools/test_forced_drop.py   # the timer rules above, against a fake clock
python tools/test_menus.py         # every menu button, driven by posted key events
python tools/test_web.py           # the properties the browser build depends on
python tools/test_controls.py      # rebinding, including that the game obeys the new key
python tools/test_handling.py      # DAS, ARR, DCD and SDF, measured in cells travelled
python tools/test_rotation.py      # 180 rotation, swept over every piece and position
python tools/test_spins.py         # spin detection under each rule, and the banner
python tools/test_settings.py      # the saved profile, including an actual relaunch
python tools/test_finesse.py       # the minimums, the counting, and what must not be judged
python tools/test_replay.py        # recording, the file, and that the fix moves nothing
python tools/test_attack.py        # the attack table, and the game feeding it
```

They write to a temporary profile rather than the real one, so running them will not
disturb your own settings.

## The C++ game

A rewrite is under way, one piece at a time, in `cpp/`: the board, the rotation
system with its kick tables, the finesse search, spin detection, the attack
table — and the game loop itself, frame-stepped: gravity, DAS/ARR/DCD/SDF, ARE,
hold, the forced drop timer, locking, line clears, the finesse retry, and the
scoring: spins, back to back, combos and attack.

With SDL2 installed, the same build now produces `forcetris`, a playable game
on that core: a clean dark board, real typefaces on a DPI-aware window,
mouse-driven menus, rebindable keys, the
forced drop meter, spin and combo banners, the same sounds and music as the
Python game — and a stat layout editor where every figure (PPS, APM, APS,
VS, finesse, back to back, combo, level, and more) is a panel you tick on
and drag wherever you want it, starting from presets, persisted between
runs. All three modes are there — free, timed, arcade with its garbage ramp
— and all three clear styles, cascades included. Finished games are recorded
to the same replay files the Python game writes, and either game can browse
and watch the other's, corrected finesse and all; high scores go into the
same `data/hiscore.dat`, byte for byte. A finished run gets the same
analysis the Python loss screen gives it — down to the wording, which the
cross test grades — plus detail tabs of its own (attack, speed, the pieces,
and an estimated Glicko/TR/rank, clearly labelled as the entertainment it
is), and How to Play explains the forced drop against whatever keys are
bound at the time.

```bash
sudo apt install libsdl2-dev    # or vcpkg install sdl2 on Windows
cmake -S cpp -B cpp/build && cmake --build cpp/build -j
./cpp/build/forcetris
```

The Python game remains the reference implementation, and the C++ core exists
to be graded against it rather than trusted. Instead of a second set of
assertions the C++ is held to the answers the tested Python engine actually
gives, two ways:

- `equivalence` — every piece's cells, every finesse route, ~130,000 rotations
  over ten boards with kicks on and off, every attack table entry, ~4,600 spin
  verdicts, and where every piece falls.
- `trace` — thirty scripted games are played through the Python engine,
  inputs frame-stamped, and the sim has to reproduce them move for move: every
  position the piece stands in, every lock and its frame, what each placement
  scored — spin verdict, back to back, combo, perfect clear, attack, score,
  downstack — every press logged and every stop of the movement trail, and
  every sound cue on the frame it fired. A one-frame timing slip anywhere
  shifts everything after it, so agreement means the loop's timing is right,
  not just its outcomes.
- `replay_cross` — the same scripted game played by both engines must write
  the same replay file, and a file written by either engine must mean exactly
  the same thing when the other reads it, re-enactment steps, the
  corrected-finesse view and the analysis screen's own rows — character for
  character against the text the Python screen renders — included.
- `hiscore_cross` — the same score submissions through both engines' codecs
  must produce byte-identical `hiscore.dat` files and announce the same
  placements, tie quirks faithfully included.
- `cascade_check` — the cascade movement loop's collision verdicts, pinned on
  the same hand-built boards `tools/test_cascade.py` pins the Python engine
  with: the one corner no natural game, and so no trace, can reach.
- `rating_check` — the analysis screen's rating estimate: the official TR
  conversion's fixed points, monotonicity in the inputs, and the rank
  ladder's order.

```bash
ctest --test-dir cpp/build --output-on-failure
```

See [cpp/README.md](cpp/README.md), which also explains why fourteen of the 104
kick table entries can never fire.

## Credits and licence

Built on [virtuNat/pyTetris](https://github.com/virtuNat/pyTetris), which is licensed
under the GNU General Public License v3. Forcetris is a derivative work and is therefore
distributed under the same licence — see [LICENSE](LICENSE). The forced drop timer, the
menus around it, and the synthesised sound effects are the changes made here; the engine,
art, and background music are virtuNat's.
