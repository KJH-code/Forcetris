# The C++ core, and its GUI

An incremental port. The pure-logic half of the game is rewritten in C++ and
graded against the Python original, and `forcetris` — built when SDL2 is
available — is a playable game on top of it: the graded sim fed from the
keyboard, with Dear ImGui screens, mouse-driven menus, and a stat panel
layout you drag into shape.

What is here:

| | |
| --- | --- |
| `piece` | The seven tetriminoes, their cells in each orientation, and the rotation maths |
| `board` | The matrix: collisions, dropping, pasting, line clears |
| `kicks` | SRS with Arika's symmetric I, and the SRS+ 180 tables |
| `finesse` | The search for the fewest presses a placement could have taken |
| `spins` | The three corner rule and the immobility rule, under each spin setting |
| `attack` | TETR.IO's garbage table, and the APM / VS arithmetic on top of it |
| `sim` | The game loop, frame-stepped: gravity, DAS/ARR/DCD/SDF, ARE, hold, the forced drop timer, locking, all three clear styles, the finesse retry, the scoring — spins, back to back, combos, cascade chains, attack, the score itself — the sound cues, timed mode's clock, arcade's level ramp and garbage, the cheese race and cheese survival, and everything the recorder writes down |
| `replay` | The replay files: the same JSON the Python game writes, read and written here, with the re-enactment and the corrected-finesse view |
| `hiscore` | The high score table: the same data/hiscore.dat, byte for byte, quirks and all |
| `rating` | An estimated Tetra League standing - Glicko, TR by the official conversion formula, rank - interpolated from TETR.IO's own reported per-rank averages, and labelled the estimate it is |
| `gui/` | The SDL2 + Dear ImGui game: board, hold, queue, forced drop meter, banners, sound, music, menus, settings, the stat layout editor, the replay browser and viewer, the three game modes, the high score screens, the how-to-play screen and the analysis of a finished run |

## The GUI

```bash
sudo apt install libsdl2-dev        # Debian/Ubuntu; on Windows see below
cmake -S cpp -B cpp/build
cmake --build cpp/build -j
./cpp/build/forcetris
```

The screens are typeset in whatever real font the machine has - Segoe UI on
Windows, DejaVu or Noto on Linux, `FORCETRIS_FONT` to choose one by hand -
and the window is laid out per-monitor DPI aware, so a scaled display gets
a sharp window at the right size instead of a stretched blur of the 96dpi
one.

Arrows move, `Z`/`X`/`A` turn, space drops, down soft drops, shift or `C`
holds, escape pauses. Everything else is the mouse — and all of it except
escape is rebindable: the settings screen lists every action, a click on a
bound key unbinds it, and `+` grabs the next key you press. An action can
hold any number of keys, but a key serves one action - binding it somewhere
new takes it away from where it was, so nothing fires twice.

The stat panels beside the board are the point: *Edit stat layout* - from
the pause menu, or from the settings screen's Layout tab, which works from
the main menu too by standing a preview board up behind the editor - gives
every stat a checkbox and makes the panels draggable, so PPS, APM, APS,
VS, finesse, back-to-back, combo and the rest sit wherever you put them.
Four presets (`tetrastats`, `battle`, `minimal`, `full`) are starting
points; the arrangement and all settings persist in a plain text file under
SDL's per-user pref directory (`FORCETRIS_GUI_CONFIG` overrides the path).

Settings — DAS/ARR/DCD/SDF/ARE and the forced drop timer, the spin, clear
and finesse rules, kicks, the volumes, the key bindings and the stat layout
— sit in one tabbed screen and mirror the Python game's. Handling applies
from the next game; keys, volumes and layout apply at once.

All three of the Python game's modes are here - free, timed with its five
minute clock and closing score multiplier, arcade with its level ramp and
rising garbage - and so are all three clear styles, the two cascade ones
included. Two more modes are this side's own, with no Python counterpart:
a cheese race - ten, eighteen or a hundred rows of holey garbage, dug as
fast as you can, the clock stopping the moment the last of it is gone -
and cheese survival, where the floor rises on a clock you pick (every
eight, five or three seconds) until the stack wins. The cut of the cheese
is picked with the mode: one to three holes per row, and a messiness from
Clean - every row's holes right under the last one's, a well - through
Full, where no two rows in a row ever align.
Their behaviour is spelled out by the `cheese_check` ctest instead of a
cross-grading, since there is nothing to cross against; a Cheese stat
panel counts what is left to dig, or what has risen. Cheese games are
recorded and analysed like any other, but stay off the high score file.

A finished game that places on the high score table is offered a
name entry, and the table is the Python game's own data/hiscore.dat, read
and written byte-compatibly; the High scores screen shows the same three
pages the Python game shows.

A finished run is laid out the way the Python analysis screen lays it out -
score, pieces, lines and time; the finesse rate, the faults, what the run
cost in presses and what it would have cost without them; attack, VS, the
clears by size, spins, perfect clears and the best chains - and the same
rows open from the replay browser for any saved recording. Those overview
rows are built in the core and graded against the Python screen's own text,
so the two games say the same thing about the same run. Around them the
analysis screen grows tabs the Python game never had: Attack (APM, APP,
APL, downstack per piece and per second, VS/APM), Speed (peak ten-piece
PPS, the run's halves, KPP, KPS, holds, forced drops), Pieces (the deal by
form, clears by size, spins and minis, the best chains) - and Rating, an
estimated Glicko, TR and rank for the run. The estimate places your APM,
PPS and VS against TETR.IO's own reported per-rank averages (APM, PPS, VS
and TR, taken off the live leaderboard breakdown) and runs the official TR
conversion over the result; there is no opponent in a trainer,
so it is an entertainment-grade placement, and the screen says so in as
many words. Play opens a mode picker - Free, Timed, Arcade, the cheese race at three
lengths and cheese survival at three paces, with the holes-per-row and
messiness of the cheese beside them - with a line on what each one does. How to Play lists every
action against the keys bound to it right now, and explains the forced
drop, in the Python screen's words.

The sound is the Python game's sound: the same synthesised WAVs out of
sound/, the same music out of music/, fired by the cues the sim itself
raises - which the trace harness grades, so what you hear is what the
engine decided, frame for frame. Finished games are saved to data/replays
in the same JSON the Python game writes; either game can browse and watch
the other's recordings, re-enacted stop by stop with the piece walking its
recorded trail, or the finesse-corrected route with *Perfect finesse* on.

On Windows, install SDL2 through vcpkg (`vcpkg install sdl2`) or point
`SDL2_DIR` at an unpacked SDL2 development package, then run the same CMake
commands. Dear ImGui is vendored in `third_party/imgui`, so there is nothing
else to fetch.

Headless machines can still prove the whole thing runs:
`FORCETRIS_SMOKE=1500 SDL_VIDEODRIVER=dummy ./forcetris` plays that many
frames of scripted-random input and exits; `FORCETRIS_SHOT=/path/out.bmp`
saves the final frame. Between games it tours the screens a game never
opens - how to play, both high score pages, the replay browser, the
analysis of the run just finished, and that same analysis screen with
nothing to show - a few frames each, with their real data behind them,
and fails if it finished a game and did not finish the tour. That proves
the screens open and draw against real replays and a real score table; it
is not a substitute for the cross tests, which is where the text on them
is actually graded. The `gui_smoke` ctest does exactly this. With
`FORCETRIS_SHOTS=<dir>` the run also leaves one BMP per screen it visited -
the way a design change is looked at, screen by screen, without a display.

## Building and grading it

```bash
cmake -S cpp -B cpp/build
cmake --build cpp/build -j
ctest --test-dir cpp/build --output-on-failure
```

Two tests dump what the Python engine does — `tools/dump_reference.py` for the
pure logic, `tools/dump_trace.py` for whole scripted games — and check the C++
gives the same answers. Nothing is compared against a stored
file: the reference is regenerated every run, so the check is against the engine
as it stands rather than as it once was. Python and pygame have to be installed
for that reason.

## Why it is graded rather than tested

A port that merely looks right is worth nothing. The Python side has ten test
suites behind it, so rather than writing a second set of assertions by hand the
C++ is held to the answers the tested implementation actually gives:

- every piece's cells, in every orientation
- every finesse route, placement by placement
- every rotation, over ten boards including seeded rubble, both with kicks and
  without — around 130,000 of them
- every attack table entry, and the APM / VS arithmetic
- every spin verdict, under each rule, kicked and not — around 4,600
- where every piece falls on every board
- thirty scripted games replayed move for move through the sim: seeded
  random button-mashing across the handling range, plus deliberate scripts
  for line clear timing, finesse retries, DCD cuts, the retry keeping the
  forced drop time a piece had already spent, back-to-back quads down a
  seeded well ending in a perfect clear, a twelve-clear combo chimney, a
  tucked mini T-spin armed by a rotation the kicks refuse, a T kicked off
  the wall into a twist-scored clear, soft-dropped gravity locks, a clear
  that empties the bottom row under a floating band of rubble, a timed game
  run to its clock's end, two more whose clock dies with a clear still
  resolving - once on a row-scan resume, once on the frame the score
  lands - arcade at every garbage tier with the piece riding the rising
  stack, and the cascade styles - sticky and linked -
  over seeded rubble, under arcade garbage, off a high shelf and around a
  garbage block nailed in mid-air
- what every one of those placements scored — the spin verdict, the back to
  back and combo counters, the perfect clear flag, the attack, the game
  score and the downstack — plus the two flags the verdict reads, the press
  log, the movement trail, the hold provenance and the queue snapshot,
  compared at every one of the ~240 locks
- every sound cue the engine fired, by name, on the frame it fired — nearly
  a thousand of them across the traces
- the replay files, both ways: the same scripted game played by both engines
  must produce the same file, a file written by either engine must read
  identically in the other (re-enactment steps and corrected view included),
  and a synthetic file exercising the format's corners must too
- the analysis screen's own rows, character for character: the C++ text is
  compared against what the Python AnalysisMenu actually renders, so a
  rounding or a plural out of place is a failure, not a detail

The sim's port was verified the hard way: over fifty deliberate mutations —
the lock grace a frame short, the DAS `+1` dropped, ARR 0 stepping instead of
sliding, the first spawn using ARE, Python's half-to-even rounding replaced
with C++'s, the spin half of the back-to-back gate dropped, the combo bonus
read off by one, the spin flags surviving a spawn, the soft drop scored at
the hard drop's rate, the twist multiplier dropped from the score, the combo
cue's ladder uncapped, the hold flag never reset, a replay's counters written
raw instead of as the HUD shows them, the corrected view inventing routes for
unjudged placements, and so on — each fail at least one trace or the replay
cross check. Where a mutation survived, the harness was extended until it
did not.

## The cascade repairs

The sticky and linked clear styles shipped broken upstream, in ways the
naive default hid: the fills indexed off the right wall and wrapped around
the left one, they were seeded from block positions a row splice had
already invalidated (so clearing a garbage row under cascade hung the game
for good), sticky groups were declared stuck by fiat so nothing ever fell,
and a shape blocked by another floating shape was re-queued unmoved
forever. The Python side was repaired first - bounds-checked fills seeded
from real coordinates, fills that stop at settled blocks instead of
dragging garbage down, one collision verdict per moved shape, sticky
groups that actually fall - pinned by tools/test_cascade.py, and the C++
port is graded against the repaired behaviour. The movement loop's
settled-collision verdict - a block whose dangling down link exempts it
from the resting test, stopped by the collision instead - is out of any
natural game's reach, so tools/test_cascade.py builds the board by hand
and the `cascade_check` ctest holds the C++ loop to the same ending,
fallen flag included; the blocked-by-floating verdict beside it is a pure
backstop, since the fills absorb any floating blocker before the movement
loop could meet one. Two mutations of the link surgery survive as provably
equivalent: a cell under a surviving link mate can never be refilled while
the mate stands, so the stale link the surgery exists to remove is never
followed anywhere it matters.

## Kick table coverage

A sweep that runs a hundred thousand rotations still proves nothing about a
table entry it never reaches, and the entries deep in a table are exactly the
ones a port gets wrong. So the sweep counts which candidate each kick settles on
and reports it:

```
kick table entries reached: 90 of 104
  14 unreachable by the floor kick rule
  0 reachable but not reached
```

The fourteen are not a gap. A kick that lifts the piece may only be tried once,
so the first upward candidate in a table spends the allowance and every upward
candidate after it is skipped rather than tested — those entries cannot fire
however awkward the board is. The dumper works that out from the tables
themselves rather than taking it on trust, and fails if anything reachable goes
unreached.

The hand-built boards alone reached only 85, and a deliberately corrupted kick
entry went undetected. The seeded rubble boards exist because of that.

## What it does not have

The game and its screens are all here now - the three modes, the three
clear styles, the high score table, the sound, the replays, the analysis
of a finished run, the how-to-play. What stays Python-only is the pygbag
web build, which is a packaging job rather than a port. The Python game
remains the reference implementation either way: every answer the C++
gives is graded against it, never assumed.
