# The C++ core

The first step of an incremental port. **The game you play is still the Python
one** — this is the pure-logic half of it rewritten in C++ and graded against the
original, so that when the rest follows there is something underneath already
known to behave identically.

What is here:

| | |
| --- | --- |
| `piece` | The seven tetriminoes, their cells in each orientation, and the rotation maths |
| `board` | The matrix: collisions, dropping, pasting, line clears |
| `kicks` | SRS with Arika's symmetric I, and the SRS+ 180 tables |
| `finesse` | The search for the fewest presses a placement could have taken |
| `spins` | The three corner rule and the immobility rule, under each spin setting |
| `attack` | TETR.IO's garbage table, and the APM / VS arithmetic on top of it |
| `sim` | The game loop, frame-stepped: gravity, DAS/ARR/DCD/SDF, ARE, hold, the forced drop timer, locking, naive clears, and the finesse retry |

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
- eleven scripted games replayed move for move through the sim: seeded random
  button-mashing across the handling range, plus deliberate scripts for line
  clear timing, finesse retries, DCD cuts, and the retry keeping the forced
  drop time a piece had already spent

The sim's port was verified the hard way: twelve deliberate mutations — the
lock grace a frame short, the DAS `+1` dropped, ARR 0 stepping instead of
sliding, the first spawn using ARE, Python's half-to-even rounding replaced
with C++'s, and so on — each fail at least one trace. Where a mutation
survived, the trace set was extended until it did not.

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

No rendering, no audio, no menus, no replay files, no settings, no scoring —
those still live in Python. Nothing in the game calls into this yet. The sim
plays free mode with naive clearing, which is the trainer's default; the
cascade clear modes and arcade's garbage ramp are not ported.
