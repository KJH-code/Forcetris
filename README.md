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
```

`--forced-delay` takes seconds as a float, so `0.45` is fine. Negative values are
clamped to 0.

The HUD shows the time the active piece has left under **Forced Drop**, and turns red
over the last quarter of the budget.

## Controls

| Key | Action |
| --- | --- |
| Left / Right | Shift |
| Down | Soft drop |
| Z or Left Ctrl | Rotate counter-clockwise |
| X or Up | Rotate clockwise |
| Space | Hard drop |
| Left Shift | Hold |
| Escape | Pause |

## How the timer behaves

The timer is deliberately strict, because a lenient one defeats the training:

- **It starts when a piece spawns**, not when it becomes movable.
- **Holding does not reset it.** A swapped-in piece inherits whatever time was left.
- **Soft dropping does not reset it.**
- **Wall kicks do not reset it.** They still reset the gravity counter, as in the base
  game — the drop timer is a separate clock.
- **It stops between pieces**, so the spawn delay is not charged against you.
- **It stops while the game is paused, while a menu is up, and while the window is out
  of focus.** Coming back from a pause does not instantly slam the piece down.

The timer is driven by `time.perf_counter()`, so it does not drift with the frame rate.
It is still only checked once per frame, so a drop lands on the first frame at or after
the deadline — up to 20 ms late at the game's 50 fps.

Everything else is the base game: 7-bag randomiser, SRS with Arika I kicks, hold with a
per-piece lock, ghost piece, T-spin detection, and the free / arcade / timed modes.

## Tests

`tools/test_forced_drop.py` drives the game loop headlessly against a fake clock and
checks each of the rules above. It needs no display or sound card.

```bash
python tools/test_forced_drop.py
```

## Credits and licence

Built on [virtuNat/pyTetris](https://github.com/virtuNat/pyTetris), which is licensed
under the GNU General Public License v3. Forcetris is a derivative work and is therefore
distributed under the same licence — see [LICENSE](LICENSE). The forced drop timer and
the CLI option around it are the changes made here; the engine, art, and music are
virtuNat's.
