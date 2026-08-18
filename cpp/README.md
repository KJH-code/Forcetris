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
| `sim` | The game loop, frame-stepped: gravity, DAS/ARR/DCD/SDF, ARE, hold, the forced drop timer, locking, naive clears, the finesse retry, and the scoring — spins, back to back, combos, attack |
| `gui/` | The SDL2 + Dear ImGui game: board, hold, queue, forced drop meter, banners, menus, settings, and the stat layout editor |

## The GUI

```bash
sudo apt install libsdl2-dev        # Debian/Ubuntu; on Windows see below
cmake -S cpp -B cpp/build
cmake --build cpp/build -j
./cpp/build/forcetris
```

Arrows move, `Z`/`X`/`A` turn, space drops, down soft drops, shift or `C`
holds, escape pauses. Everything else is the mouse.

The stat panels beside the board are the point: pause → *Edit stat layout*
gives every stat a checkbox and makes the panels draggable, so PPS, APM, APS,
VS, finesse, back-to-back, combo and the rest sit wherever you put them. Four
presets (`tetrastats`, `battle`, `minimal`, `full`) are starting points; the
arrangement and all settings persist in a plain text file under SDL's
per-user pref directory (`FORCETRIS_GUI_CONFIG` overrides the path).

Settings — DAS/ARR/DCD/SDF/ARE, the forced drop timer, spin rule, finesse
rule, kicks — mirror the Python game's and apply from the next game.

On Windows, install SDL2 through vcpkg (`vcpkg install sdl2`) or point
`SDL2_DIR` at an unpacked SDL2 development package, then run the same CMake
commands. Dear ImGui is vendored in `third_party/imgui`, so there is nothing
else to fetch.

Headless machines can still prove the whole thing runs:
`FORCETRIS_SMOKE=1500 SDL_VIDEODRIVER=dummy ./forcetris` plays that many
frames of scripted-random input and exits; `FORCETRIS_SHOT=/path/out.bmp`
saves the final frame. The `gui_smoke` ctest does exactly this.

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
- thirteen scripted games replayed move for move through the sim: seeded
  random button-mashing across the handling range, plus deliberate scripts
  for line clear timing, finesse retries, DCD cuts, the retry keeping the
  forced drop time a piece had already spent, back-to-back quads down a
  seeded well ending in a perfect clear, and a tucked mini T-spin armed by a
  rotation the kicks refuse
- what every one of those placements scored — the spin verdict, the back to
  back and combo counters, the perfect clear flag and the attack — plus the
  two flags the verdict reads, compared at every one of the ~190 locks

The sim's port was verified the hard way: twenty-six deliberate mutations —
the lock grace a frame short, the DAS `+1` dropped, ARR 0 stepping instead of
sliding, the first spawn using ARE, Python's half-to-even rounding replaced
with C++'s, the spin half of the back-to-back gate dropped, the combo bonus
read off by one, the spin flags surviving a spawn, and so on — each fail at
least one trace. Where a mutation survived, the trace set was extended until
it did not.

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

No audio, no replay files, no rebindable keys yet. The Python game remains
the reference implementation and keeps all of those. The sim plays free mode
with naive clearing, which is the trainer's default; the cascade clear modes
and arcade's garbage ramp are not ported.
