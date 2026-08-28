# The C++ game, and its graded core

The product half of the repository: `forcetris` — built when SDL2 is
available — is the variant itself, the fuse and Flow and Overdrive on top
of a pure-logic core that began as an incremental port of the Python
original and is still graded against it move for move. Dear ImGui screens,
mouse-driven menus, a stat panel layout you drag into shape, and the
variant's own modes, career and score tables on top.

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
| `temper` | The game's central gimmick: the ten-card temper pool, what each card does to the rules, the weighted roll that offers three a heat, the heat counter every screen shares, and the bot's rank-tempered pick |
| `campaign` | The Forge Map's machinery: the seeded branching map generator (`forgemap.cpp`), the run state and its difficulty arithmetic, the chapter gate, the star and slag arithmetic, the Anvil's permanent upgrades, and campaign.dat. The content - the chapter table and every stage recipe - lives apart in `stages.cpp`, so growing the road edits one file. The game's design itself (audience, goals, roadmap) is written down in the repository root's `DESIGN.md` |
| `munch` | MinoMuncher's statistics re-derived over this game's own records (formulas from the MIT-licensed minomuncher-core): the nine clear buckets, spin efficiencies, the three-way attack-per-line split, burst and plonk PPS by Gaussian mixture, the four-deep well rule, the surge accounting and the cheesiness sigmoid - scored over raw attack, a trainer having no multiplayer wire |
| `profile` | The history: one tolerant key=value line per finished game, appended forever, read back for the profile screen's aggregates and growth charts |
| `bot` | The versus opponent: a full-reachability search over the real kick tables - so its tucks and spins are exactly the game's - an attack-and-shape evaluation, a rank ladder paced to TETR.IO's own per-rank speeds, and the driver that types the plan into a sim one key at a time |
| `gui/` | The SDL2 + Dear ImGui game: board, hold, queue, forced drop meter, banners, sound, the generated furnace bed and Forge score, menus, settings, the stat layout editor, the replay browser and viewer, the game modes - versus included, with the bot's board beside yours - the high score screens, the how-to-play screen and the analysis of a finished run |

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
holds, escape pauses, and `R` restarts the run - mid-game, paused, or from
the loss screen. Every game opens on a three-second countdown over the
frozen board, so clicking Play never throws the first piece at you cold;
in a versus match the bot waits through it too, and every round gets its
own. Everything else is the mouse — and all of it except
escape and `R` is rebindable: the settings screen lists every action, a
click on a
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

The handling the game *ships* with is TETR.IO's own defaults, digit for
digit: DAS 167, ARR 33, DCD 17, SDF 6, ARE 0. Beginners arrive from the
game everyone learns on and lower the numbers as they improve — that is
what improving *is* — so the defaults are where they start, not where a
stacker ends up. Two things stay banished whatever the handling says: the
clear-animation freeze (an animated quad froze the board for 580ms; no
modern game has that, and `feel_check` pins it at one frame) and the spawn
delay. The Handling tab leads with three buttons — **Standard** (what
ships), **Instant** (the stacker's 100/0/40: straight to the wall,
straight to the floor) and **Trainer** (the Python trainer's numbers, kept
whole, animated clears and all) — and prints what the current sliders cost
in milliseconds underneath. A config file still on an older build's
shipped numbers is brought forward once and stamped `handlingrev`; a file
whose handling the player chose - Trainer, or hand-typed values - keeps
it and only picks up the stamp.

All three of the Python game's modes are here - free, timed with its five
minute clock and closing score multiplier, arcade with its level ramp and
rising garbage - and so are all three clear styles, the two cascade ones
included. The clear delay sits in the Handling tab, where it is felt:
animated - the reference timing, seven frames a clearing pass - or off,
where a clear resolves on the lock frame and the next piece follows
immediately, the way TETR.IO plays it. Nothing about the outcome changes but the clock
(the `clear_check` ctest holds the two modes to identical scores,
counters and boards), though a no-delay run naturally fits more pieces
into the same minutes - worth remembering when reading the score table.
The bot plays under whichever you pick, its pace re-tuned so its rank
dial still means what it says. And above all of it sits the fuse - the
variant's own ruleset, on by default and switchable off in Rules: every
piece burns a per-level fuse and is slammed down when it runs out, clears
bank refuel for the pieces to come, and the Flow rail on the board's
left flank charges on quality - the lines and attack a clear resolves
into, so spins, quads, back-to-backs and perfect clears fill it while
haste alone barely moves it (a lock inside the Flash window adds a
little). A full rail ignites Overdrive: the fuse frozen, score and
attack multiplied, every clear burning a garbage row off your own floor
besides - the backdraft that makes it a digging window too - with the
board rimmed in gold until it gutters out. Duel burns it
on both sides: the bot's driver types inside the fuse - a rank slower
than the burn abandons its pace rather than its piece, the way a rushed
player would - replans cleanly when a slam beats it to the lock, and
reaches Overdrive on its own merit, more of it the higher the rank.
And igniting is an attack in its own right: while either board's
Overdrive burns, the other board's fuse burns almost half again as fast
- heat pressure, both ways - so a duel swings between pressing and being
pressed rather than trading quiet bonuses. The screen fights along:
pieces smoulder and shed embers as their fuse runs down (white-hot when
the pressure is on you), red heat closes in at the edges as danger
mounts, attack arcs between the boards as ember streaks that burst
where they land, garbage thuds home with a flash and a shudder, and an
ignition flashes the screen, cries OVERDRIVE across the board and runs
the music hot until it gutters out. The well is a furnace: its floor
glows warmer the worse the trouble, the grid fades towards the sky, the
whole crucible haloes as the Flow gauge fills, and a cleared row goes
white-hot and throws embers off both ends before it goes. Garbage wears
burnt slag rather than your own colours, the ghost is an outline that
never hides in the stack, and the hold box hatches over when it has
already been spent. Behind all of it the room itself burns: a molten
horizon along the floor that banks up with the danger, heat shafts
leaning as they climb the margins either side of the board, smoke
drifting through them, and embers rising past - all of it brightening
as the Flow gauge fills and blazing in Overdrive.

There are two hand-built sprites and no image files. The first is a soft
falloff, which every glow in the game is a stamp of, so the light is
smooth instead of banding into rectangles. The second is fire, and it is
deliberately the opposite: sixteen frames of a flame on a 38x88 grid,
three octaves of value noise warped by a taper wide at the foot and
pinched at the tip, then quantised onto a five-colour ramp with nothing
in between and scaled up nearest-neighbour to whole pixels. Pixel art,
in other words. The noise lattice wraps over exactly the distance the
frames scroll, so the strip loops with nothing to see at the seam, and
each frame leans differently, so cycling them licks rather than
flickers. It burns in one place: the mark beside FORCETRIS on the main
menu. Everywhere else the menus stay cool - a centred panel is a plate
off the forge, dark iron with a bevel lit from above, rivets and a rim
that pulses like metal fresh out of the coals, over the drifting embers
the backdrop has always had.

Overdrive is light, not shapes. The well is framed like a filament -
rails either side and a lip above and below, white hot behind a wide
soft bloom - a second bloom sits close behind the board so the board
reads as the thing the light comes from, and fine motes drift up through
the room. All of it is the same falloff sprite, stamped once very large
and many times very small.

What matters as much as any of that is the dark. Light spread evenly
over the whole screen does not read as a board that is glowing; it reads
as fog. So the blooms are kept close and low and the room stays black,
and the contrast is what does the work. On top of it sits everything
Overdrive always had: the gold screen flash, the OVERDRIVE cry, sparks
off the Flow rail, the heat vignette and the music running hot.

Flames and speed lines were both tried here first and both were worse
than nothing. The lesson was that a drawn *thing* on a dark screen reads
as a shape someone cut out, however carefully it is drawn.

A finished game says three things and stops: the verdict, the score, and
the estimated rank, out of the same call the analysis window's Rating tab
makes so the two can never disagree. Everything else the run produced is
one button away, under *Full analysis*, which is where it was all along -
the loss screen used to repeat those rows underneath, which is what made
it tall enough to run its last button off the bottom of the display. It
is also capped at the display height now, so a panel that cannot fit
scrolls instead of losing its buttons.

Career, off the main menu, is the long game: The Ladder sends you up the
bot's ranks in order - a win opens the next rung, a sweep pays two stars,
a sweep with Overdrive ignited pays three, and every rung tightens the
fuse a notch while the top half fights first-to-two. The Daily is one
Ignition run a day on a seed derived from the date - the same fuse for
everyone who shares it - and the attempt is burned the moment it starts,
so walking out spends it too. Progress lives in data/career.dat, a
tolerant key=value file like the profile's (the `career_check` ctest
pins it), and the smoke harness runs against its own career file so a
test run can never spend your daily. A
fuse-rules replay writes every tunable into its meta, so a file always
says which game its score belongs to (`fuse_check` pins the ruleset and
the meta round trip both). On a desk the renderer runs uncapped by default - vsync off, the loop
paced by a millisecond nap - which cuts a frame or two of input latency;
the Rules tab's Low-latency toggle brings vsync back for tearing-averse
displays, and phones keep vsync regardless. Three more shavings: the
audio buffer is 256 samples (about 6ms), so the keypress cues land with
the press instead of 23ms behind it; a game key lets the sim borrow its
next tick early - one tick at most, repaid by the accumulator, so the
pace never changes but the press never waits out a 20ms boundary; and
F11 flips fullscreen, which on Windows trades the compositor's extra
frame for the direct path. Input is not spread out the way the
Python engine's one-event-per-frame poll spreads it: every press and
release that arrived since the last 20ms frame lands on the next one, in
order, so a quick tap-rotate-drop is on the board the frame after the
fingers made it (the `input_check` ctest holds a burst frame to the same
placement the spread-out frames reach). What is left after all of that is
not the loop but the handling, so `feel_check` counts it: the frames
between a hard drop and the next piece a hand can move, the frames a held
soft drop takes to reach the floor, and the frames a held direction takes
to reach the wall - measured on the config the game ships with, so
"sluggish" is a number that either moved or did not.

**The picture is paced as carefully as the sim.** The engine steps on a
fixed 20ms grid (that grid is what the Python equivalence is graded on,
so it never changes), but displays run at 60Hz and up, and drawing the
piece only on the grid makes single moves beat against the refresh and
read as judder. Smooth motion - on by default, a Rules-tab checkbox -
draws the falling piece part-way between its last two engine steps
instead: presentation only, and only for a plain step (same rotation, at
most one column across, one row down); rotations, spawns, hard drops and
ARR-0 teleports still snap, and a step that carried a key press snaps
too, so input latency is untouched. On Windows the SDL timer subsystem
is initialized alongside video so the pacing nap actually sleeps ~1ms
(without timeBeginPeriod it can oversleep to ~15ms and bunch engine
ticks into some frames while starving others). F3 toggles a frame
diagnostics overlay - average and worst frame time, and how many engine
ticks each drawn frame carried - so a stutter report can arrive with
numbers attached.

**The draft is the game's whole gimmick.** Every game played under the
fuse rules climbs the forge in heats - ten cleared lines each, six dug
rows in Meltdown - and crossing a heat is where the forge tightens the
fuse *and* offers three tempers, of which one is taken. The board and the
fuse wait while the cards are up; the pick is one press (`1` `2` `3`, the
arrows and Enter, or a tap). The trainer rules never draft: the draft is
part of the fuse ruleset, and turning the fuse off in Rules turns the
whole forge off with it.

The pool is ten cards in four families, and a card's face carries no
numbers - a family word and glyph, a name, and one plain line, because a
card that needs a manual has already failed. **Fuel** survives: *Thick
Wick* (pieces burn longer, `fuse_base +0.5s`), *Quench* (clears refill
more fuse, `+0.3s` a line), *Slow Burn* (the forge tightens slower,
`fuse_decay -0.05s`). **Flow** presses: *Bellows* (`overdrive_secs +3`),
*White Heat* (`overdrive_mult +0.5`), *Spark* (`+2` Flow per line and per
attack). **Risk** trades: *Overheat* (all Flow gains doubled, the wick
half a second shorter), *Gamble* (`overdrive_mult +1.0`, a burnt piece
costs `15` more Flow). **Rule** rewrites: *Collapse* (clears become a
sticky cascade), *Every Twist* (every spin scores, minis included). Fuel
and Flow stack two or three deep; Risk and Rule are one each - nineteen
stacks in all, and when they run out the forge simply stops dealing.

**Tempering** is the flagship: the run with a finish line. Twelve heats,
a draft at each, and clearing the twelfth forges the blade - **Forged** -
while topping out leaves the run what it got to. Its score goes to its
own table. The daily, being a fixed-seed run, offers everyone the same
cards at the same heats.

A **Duel never stops**. There are no drafts in versus, either side - the
freeze that lets a hand read cards has no business in a real-time fight -
and the heats only tighten the fuse there. The bot arrives *armed*
instead: every rank carries a fixed blade (`temper::blade_for`, D's
single thick wick escalating to X's overheat-and-gamble build, never
Collapse - its planner searches naive clears), applied to its rules at
round start and shown under its board, and written into its embedded
recording the way a drafted build would have been.

The draft screen is also where the run's coin is spent. Clears earn
**embers** - `embers_of`: two a line, three per point of attack, and
nothing for haste - and the purse buys a **reroll** of the three cards
(6) or a **second pick** off the same table (14). The balance is derived
from the sim's own totals minus what was spent, so it cannot drift, and
it dies with the run - which is where the campaign picks up.

**The Forge Map** is what became of the career screen: a chapter played
as one seeded climb. Setting out builds a branching graph six rows deep -
two doors at the entrance, two or three lanes through the middle, the
chapter's boss alone at the top - as a pure function of (chapter, seed),
so the save file stores two numbers and the path picked, and
`campaign_check` grades the generator's promises (shape, connectivity,
non-crossing edges) across dozens of seeds without a window. Battle nodes
draw from the chapter's recipe pool with the easy fires at the gate and
the hard ones under the boss; each is a declarative recipe over the same
engine - a line quota over preset rubble, a three-hole dig, cascade-only
clears, a floor that rises, a stage that starts with Overheat already in
your blood - and the boss duel carries its own blade (the Forgemaster
fights two falls behind bellows, white heat, overheat and gamble).

The roguelite is that the build outlives the battle: a won node banks its
embers into the run and deals **the spoils** on the map - three cards,
take one or take nothing, reroll or a second pick paid from the run's
purse - and every temper picked rides into every later battle of the
climb, forged into the player's rules before the first piece falls. A
stage never drafts mid-game any more; on the Forge Map the board never
stops. Not every node fights, either: each map scatters **one forge**
(a free hand, and the melting pot - eight embers unmakes a temper you
regret) and **one or two events** (a single card of choice, seeded like
everything else: sell scrap, tithe embers into slag, catch a stray
spark, quench your last pick) through the middle rows; entering a stop
spends it, so it can never be farmed. And two stages carry gimmicks the
sim never sees: The Dark Gallery lights only a lantern that glides after
the falling piece, and Smoke in the Rafters smokes the queue over past
one piece - presentation only, the graded engine untouched, which is
the pattern every "seen, not simmed" gimmick follows. A survival floor
rising now lands with a shudder and a thud, so the recipe's quake is
heard as well as suffered. What death costs is picked at the door: **mild** re-offers the
node, **forged** spends one of three lives, **white-hot** ends the climb
outright - and heavier fires pay 150 / 200 percent slag. Death always
renders unspent embers down to slag - the prestige loop, "the run's coin
dies, the metal stays". Slag buys permanent upgrades at **the Anvil** - a
longer wick, a deeper bank, a greater bellows, ember sense, Preheat's
free spoils at the door - and that metal rides into campaign battles
*only*: their records say `campaign`, a name no score table owns, so an
Anvil-boosted run can never touch the pure modes' tables. Stars still
accrue per stage id (clear / under par / no forced drops; bosses count
win / sweep / ignited sweep) and the next chapter opens on the previous
boss's star. Progress - the run in flight included, as `run_*` keys that
simply stop being written when the climb ends - lives in `campaign.dat`,
the same tolerant key=value file the career and profile keep; the old
ladder's career.dat stays untouched beside it, and The Daily survives
unchanged.

Every temper is one or two numbers out of `SimConfig`, which is what makes
the gimmick cheap and honest: the sim reads its fuse and Flow values live
at every use rather than deriving anything from them at construction, so
`Sim::retune` replaces them mid-run and the next piece is dealt the new
schedule. Handling is deliberately not among the fields it copies - a
draft may change the game, never the pad. The pool, the roll, the heat
counter and the bot's pick all live in the core (`temper`), so a replay
records the build a run was played with - the bot's side included, in the
embedded opponent. `temper_check` grades the whole of it: every card
against the numbers it claims *and* against the ones it must leave alone
(the whole `SimConfig` is compared field by field, so a card that quietly
moves something it never declared is a failure), the roll's repeatability
and its caps, the heat counter, the bot's pick - deterministic, biased by
rank, never Collapse - a retune landing on the next piece without
disturbing the handling, and a run driven all the way to its twelfth
heat. One honest asterisk: drafted runs score higher than the undrafted
runs the tables already hold, so a table crossing this change compares
eras, not just players.

Two more modes are this side's own, with no Python counterpart:
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

And there is someone to play against: a versus mode, first to one, two or
three rounds, against a bot picked by TETR.IO rank - D through X, each
paced to that rank's real league speed, blundering as often as its rank
would, and gated to its rank's technique: the low ranks hard-drop, tucks
arrive around B, quad-well building around A, spins and T-slot keeping
around S with one piece of lookahead - and from SS up the planning
changes kind: a deterministic beam search, full reachability at every
ply of the real preview queue with the hold weighed at every step, so a
spin set up now and hit two pieces later is seen and chosen, which no
hard-drop lookahead can do. The bot is our own, written referencing the
published techniques of the well-known bots (MisaMino, ColdClear, and
the beam-search shape of the modern engines): a full-reachability search
- taps, sonic drops, and rotations through the game's own kick tables,
so a slide under an overhang or a kicked T-spin is found the way a
strong player finds it, and behaves exactly as the sim will judge it -
under an attack-aware evaluation. From A up that evaluation plays for
keeps the way those ranks actually play: it reserves one well and banks
rows against it instead of fearing the hole, treats a clear that is not
a quad or a spin as stack spent for nothing - unless it is digging out a
buried hole - holds its back-to-back chain (and the Surge charged on it)
dearly, and past a dangerous stack height drops all of that and digs
with whatever clears at all; the beam ranks add a bonus for every Surge
row banked and a hard penalty on any stack shadowing the spawn. Under
the trainer's default all-spin rule the beam ranks play it the way the
all-spin bots do - wedged S, Z, L and J spins chained to hold the
back-to-back and charge Surge - because the search scores every kicked
rotation with the game's own judge; measured over seeded 300-piece runs
the X bot lands ~0.79 attack a piece with fifty-odd spin clears, against
0.43 for the greedy builder and 0.10 for the plain downstacker. No bot
code is copied.
The garbage rules are TETR.IO's multiplayer shape: attack in flight
cancels first, what survives rises through the floor up to eight rows a
lock, and a back-to-back chain held to four or more starts charging
**Surge**, the whole charge landing in one burst when the chain finally
breaks - an approximation of TETR.IO's current chaining, charge-at-four
and fire-on-break, not a byte-exact port. The bot's board stands beside
yours with both sides' incoming garbage metered in red; rounds replay on
a draw, and the match verdict takes over the finish screen. Versus games
are analysed like any other and stay off the high score file - and every
round leaves a replay, not just the match's last: the file carries the
bot's whole side embedded under an optional key both engines' readers
simply skip when they do not know it, and the viewer stands the bot's
board up beside the re-enactment, synced to the player's clock, so a
watched round shows both halves of the fight. The exchange, the cap, the
cancellation and Surge are pinned by the `versus_check` ctest; the bot
by `bot_check` - it must survive hundreds of pieces through the real
sim, land exactly where it planned, repeat itself under a seed, hold its
pace, dig under fire, reach a cavity only a tuck can enter, spin a T
into a real TSD slot, and - building - fire quads, charge Surge and
out-attack the plain downstacker, while a rank without the technique
must not; the beam is pinned to roof a roofless TSD slot for the T
behind it - the move whose payoff only depth can see - then spin that T
in through the real sim, stay deterministic, stay on its time budget,
and keep a spawn-shadowing placement at the bottom of its list; the embedded side by `opponent_check`, and the whole loop -
record, save, watch with two boards - by `gui_smoke_versus`, headlessly.

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
many words. A Munch tab chews the run the way MinoMuncher would - clear
buckets, spin efficiencies, upstack against downstack against cheese
attack-per-line, burst and plonk PPS, well loyalty, surge conversion, and
the two style verdicts (upstacker to downstacker, lean to greasy) by that
tool's own twenty-point zones. Every finished game - versus rounds
included, with their verdicts - also writes one line of history, and the
main menu's Profile screen reads it all back: lifetime totals, bests and
the versus record on one tab, growth charts of PPS, APM, VS, estimated TR
and finesse - each with a ten-game moving average - on the next, and the
munch averages on the third, all filterable by mode.
Play opens a mode picker in the variant's own names, Tempering - the run -
leading it. Ignition (endless, the fuse shortening per level), Blaze
(three burning minutes) and Inferno (the rising floor) start at a click,
while Meltdown / Bunker and Duel each open their own window: the cheese
one holds Meltdown's race at three lengths, Bunker's survival at three
paces, and the holes-per-row and messiness dials; the duel one the bot's
rank row, the first-to count and the Fight button - and every one of them
drafts, because every one of them burns the fuse. A fuse-rules game
scores into the variant's own seven tables (fusescore.dat, one per mode,
Duel included for the day it fights fused); the trainer's three-table SFH
file keeps its bytes and its meaning, and the scores screen shows all ten
side by side. Game history and replays carry the matching keys - ignition,
blaze, inferno, meltdown, bunker, duel, temper against the frozen legacy
names - so no record ever changes game under your feet. A variant file
written before a table was added is read as far as it goes and the new
tables start empty, so adding a mode never costs anyone a score. Every dial is remembered in the config file, written the
moment a game starts, so the next launch picks up where the last fight
left off. Each entry carries a line on what it does, and Escape
steps back out of anything - a detail window, the picker, the settings,
the layout editor, a finished game's screen. How to Play lists every
action against the keys bound to it right now, and explains the forced
drop, in the Python screen's words.

The room is heard as well as seen. The cues are the synthesised WAVs out
of sound/, fired by the cues the sim itself raises - which the trace
harness grades, so what you hear is what the engine decided, frame for
frame - and they are struck metal now, in three tiers: the ticks that
fire several times a second keep their old lengths and stay dry, the
landings get a sub under them, and the clears, the spins and Overdrive
ring out into a hall.

The other half is generated in the mixer, sample by sample, from the same
four numbers the backdrop paints with. A furnace bed roars under
everything, quiet in a menu and open when the board is in trouble. Over
it runs the Forge score: eight bars in D minor whose layers *arrive with
the heat* - a drone and the bellows while you are safe, the anvil once
the Flow gauge has something in it, a bell line over a hot board, a sub
swell and a faster tempo in Overdrive. It is a readout, not a backing
track, and there is nothing on disk for it: no loop seam, and the
intensity is a parameter. The classic chiptune is still one setting away
under *Sound -> Track -> Classic*, and the bed has its own switch.

Because half the audio is now generated rather than loaded, `audio_check`
renders buffers straight out of the mixer and measures them: silence when
silence was asked for, a room whose level rises with the board, nothing
that clips or goes non-finite, a cue pool that steals a voice rather than
dropping a keypress, and byte-identical output from a fixed start. Finished games are saved to data/replays
in the same JSON the Python game writes; either game can browse and watch
the other's recordings, re-enacted stop by stop with the piece walking its
recorded trail, or the finesse-corrected route with *Perfect finesse* on.

The game also runs on Android, phones and tablets both: the same sources
build as the shared library SDL's activity loads (`cpp/android/build.sh` -
no Gradle, just the NDK's CMake toolchain, `javac`, `d8`, `aapt`,
`zipalign` and `apksigner`; the script's header lists what it needs). On a
device the game plays in either orientation - landscape is the desktop
picture fitted to the screen, portrait rebuilds the essential column and
trims the previews to three - with on-screen touch buttons that step aside
whenever a hardware keyboard talks and come back at a touch, and the
Android back button standing in for Escape. A finger dragged over any menu
scrolls it, so screens taller than the phone - the profile's growth charts,
the analysis tabs - stay reachable by touch. Assets unpack out of the APK
into the app's own storage on first launch, so every path in the game
works unchanged. On a desk, `FORCETRIS_MOBILE=WxH` stands a phone-shaped
window up with the same layouts and buttons, which is how they are
screenshot-verified without a phone.

On Windows, install SDL2 through vcpkg (`vcpkg install sdl2`) or point
`SDL2_DIR` at an unpacked SDL2 development package, then run the same CMake
commands. Dear ImGui is vendored in `third_party/imgui`, so there is nothing
else to fetch.

**The art is generated.** Every image the GUI ships - the furnace-hall
backdrop, the nine-slice metal plates, the node and mode icons, the coin
marks - lives in `gfx/` as PNGs that `tools/make_gfx.py` painted (PIL,
drawn at 4x and downscaled, deterministic), so the look is versioned
twice: as the pictures the game reads and as the code that made them.
`gui/gfx.cpp` loads them through a vendored `stb_image` into a texture
cache, with a nine-slice helper for the plates; two bundled OFL faces in
`gfx/fonts` (Marcellus for headings, Cinzel Decorative for the wordmark
and verdicts) carry the identity, the body text staying a system sans.
Every drawing site falls back to the old procedural look when an asset
is missing, so a checkout with no gfx directory still runs - and the
smoke proves it.

Headless machines can still prove the whole thing runs:
`FORCETRIS_SMOKE=1500 SDL_VIDEODRIVER=dummy ./forcetris` plays that many
frames of scripted-random input and exits (`FORCETRIS_SMOKE_STAGE=<n>`
points the run at a Forge Road stage instead, so every recipe's launch,
overrides and settlement can be proven headlessly - the campaign file is
loaded first, the way the Career screen loads it, so the file's Anvil
upgrades ride along; `FORCETRIS_SMOKE_RUN=1` resumes the file's run or
sets out on chapter one's map and drives the whole roguelite loop -
node picked, stops visited, battle fought, verdict settled, spoils
taken, next node - failing if not a single battle settles;
`FORCETRIS_CAMPAIGN` redirects campaign.dat the way the other data
files redirect);
`FORCETRIS_SHOT=/path/out.bmp`
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
