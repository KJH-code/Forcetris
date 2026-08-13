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
percentage from 0 to 100, matching what the settings menu shows. Both clamp rather than
complain, and both only set the starting value — **Game Settings** retunes either one
without restarting.

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
| Space | Hard drop |
| Left Shift | Hold |
| Escape | Pause |

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
| Line Clears | Naive / Sticky Cascade / Linked Cascade |
| Music | Off, then 5% to 100% in 5% steps |
| Sound | Off, then 5% to 100% in 5% steps |
| Controls... | Rebind the gameplay keys |
| Handling... | DAS, ARR, DCD and SDF |

Changes apply immediately, including to the piece already falling and to the music
playing behind the pause menu, so you can pause mid-run, shave 0.05s off, and feel it on
the very next piece. They last for the session only — use `--forced-delay`, `--volume`
and `--sfx-volume` for values you want every time.

## Controls and handling

**Controls** rebinds the gameplay keys: pick a row, press the key you want. Taking a key
that another action holds removes it from that action rather than leaving one key firing
two things; an action left with nothing reads as *Unbound* until you give it a key.

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

The millisecond settings land on a 20ms grid, because the game runs at a fixed 50 frames
per second and auto-shift can only act on a frame boundary. Arcade mode's difficulty ramp
no longer touches auto-shift or soft drop — handling is yours, and only gravity climbs
with the level.

Unlike the other settings, controls and handling **persist**, in `data/controls.json`.
Rebinding that reset on every launch would not be worth having. A missing, corrupt or
unwritable file falls back to the defaults, which is also what the browser build gets.

## Sound

| Cue | When |
| --- | --- |
| move / rotate / hold | Shifting, rotating, swapping. Shifting speaks on the initial press only, not on every auto-shift step |
| lock | The piece settles under gravity |
| drop | You hard dropped it yourself |
| **forced** | The timer took the placement away from you |
| clear / tetris | One to three lines, or four |
| tspin | A T-spin landed |
| gameover | The stack topped out |

`drop` and `forced` are deliberately unalike — a low sawtooth buzz against the hard
drop's thud — because the whole point is hearing, without looking, that you ran out of
time rather than chose to place.

The effects are synthesised, not sampled, so the repository carries no third-party
audio. `tools/make_sounds.py` regenerates every file from the parameters at the bottom
of that script, so retuning a cue means changing a number and re-running it:

```bash
python tools/make_sounds.py
```

## How the timer behaves

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

Two things differ in the browser:

- **There is no command line.** The build runs on the defaults, so set the delay and the
  volumes in **Game Settings** instead.
- **High scores do not survive a reload.** The page gets an in-memory filesystem, so the
  score file is written and then thrown away when the tab closes.

Audio will not start until you have pressed a key, which browsers require. Since the
music starts when you pick a mode, this happens on its own.

### Native instead of the browser

Termux with an X11 server can in principle run the desktop version unchanged, but getting
pygame's SDL2 built there is its own project. The browser build is the supported path.

## Tests

All five suites run headlessly — no display, sound card, or browser needed.

```bash
python tools/test_forced_drop.py   # the timer rules above, against a fake clock
python tools/test_menus.py         # every menu button, driven by posted key events
python tools/test_web.py           # the properties the browser build depends on
python tools/test_controls.py      # rebinding, including that the game obeys the new key
python tools/test_handling.py      # DAS, ARR, DCD and SDF, measured in cells travelled
```

## Credits and licence

Built on [virtuNat/pyTetris](https://github.com/virtuNat/pyTetris), which is licensed
under the GNU General Public License v3. Forcetris is a derivative work and is therefore
distributed under the same licence — see [LICENSE](LICENSE). The forced drop timer, the
menus around it, and the synthesised sound effects are the changes made here; the engine,
art, and background music are virtuNat's.
