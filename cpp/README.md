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
| `temper` | The game's central gimmick: the thirty-four-card temper pool, what each card does to the rules, the weighted roll that offers three a heat, the heat counter every screen shares, and the bot's rank-tempered pick |
| `campaign` | The Forge Map's machinery: the seeded branching map generator (`forgemap.cpp`), the run state and its difficulty arithmetic, the chapter gate, the star and slag arithmetic, the Anvil's permanent upgrades, and campaign.dat. The content - the chapter table and every stage recipe - lives apart in `stages.cpp`, so growing the road edits one file. The game's design itself (audience, goals, roadmap) is written down in the repository root's `DESIGN.md` |
| `munch` | MinoMuncher's statistics re-derived over this game's own records (formulas from the MIT-licensed minomuncher-core): the nine clear buckets, spin efficiencies, the three-way attack-per-line split, burst and plonk PPS by Gaussian mixture, the four-deep well rule, the surge accounting and the cheesiness sigmoid - scored over raw attack, a trainer having no multiplayer wire |
| `profile` | The history: one tolerant key=value line per finished game, appended forever, read back for the profile screen's aggregates and growth charts |
| `bot` | The versus opponent: a full-reachability search over the real kick tables - so its tucks and spins are exactly the game's - an attack-and-shape evaluation, a rank ladder paced to TETR.IO's own per-rank speeds, and the driver that types the plan into a sim one key at a time |
| `gui/` | The SDL2 + Dear ImGui game: board, hold, queue, the burn rooms' fuse wick, banners, sound, the generated furnace bed and Forge score, menus, settings, the stat layout editor, the replay browser and viewer, the game modes - versus included, with the bot's board beside yours - the high score screens, the how-to-play screen and the analysis of a finished run |

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

Settings — DAS/ARR/DCD/SDF/ARE, the finesse trainer, the feel toggles
(shake, low-latency, smooth motion), the volumes, the key bindings and
the stat layout — sit in one tabbed screen. The rulebook knobs an older
build offered (spins, clear style, kicks, the fuse, the flat forced-drop
timer) are gone: the rules are the game's own, and the fuse is the
Forge's gimmick. Handling applies from the next game; keys, volumes and
layout apply at once.

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
A chain that resolves inside that one lock frame still announces itself:
the banner leads with CASCADE and the board rumbles, so a collapse is
never mistaken for a single clear. Spins follow one rule in both
engines: a move disarms the spin only when the piece actually goes
somewhere, so holding the key into the stack while rotating - the way a
twist is played - leaves the spin armed.
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

Every event on the board has its own shape, which took a rewrite to be
true. For a long time eleven different cues all called the same spark
burst and varied only its colour and count, so a t-spin, a quad and a
cascade were the same puff of dust in three tints - the game looked
uniform because it was. There are four primitives now. A **spark** is the
soft round glow, still the workhorse. A **shard** is angular: a quad
drawn as two triangles so it keeps a hard edge at any angle, spinning
under twice a spark's gravity, which reads as something solid coming
apart. A **ring** expands and thins as it goes, so it is a wave and not
a circle sitting still. A **beam** is a column of the well going bright,
narrowing to a seam as it cools.

They are handed out by what happened. A single line clear stays quiet -
it is the game's heartbeat and a field that erupts on every one of them
is exhausting - while two lines start throwing debris and a quad throws
gold off every cell of all four rows and punches a beam down the well.
A t-spin opens a ring at the piece and throws its fragments along the
circle rather than out of it, so the turn is visible in the debris. A
perfect clear whites out the whole well and sends a wave from its
middle. Garbage rising kicks dust up off the floor, scaled to how many
rows came.

And ice breaks like ice. Cold Iron freezes a row solid and the row has
to be cleared twice, but the sim only announces the freeze - the shatter
arrives as an ordinary clear, which is exactly what it used to look
like. The screen keeps last frame's copy of the frozen-row mask and
watches for a row that stopped being iron: pale shards off all ten
cells, a white crack the full width of the row that thins to nothing
(not the burn's spreading glow - ice is already broken along its whole
length), and a short jolt that snaps rather than rumbles. Nothing in the
graded engine changed to get any of this; the mask, the pending-garbage
count and the locked piece are all public, and the screen reads them.

**What happens between the things that happen.** Screens used to cut, a
game used to begin with a board that was simply there, and a climb used to
begin with a map appearing. Four beats fix that, and all four are timers
that do nothing at all while they are at zero, so anything that forgets to
start one simply looks the way it always did.

The **curtain** lifts on every screen change. Nothing announces one:
forty-odd places in the code assign a screen, and a transition each of
them had to remember would have been a transition half the game did not
have, so the curtain watches instead. What it draws is a soot veil lifting
off a hot seam, the seam running across at whatever height the veil has
reached, so the eye follows one bright line up and out.

The **preheat** brings the well up to temperature behind the count: the
soot over the board thins across the three seconds while the floor's own
glow climbs the well, so the first piece falls into somewhere that was
made ready. The **cooldown** puts it out again over half a second under
the verdict - the board stays drawn underneath the whole time, because a
loss screen over a well that vanished reads as a crash.

**The Anvil used to run out, and a roguelite whose meta finishes in two
runs has a tutorial with a price tag.** Seven upgrades, twelve levels
between them, six hundred and eighty-five slag - two good climbs - and
after that every run rendered its embers down into a number nothing would
ever spend.

Three things changed. The metal goes deeper (five levels of wick, four of
bank, bellows, sense and war chest, two of lifeblood). The cost curve
steepens from linear to triangular - level n costs n(n+1)/2 bases, so the
wick's fifth level is fifteen times its first rather than five, and a deep
buy is a decision against every other deep buy rather than whatever the
list happened to print first. Together the Anvil now holds 4,245 slag
instead of 685.

And it sells **tools**. A tool is bought once and carried into every room
on the road: one charge a room, spent by hand, and only one of the three
rides at a time.

| tool | one charge |
|---|---|
| The Shear | the bottom row of your own well, gone, the stack settling onto it |
| The Cull | four rows struck off what is coming, before it ever rises |
| The Flare | half a gauge thrown on the fire |

They are the first permanent buy in this game the player *does* rather
than *has* - everything else at the Anvil is a number leaning on a rule -
and which one to carry is a decision that outlives the run that bought it.
`E` spends it on a desk; a chip under the board spends it on a phone and
says whether the charge is still in hand.

All three land through GUI-only levers - `Sim::shear_floor`,
`Sim::shed_garbage`, `Sim::stoke_flow` - through the same door
`impose_gravity` and `drain_flow` already came through, so a game that
carries no tool is bit-for-bit the game it always was and equivalence and
trace do not move. The Flare leaves ignition to the ordinary path: a flare
that falls short of the bar lights nothing, and the player has spent a
charge on a warmer fire, which is a decision they made.

**And the Turning Rack is a curse now.** It was a RULE card for four arcs,
offered as though stirring the hold on every clear were a way to play.
Nothing in the game wants the box shuffled, so it was a trap wearing the
colour of the rare ones, and it was taking a seat in a hand that owes the
player three real choices. It does what a curse does, so it is one: laid
by the ring rather than drafted, priced like a curse to burn off. Rule
drops to three, the climb lays six curses instead of five.

**The mechanics were fine and nothing landed.** Every event on the board
already threw particles - sparks, beams, rings, swirls, shards - and the
whole thing still read flat, because three pixels of random shudder for a
fixed count of frames is what a double got and what a perfect clear got.
No direction, no size, no decay, and nothing anywhere in the game ever
stopped. Impact is not particle count; it is weight, and weight is three
things:

- **The kick.** An impulse with a direction and a size, ringing down under
  a falling envelope rather than rattling flat. A blow you send shoves the
  well down and scales with the rows it was worth - a ten-row blow does
  not shove exactly as hard as a two-row one - and a blow you take shoves
  it *up*, the one direction nothing you do yourself ever pushes. The
  strongest kick wins rather than the newest, so a quad landing mid-slam
  is not shrunk to whatever arrived second.
- **Hit-stop.** The whole match holds still for a few frames on a big one:
  four on a quad or a spin, five on a cascade, six on an ignition, nine on
  a perfect clear, three on a garbage slam of four rows or more. The
  oldest trick there is and the one that does the most - the pause is what
  makes a hit feel like it hit. Cosmetic and GUI-only: the sim is simply
  not stepped, exactly as it is not stepped while paused, and both boards
  hold together so a duel never gains a tempo from it.
- **The number.** What the blow was worth, thrown off the well in the size
  it deserves, gold going out and red coming in, on the foreground list in
  the title face with a hard shadow. Every one of these numbers was
  already in a meter down the side of the screen, and not one of them was
  ever looked at during a fight.

The small events stay small on purpose: an ordinary clear gets a three
pixel nudge and no stop, because a field that halts every few seconds is a
stutter rather than a reward.

**And a blow you send now leaves.** The wire between the boards used to
carry one five-pixel square with four ghosts behind it, arcing over twenty
frames and landing in a handful of sparks - the same dot for a two-row
blow and for a twenty-row one, and the board it landed on did not move. A
blow you sent left no impression at either end of it.

A blow is a **volley** now: a slug for every row it was worth, up to ten,
launched a couple of frames apart so a big one visibly pours rather than
blinks. Each carries its own arc height, its own scatter at the target so
ten of them do not stack into one dot, and a trail sampled closely enough
to read as a streak (0.022 apart put eleven pixels between the samples and
the eye counted them). The colour carries the size - ember for a nudge,
gold at four rows, white-gold at eight - so a blow is readable in the air
before anyone has counted what it is about to lay. Red coming back the
other way.

Both ends of the wire do something now. The muzzle sparks at the well it
left, and the well takes a sideways recoil scaled to the rows, opposite in
direction to a blow taken, so sending and taking never feel the same in
the hands. On arrival the slugs throw shards rather than twinkle - a blow
landing should break something - and the volley's last slug leaves a mark:
a cross of light that opens and fades, sized by the blow, so a twenty-row
landing does not look like a two-row one.

And it is aimed. In a raid there are three boards on the table and the
player chose one of them; the volley flies at that board's rectangle
rather than at a fixed corner of the screen, which is the only decision a
raid asks for.

**A playstyle is assembled, not handed over.** Every card before these
made a blow heavier however it was struck, so every build wanted the same
things and a run was a pile rather than a plan. The Style family names a
way of playing, pays it, and charges the other ways for the privilege.

The first draft of it was four cards, one per style, each carrying a whole
style AND its whole price - and that chose the build for the player: take
the card and you are a plonker now, bill included. So each style is split
three ways instead. Two **steps** only pay. One **creed** pays most and is
the only card in the game that charges the other styles. A hand can offer
a lean towards digging without also selling the whole cost of it, and the
commitment happens when the player reaches for the creed.

| style | steps (pay only) | creed (pays most, and charges) |
|---|---|---|
| plonking | rows by the rubble the clear ate; the same again plus a faucet of Flow for digging | the deepest dig, and a clear over a bare floor is worth less |
| striding | the back-to-back chain past its first link; the same again plus Flow on what you land | the chain again, and the blow that breaks it hurts |
| the opening | a loud first half-minute; a second, shorter, louder window | the longest loud window, and every blow after it is lighter |
| downstacking | a plain clear - no spin, no quad - pays like a rare one; the same again plus Flow by the line | the plain clear again, and the quads and spins give it back |

They land on the raw table value, before the multipliers and therefore
before the ceiling: a style changes what the clear was worth, the ceiling
bounds what the cards can multiply it by, and the two do not fight. Nine
SimConfig fields carry them, adds at zero and prices at one, so nothing
that does not draft them notices - equivalence and trace do not move a
step. A second copy of a step deepens the same bet rather than buying a
different one (a second Opening buys a longer opening, not a louder one),
and every price has a floor so no stack of creeds can erase another style
entirely.

The tests hold the split rather than the flavour. fusecheck measures one
quad off an all-I deck under each card: a step must leave that quad
*exactly* where it was, and a creed is where the bill arrives - so a step
that quietly nerfed something would fail even though the copy still read
like a step. tempercheck says the same thing from the other side, over
every card in the family at once. The plonk's dig case took two tries:
handing garbage to `receive_attack` and then calling `seed()` overwrites
the board, so the first version quietly measured nothing.

**A blow has a ceiling now, and it was measured rather than argued.**
Adding the multipliers instead of composing them stopped the runaway; it
did not stop the total. Four cards into chapter two - two heavy hands, the
dice, a glass edge and a coat of hot oil - already reach four and a tenth,
which turns a T-spin single and a T-spin double into twenty-nine rows
against a well that is twenty deep. That is not a fight, it is a click,
and it is what a grandmaster miniboss was dying to. A blow may be doubled
and no more. The cards keep every other half of what they do; what a maxed
hand buys is reaching the ceiling sooner and holding it through more of a
room, not a bigger number on one clear. fusecheck states the ordinary hand
that would have gone past four, so the ceiling is a decision with its own
pin rather than a number someone can quietly raise.

**And the coin is cut again.** The rate began at two per line and three
per attack, which paid a forty-line, sixty-attack room 260 against a shop
whose whole stock cost sixty-five. One and one was meant to put a good room
at about a reroll and a pick; it paid a hundred, and a climb still reached
ring three holding eight hundred embers with the shop already at its
three-times ceiling. Half of that, floored - a strong room pays fifty, an
ordinary one nine - and the four things worth buying cost ten, twenty,
thirty and thirty-five. The choice the coin was for is a choice again.

**What the run is carrying is emblems, not a sentence.** It used to be one
line of prose - "Loaded Dice x2, The Floor Sweep x2, Heavy Hand x2,
Coolant, The Sifter, Frostbrand..." - which by the middle of a climb
wrapped to three lines of names. Names in a row are not a build: nothing
in them says how many of a thing you hold, which family it belongs to, or
which of them is the reason the last fight went the way it did; reading it
meant already knowing every card in the game. Each distinct card is a
plate now, cut to the same octagon as the map's nodes, filled with its
family's colour, marked with two letters and stamped with `x2` when a run
holds more than one. Colour groups them at a glance, and the full name and
what it does are one hover away.

**The board ships bare, and the wick ships long.** Two defaults, both
chosen for someone opening this for the first time rather than for the
person who wrote it.

Nothing is pinned beside the well until the player asks for it. The old
default lit seven live figures - PPS, APM, VS, time, pieces, lines,
finesse - which is a dashboard, and a dashboard is for someone who already
knows which number they are trying to move; anyone else reads it as noise
between them and the piece. Every panel is one tick away in Settings -
Layout, the presets are still there, and there is a `none` preset to get
back to bare. A config file written under the old default is cleared once
on load and stamped, exactly the way the handling bring-forward works, and
only if its layout is still on that shipped set - a player who picked
their own panels keeps them.

And the fuse starts at five seconds a piece rather than three, shaves a
tenth every ten lines rather than a sixth, and floors at 1.2s rather than
0.8s. The old schedule was written for a player who already places a piece
without thinking about it; anyone else met the slam before they had
learned what the board wants, and a room that ends because the clock ran
out teaches nothing except that there was a clock. A run's chosen fire and
each burn room's own `fuse_scale` still tighten it from there - this is the
gentlest the game ever is, and it should be gentle. fusecheck states these
three numbers in one deliberate pin now; the five checks that used to read
them off the defaults while really testing the schedule's arithmetic have
their own fixed ground.

**The screens take the screen.** Every content screen used to be a centred
panel of some hand-picked width - four hundred and ninety here, five
hundred and eighty there - sitting in the middle of the display with the
backdrop showing all round it. On a small window that is fine; on anything
modern it is a postage stamp, and the map in particular had grown a tree
too wide for its own panel and was clipping on the right and scrolling
inside a box while two thirds of the screen sat empty behind it. The map,
the scores, the settings, the analysis, the replay shelf, the help and the
profile all take the display now. The menu, the mode picker and the game
over panel deliberately do not: those are hero panels that auto-fit what
they hold, and a column of five buttons stretched over a display is not a
menu, it is a menu with a lot of nothing beside it.

**Width alone was not the point, which the first pass got wrong.** A form
of sliders or a list of key bindings stretched across nineteen hundred
pixels is a label pinned to the far left and its value pinned to the far
right with a hand's width of nothing between them - harder to read than
the small panel it replaced, not easier. So the window is full-bleed - the
chrome, the header and the ground fill the display - and the content sits
in a column of its own down the middle (`open_column`), with the footer
lined up under that column rather than out at the rim. Wide content skips
the column and uses the whole width, because a score table has somewhere
to put it. On a phone the column is wider than the screen, so it collapses
to the full width on its own and nothing needed a second code path.

The map is the one screen whose content genuinely wants every pixel, and
its plates are sized from the room there actually is: the widest row is
measured and the width shared out between its lanes, so the tree spreads
across a desktop and shrinks to fit four lanes on a phone rather than
either one being a number typed once.

**A chapter has several endings.** The top row of a map used to be a
single node, and every path in the graph funnelled into it - that, more
than anything else, is what made a run feel like a corridor with a bulge
in it. Two or three watches stand up there now, each from a different
concept pair, so which finale a run reaches is decided by the road it
walked rather than by the seed alone. The middle widened with it: two or
three doors at the entrance, three or four lanes through the body, where
before it was two or three, and a row that offers one choice is not a
choice. campaigncheck holds the new shape, that the middle is never
narrower than the doors, that every ending is a genuinely different
watch, and that the miniboss belongs to one of them - the risky branch
leads toward a finale rather than being a fourth thing on its own.

**The map is a casting floor.** Every node is an octagon - a square
chamfered on the anvil - drawn as a mould cut into the floor, with a
keyline round it, a lit bevel along its top and upper chamfers and a dark
one down its shadow side. Rounded rectangles were the shape of a button,
which is to say the shape of a form to fill in; a row of identical
octagons at identical angles reads as machined instead of arranged.

The edges are **channels cut in that floor**, and this is where the metal
lives. A groove is routed square - up out of the plate below, across at
the halfway line, up into the plate above - with both turns chamfered at
forty-five degrees, so no angle anywhere on the screen is a right one and
the edges are made of the same geometry as the nodes. Each is drawn as a
raised lip either side of a dark cut, and what is in the cut is the whole
state of the run: a road already walked holds metal that has **set**, gold
and still glowing; a door open right now has a **head of molten metal
travelling up the groove** toward the mould it will fill; everything else
is an empty channel waiting to be poured. The first row has nothing above
it, so it is fed by short stubs running straight up out of the furnace -
without them a map that has just been struck shows no metal moving
anywhere, which is the moment it most needs to.

The flow is a moving window onto the groove's own points (`route_slice`),
not a second path kept in step with the first, so it follows every corner
exactly. All of it goes on the lower half of a split draw list, under the
plates, where a groove belongs; the edges used to be drawn last, straight,
and over the top of the nodes, which is the one arrangement that makes a
tree look like a diagram lying on a panel.

The first version of this poured lava across the *background* instead -
a pool at the foot and veins wandering up between the lanes - and it was
wrong for a reason worth writing down: the lava was decoration behind a
diagram rather than the thing the diagram is made of. What is left of it
is the floor the channels are cut into: scale and sand, level cast lines
far apart, and the furnace banked along the very bottom, hot while the
maul's shock is climbing and an ember glow the rest of the time.

And a run opens with **the maul**. The blow comes down out of the top
right onto an **anvil** at the foot of the tree - the bottom row, which is
where a run is actually started - and the shock runs up the map lighting
the road as it goes. A hammer swinging at nothing was the first version
and it read as exactly that; and the maul no longer vanishes on the frame
it strikes, but rests on the face for a few frames and lifts back out the
way it came.

**A blow's multipliers add; they do not compose.** This was a real bug and
it took a screenshot from a ring-three climb to find it. Three separate
bonuses each multiplied the *result of the last* - Overdrive scaled the
attack, a heavy hand scaled that, and a crit doubled that again - so a
plain double, worth one on the table, could leave the board as twelve, and
picking a third bonus was worth more than the first two put together. They
are gathered into one factor now and applied once, so a bonus is worth
what its face says whatever else is already on. With exactly one of them
live the arithmetic is unchanged (1 + (m - 1) is m), which is why every
existing pin held; it only bites when they stack. The same build's worst
case went from fifty-six to thirty-two.

**And the climb's foe has a hide.** Every dial the endless ramp turned
made the foe hit harder - a rung of rank, a scale on what it sends, a
curse laid on your board - and not one of them made it harder to kill, so
a build that had been collecting a card a node put the ring-twelve foe
down in the same two blows as the ring-one foe. `endless_guard` is the
Cold Shoulder's dial on the other side of the room, turned by the ring
instead of bought: the foe takes six per cent less per ring, down to a
floor of 0.35. Its offence stays unbounded - only the hide is floored.

Both of those floors were raised once, after the climb was measured
rather than argued about. The first tuning - four per cent a ring floored
at half, a hide floored at 0.45 - left a ring-ten blow landing at
sixty-four per cent and the hide bottomed out at ring nine, so every ring
past that turned nothing at all and the climb ended only because the foe's
rank had run out of rungs. A climb with nothing left to tighten has ended
whatever the row counter says, so the tax bites harder and both floors sit
further down: ring ten now lands at forty-six per cent against a hide of
0.40, and neither floor is touched until ring twelve.

**The climb's other two dials lean on the build instead of the room.**
The hide made the foe survive longer; it did not answer why the build was
so large in the first place. A card a node, forever, means that by about
the fourth ring the purse has bought everything worth buying - the draft
has stopped being a decision and become a formality, and the build has
stopped being a shape and become the whole pool.

So the spoils thin as the climb deepens (`spoils_every`): every node for
the first two rings, then every second, every third, converging on one
hand a ring. The gatekeeper pays regardless, so the thinning has a floor
and is never a chance of nothing; the rhythm counts the climb's whole
height rather than restarting at each ring, so the boundary is not a free
extra hand. In practice a ring pays six hands, then three, then two, and
from ring ten exactly one.

And the hand itself is taxed (`endless_toll`): what the player sends is
scaled down six per cent a ring, starting one ring late so the early climb
is untouched, and stopping at three tenths. Past that floor the climb has to
beat you with what it sends rather than with what it takes away - a fight
that cannot be won at any speed is not a fight. Both tolls are printed in
the map header beside the ring, because the player did not agree to either
one and should not have to infer them from a fight going badly.

**The map has two rooms that never fight, and neither had ever been
drawn by a test.** The run smoke's player does not press keys, so it loses
every battle, so a run it drives never leaves the entrance row - and the
forge and the event room sit on the middle rows. Two of the four kinds of
node in the mode, and the matrix had walked past both of them for their
whole life. `FORCETRIS_SMOKE_STOPS` walks a run onto one, first putting a
full hand of cards and every curse the climb can lay into the run's
hands, and then presses what the room offers: the free draft, a melt, a
duplicate. `gui_smoke_stops` is the thirtieth ctest. Found while chasing a
crash report about the forge that has not reproduced here yet - the hole
in the coverage is real either way.

**Two phone bugs the forge made, and both are shapes rather than
accidents.** A player reported that tapping the forge on a phone slammed
the room shut, and that a room reached later took no input at all.

The first is one tap walking through three widgets. On touch a press
lands on one frame and the release on the next, so the tap that opens a
window is still in flight when the window draws - and a map node sits
exactly where the room's own buttons land. Node, then the forge's free
hand, then the first card of the spoils it deals: three presses, one
finger. `kUiGuard` makes a freshly opened overlay deaf for ten frames.
Phone only: on a desk a click is a press and a release on the same widget,
and greying every draft for a fifth of a second would be a tax paid for a
bug that lives on touch.

And the forge itself is a grid now rather than two lists. It used to
print every card as a row of name-and-button to melt, then every stackable
card again as a row of name-and-button to duplicate: twenty tempers made
forty rows, which is a wall of text to scroll through looking for one name
even once it fits on the screen. The plates are the same emblems the map
header already reads, so a build looks the same everywhere it is shown;
tapping one opens the card - its name in the family's ink, its one line,
and its own two deeds, with the duplicate absent on a curse or on a card
already struck as deep as it goes. Twenty-one tempers is three rows.

The second is arithmetic. The centred overlays are `AlwaysAutoResize`,
which on a desk is right and on 1080x2280 is not: a run holding twenty
tempers gives the forge a melt list forty rows long, so Leave sat below
the bottom of the screen and the run was over. `bound_window` caps every
overlay at 94% by 90% of the screen and lets ImGui put a scrollbar on it,
and Leave is now at the top of the room as well as the bottom - a way out
that can only be reached by scrolling past forty rows is not a way out.

**And for two arcs the blow did not happen at all**, which is worth
writing down because of how it hid. `begin_run` arms the strike and clears
`map_seen`, and `draw_forge_strike` was supposed to wait for the map to
have been drawn once. It did not wait - it spent a frame and then threw
the whole strike away whenever `map_seen` was false.

That is every real run. "Set out" is a button *inside* `draw_career`, so
`begin_run` lands in the middle of a frame, after the chapter picker has
drawn and after the last chance that frame had to draw a map. The strike
was armed and killed in the same frame, before a pixel of it existed.

The headless smoke never caught it because its run driver calls
`begin_run` after the present, so the next frame draws the map first and
the strike survives - the one ordering that works, and the only one that
was ever exercised. Every screenshot of the maul in this file was taken
down that path. The fix is to hold rather than cancel: not on the map yet
means wait, and only leaving the Career screen cancels. Holding costs
nothing - the maul is not spent, not drawn, and the nodes stay unclickable
for the single frame it takes the map to appear.

The blow shakes the whole screen, which took a second mechanism to do at
all. The board's own quake runs only on the game screens and moves only
the board pane, so a blow landed on the map shook nothing whatsoever. The
jolt shifts the viewport for the entire ImGui pass instead, and it is
deliberately the hardest one in the game, because it is the only one that
opens a run.

The map scrolls to that foot while the blow is in the air and no
node is pickable until it lands: a flourish you can click through before
it arrives is neither a flourish nor a click, and it is under two seconds,
which is less than it takes to read the bottom row anyway. The maul is a
generated asset like everything else, and it took three passes - a wide
thin head read as a signpost, the same head moved to the foot of the
sprite read as a pedestal, and what finally says *hammer* is the asymmetry
a real one has: a flat pale striking face at one end, a tapered peen at
the other.

**Both of these are pixel art**, and they are the only two sprites in the
set that are. Everything else is drawn at four times size and brought down
with a LANCZOS filter, which is right for a 24px icon and wrong for the
two things that are drawn large and on their own: at that size a smooth
vector silhouette reads as clip art. So the maul and the anvil take a
different last step - box-average down to a 32-square grid, throw away
partial coverage so the outline is a staircase and not a fade, snap every
surviving pixel to a flat ten-step ramp, and blow it back up
nearest-neighbour. The alpha cut is what makes it pixel art rather than a
small blurry picture: a cell the shape only half covers is either in or
out, so the edge lands on the grid instead of feathering across it.

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
the Feel tab's Low-latency toggle brings vsync back for tearing-averse
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

**A release is never refused.** The game gates presses in a lot of
places - the screen is not the board, the game is paused, the layout
editor is up, the countdown is running, ImGui wants the keyboard - and
for a long time that same gate turned away the releases too. Every one of
those conditions can turn true in the gap between a key going down and
coming back up, and when it did the release was swallowed while the press
was not: the engine went on holding a key the player had let go of, and
the next press of it did nothing at all, because it was already down.
That is the whole of "it eats my inputs", and it was not a handling
setting or a frame rate - no DAS or ARR number can fix a key the engine
thinks is still held. Releases now pass the gate unconditionally, since
letting go of a key can only ever un-stick something, and losing the
window - focus, minimize, the phone's task switcher - lifts every key the
board could be holding, which is the same bug wearing an alt-tab.

**The fuse punishes; the Flow rail rewards.** They were one flag once,
and turning the fuse into a room's gimmick took the reward with it by
accident. They are two flags now (`fuse`, `flow_rail`): the fuse is the
clock that slams a piece down where it stands and belongs to the burn
rooms alone, while the gauge - filled by spins, quads, back-to-backs,
combos and perfect clears, never by haste - climbs in every game there
is, and a full gauge still ignites Overdrive: score and attack
multiplied, and every clear burning a garbage row off your own floor
while it lasts. What genuinely needs a clock stays with the fuse: the
Flash bonus for a quick lock, the Flow bled by a forced drop, and the
heat pressure a duel's Overdrive puts on the other wick.

**The draft is the campaign's gimmick now.** The Forge Map deals its
cards between battles - the spoils of a won node - and the board never
freezes mid-game for a hand of cards; the pick is one press (`1` `2`
`3`, the arrows and Enter, or a tap). Inside a burning room the heats -
ten cleared lines each - still tighten the fuse; that is the sim's own.

The pool is thirty-four cards in six families, and a card's face carries no
numbers - a family word and glyph, a name, and one plain line, because a
card that needs a manual has already failed. **Fuel** survives: *Thick
Wick* (pieces burn longer, `fuse_base +0.5s`), *Quench* (clears refill
more fuse, `+0.3s` a line), *Slow Burn* (the forge tightens slower,
`fuse_decay -0.05s`). **Flow** presses: *Bellows* (`overdrive_secs +3`),
*White Heat* (`overdrive_mult +0.5`), *Spark* (`+2` Flow per line and per
attack). **Risk** trades: *Overheat* (all Flow gains doubled, the wick
half a second shorter), *Gamble* (`overdrive_mult +1.0`, a burnt piece
costs `15` more Flow). **Rule** rewrites: *Collapse* (clears become a
sticky cascade), *Every Twist* (every spin scores, minis included).
**Chaos** is not a family of cards any more. It was for two versions, and
in that time nobody picked one - which in hindsight was the only possible
outcome. A card that breaks something the hands trusted has to give
enough back to be worth taking, and giving that much made every one of
them a wash: the gift cancelled the price and the card meant nothing.
Worse, one of them was not a wash at all. *The Crooked Judge* read "any
piece boxed in on both flanks scores a full spin", and on a real stack
that is most placements - a back-to-back chain that never breaks, and by
a distance the strongest effect in the game. Nobody was ever going to
turn that down.

So the gift is gone and so is the choice. The five are **curses** now,
and the Endless Climb lays one every second ring without asking: *The
Crooked Judge* (the judge has stopped listening - no spin scores at all,
whatever the rules say about it), *The Ring* (the two walls open onto
each other, so a held direction crosses and keeps going, and gravity
tightens six frames), *Crossed Wires* (left and right and the two
rotations trade places), *The Loose Ratchet* (every third turn goes one
further), *Sticky Tongs* (every fourth hold sticks). The three that curse
the keys do it in the GUI's input path, never in the sim: the graded
engine is handed honest keys and the recording is the piece's real
journey. A curse lands in the same list the drafted cards do, so
everything that already reads a build reads it too - and a forge will
burn one off, at three times what it charges to melt a card, because the
choice between shedding the ring's work and building on is better than
either alone. The drafted pool is twenty-nine cards in five families:
Fuel and Flow stack two or three deep, the one-of-a-kind rewrites are one
each, and when the pool runs out the forge simply stops dealing.

**Ward** guards, and it is the only cold colour on the table - slate,
iron that has been let alone to cool. Nothing in it wins a fight faster
and everything in it makes one survivable: *The Counterweight* (the forge
lets go slower, `fall_delay +6`), *The Free Hand* (the hold box never
locks - and it cannot stall the fuse, which rides through every swap, nor
wash a finesse count, which only the first swap of a piece clears), *The
Floor Sweep* (every eighth clear made with rubble still down takes the
bottom garbage row with it - a counter, not a die, and a clean board
never banks one), *The Cold Shoulder* (`garbage_scale 0.75`: what lands
on you in a duel lands thinner, and a smaller queue also cancels less of
what you send - two defences for one slot, with a floor of one row so a
blow that landed never weighs nothing), *Coolant* (`fuse_pressure
-0.25`: another forge's Overdrive leans on your wick less), *The Sifter*
(`cheese_messiness -60`, floored at 20: the rubble's holes line up into a
well instead of scattering). Half of them are conditional by nature -
a guard drawn where there is nothing to guard is a wasted pick - which
is why Ward is weighted with Risk rather than with Fuel. Deliberately
there are no counter-cards: a ward makes a room's gimmick milder, and
never switches it off.

The rest of what V2.4 added spreads over the older families: *The Deep
Bank* and *Hard Quench* feed the wick, *The Draught* lowers the bar
Overdrive lights at (`flow_ignite`, floored at 60 - the rail draws a
hairline tick where the new bar sits, so a gauge that fires early does
not read as a fault), *The Glass Edge*, *The Hair Trigger* and *The
Hollow Wick* trade in the Risk manner, *The Linked Chain* settles clears
whole where Collapse crumbles them, and *Sticky Tongs* joins the curses:
every fourth hold press sticks in the tongs, and the Flow gauge pays for
the fumble. Like the other two curses it lives in the GUI's input path -
and only the press is ever swallowed, never the release, because hold is
edge-triggered and a swallowed release is the one way a curse could leave
a key stuck.

(The old **Tempering** mode - twelve heats, a draft at each, played as
its own Training Yard card - retired in V2.2a: the campaign absorbed the
draft, and the map run is the run now. Its score table stays on the
scores screen, read-only history. The daily is a fixed-seed Ignition
run, the same pieces for everyone who shares the date.)

A **Duel never stops**. There are no drafts in versus, either side - the
freeze that lets a hand read cards has no business in a real-time fight.
The bot arrives *armed* instead: every rank carries a fixed blade
(`temper::blade_for`, D's single thick wick escalating to X's
overheat-and-gamble build, never Collapse - its planner searches naive
clears), applied to its rules at round start and shown under its board,
and written into its embedded recording the way a drafted build would
have been.

The spoils screen is also where the run's coin is spent. Clears earn
**embers** - `embers_of`: one a line, one per point of attack, and
nothing for haste - and the purse buys a **reroll** of the three cards
(8) or a **second pick** off the same table (22). The balance is derived
from the sim's own totals minus what was spent, so it cannot drift, and
it dies with the run - the campaign's own economy.

The rates used to be two and three, and they were wrong by a wide margin.
A forty-line room that sent sixty banked two hundred and sixty against a
till whose entire stock costs well under a hundred: you bought everything
on the table every time and still had change, so no purchase was ever a
decision. Halving the take and raising the prices is the whole fix - a
good room now pays for about one choice, which is what the coin was for.

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
clears, a floor that rises, a stage won by points instead of rows, a
forge narrowed by sealed columns, a cold room where every completed line
freezes solid and shatters one lock later, a stage that starts with
Overheat already in your blood - and the boss duel carries its own blade
(the Forgemaster fights two falls behind bellows, white heat, overheat
and gamble). Every duel on the road is a rung of the bot ladder and the
rungs only climb: a chapter runs skirmish, miniboss, boss one rung apart
(C-B-A, then B-A-S, then A-S-SS) and opens a rung under the boss just
beaten, so an S never arrives before an A has been fought twice - a
raid, being three foes in the room at once, stands well under its own
chapter. `campaign_check` pins that climb, and the fire picked at
the door shifts the whole ladder a rung either way.

Which face stands at the top is the seed's to choose. Every chapter
fields **three concept pairs** - a miniboss and a boss who belong
together - and a run climbs to exactly one of them: the Wardens carry the
rank's own blade and a skill or two, the Hammers trade every trick away
for a blade one grade heavier, and the Tricksters give up that metal for
a fuller kit (two skills on the miniboss, three on the boss). The three
stand on the same rungs, so the roll never changes how hard a chapter is,
only the shape of the fight. The map keeps their names back until the run
is a row away: the icon says what waits - a boss, a miniboss - from the
first look, but the plate reads `? ? ?` until you stand below it. None of
this is read off the table's shape any more. A recipe declares its
`role` (room, miniboss, boss) and its `pair`, and `chapter_rooms` /
`chapter_pairs` / `pair_boss` / `chapter_bosses` answer the questions the
generator and the screens used to open-code as "the chapter's last
recipe" - which is why twelve new duels could be added without moving one
existing seed's map: the battle window is the set of rooms, and the pair
is rolled from a stream of its own. The chapter gate and the Endless
Climb's key ask for *any* boss of the chapter below, since a run only
ever meets one of them.

One row under the boss a **miniboss** duel bars the risky
branch, and the battle pool itself now fields **skirmishes** (a lesser
foe on an ordinary node), a **raid** (three hounds in the room at once,
all of them sending), and **watches** (outlast the rising floor;
the stars are the lines you cleared while holding on). A duel is fought
face to face now - both boards full size, side by side.

A raid is not a queue. All three foes play at once, the way a
multiplayer lobby does: every one of them sends at you, and you send at
one of them. Which one is the whole fight - the board closest to topping
out, or the one hitting hardest - so the aim is yours to move, with Tab
on a keyboard and by touching the board you want buried on a phone. The
aimed foe wears an ember frame and the slot over your own well names it.
A foe that tops out does not end anything; its board cools where it
stands and comes off the wire, and the room is beaten when the last of
them falls. They used to be fought one per round, which is a gauntlet
and not what three boards on a screen looks like anywhere else. Because
three streams of garbage arrive on one board, the roster sits well under
the rung a lone foe of that row would carry, and campaign_check pins
that every foe in a room is weaker than one met alone.

The room is drawn as a focus, not as three thumbnails. It used to be
fitted into the width a single foe had, which put the boards at four
pixels a cell with half that side of the screen empty underneath - it was
unreadable and it looked like a mistake, because it was one. The room
takes the whole band to the right of the player now, measured off the
real window so a phone cannot overflow, and it does not split that band
evenly: the aimed board is the big one and the other two sit back, with
the sizes travelling when the aim moves so a switch is felt rather than
merely noted. A downed board shrinks further - it is furniture now - and
all of them stand on one floor, so different heights read as a room
instead of a misaligned row. Each board carries its own Flow rail and its
own incoming-garbage meter, because choosing which of three to bury is
made out of exactly those two readings, and the scoreboard moved from
beside the room to under it.

A campaign boss fights with its own kit: telegraphed skills, two seconds of warning
before rust falls on it, a column seals shut, the iron cold-snaps, or a
heat wave leans on your gauge.

How hard that kit lands is the fire's business, not the recipe's. A
recipe writes one number for a blow and `campaign::skill_scale` decides
what the number is worth: the gentlest fire takes most of the sting out,
the forged fire is the recipe as written, white heat lands nearly half
again as hard, and a climb goes on raising it ring by ring with no
ceiling, the way everything about a climb does. It scales the rows a
skill throws, the share of the gauge it takes and the seconds it holds a
gimmick down - so a rustfall is two rows on mild, three on forged and
four at white heat. There are floors under all of it: at least one row
and at least one second, because a blow the player watched arrive for two
seconds and then felt nothing from is worse than no blow at all.

A cast is drawn as one thing happening in three places. The foe's well
gathers it: the rim heats through the skill's own colour, motes draw in
from the edges to a core that whitens, and the board itself starts to
shudder in the last second. A plate names the caster and the blow, hung
over that well in landscape and tethered to it on a phone, where a plate
wide enough to read would otherwise cover your own stack. Then, for the
last twenty frames of the wind-up, the blow crosses: three bolts leave
the foe's board and arrive on the exact frame the rules land the effect,
so what hits you is the thing you watched coming. The foe recoils, your
well takes a ring, the screen goes the skill's colour hard and is clear
again three frames later, and metal comes off the impact.

That last part was a second attempt. The first shipped only the plate,
and a plate is static: it announced a skill without anything moving, and
without the caster doing anything at all. A boss that stacks placidly
through its own spell is not a boss casting - it is a timer on your side
of the screen. And the fuse is a stage gimmick now, not the campaign's default:
most rooms play the board pure - the forced drop was the beginners' wall
- and only the Backdraft rooms and the Overheated Wing still burn, the
way the Dark Gallery is the room that goes dark. Duels were the last
carve-out and are one no longer: a clock that is always there is not
tension, it is the rule the beginner already lost to, and it made the
fight the whole road builds towards feel like the trainer. A duel's
tension is the foe - its attack, its blade, and the skills it telegraphs
at you. Above mild, a
mid-fight restart is a surrender and costs what a death costs.

Every stage says what it wants in a plain sentence, read off the recipe
rather than written by hand: "Clear 15 lines.", "Dig through 10 rows of
rubble.", "Survive 75 seconds. Clearing lines is optional." The blurb under
it only says what makes that room itself. They used to be one sentence -
"Old iron on the floor. Fifteen lines through it." - which asks a new
player to work out which half is the rule, and lets a number typed into
prose drift from the number the stage enforces. The computed line cannot
drift, and campaigncheck pins that the number it prints is the number the
recipe holds.

A climb that ends - taken, broken or put down - is graded on its own
facts: rows reached, deaths paid, and the seconds actually spent in
battle rather than wall clock, since a run left open overnight is not a
worse run. Progress carries the most weight, blood the next, pace the
least (this is a casual-first game and a player who thinks about their
stack is not playing it wrong), and the fire wagered at the door scales
the lot. The letter is printed with the three numbers it came from, so
it is never a mystery which one to go after. The per-game TETR.IO
estimate stays where it belongs - the Training Yard - rather than
appearing after every stage of a climb that has not finished.

The road runs three chapters now. **The White Heart** is where the
lessons combine: every one of its rooms stacks two gimmicks the first
two chapters taught one at a time - darkness over sealed columns, a
score run in the dark, cold iron under fog, cold iron over rubble, a
fast fuse burning hot all stage - and it ends at **the Forge Heart**,
the only foe on the road that fights with three telegraphed skills at
once. Past it waits **the Endless Climb**, open once any of the
Deep Forge's masters has fallen: the same six-row map stacked as *rings*
without end, every battle drawn from all three chapters' rooms with the
window sliding up as the rings stack, and the top of every ring held by
the gatekeeper rotation - the road's watch a rung at a time (chapter
one's miniboss, chapter one's boss, and up), then the White Heart's own
two trading watches forever, with each ring rolling which concept pair
supplies the face.

Each ring tightens the screws, and the point is that it never stops. The
ordinary dials - gravity, the quotas, the flood's period - all bottom out
eventually, and for a while that was the whole story: the foe's rank
climbed half a rung a ring into a ceiling it hit around ring eight, and
after that nothing about a climb got harder while the player went on
collecting a card a node forever. One bag of the right shape ended every
room past that point. Three things now climb without a ceiling. The
rank ceiling moved to the top of the actual ladder, and past THAT the
promotion the climb cannot pay in rungs is paid in steel instead - the
foe's attack scales by the rungs it was owed and never got. The flood on
the player's own board gets heavier every ring from the first one. And
every second ring lays a curse. The climb is always played at white heat
- one death ends it - so the record is simply the rows climbed, kept as a
single best on the chapter-select card.

The roguelite is that the build outlives the battle: a won node banks its
embers into the run and deals **the spoils** on the map - three cards,
take one or take nothing, reroll or a second pick paid from the run's
purse - and every temper picked rides into every later battle of the
climb, forged into the player's rules before the first piece falls. The
pool is thirty-four cards now, and its newer families play with the fight
itself: a heavy hand that scales your attack, loaded dice that land
every third strike double (a counter, not a die - the sim never rolls),
a cold forge that freezes your own iron in exchange for a far harder
hand, a turning rack that trades the hold for the queue's front on every
clear, and two brands that land on the foe - a frostbrand that freezes
its clears, hobnails that open every duel round with rust already headed
for its floor. The Anvil grew too: Forged Lifeblood buys a forged run
one more life, and the War Chest sets a run out with embers in the
purse. And the coin itself has more to do than reroll a hand: the forge
strikes a second copy of a card you carry (where its stack leaves room)
and sells a life back on forged fire, once a visit; the map sells two
oils good for the next battle entered - Hot Oil for a heavier hand,
Frost Oil to freeze a duel foe's clears - painted on the map, spent as
the doors close; and walking past the spoils untaken pays a small
solace, so skipping is a choice instead of a refusal. A
stage never drafts mid-game any more; on the Forge Map the board never
stops. Not every node fights, either: each map scatters **one forge**
(a free hand, and the melting pot - fourteen embers unmakes a temper you
regret, forty-five burns off a curse the climb laid) and **one or two events** (a single card of choice, seeded like
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
outright - and heavier fires pay 150 / 200 percent slag. The fire also
picks the fight, not only the stakes: every duel on the road - miniboss,
boss, skirmish and each foe of a raid - moves a rung of the bot ladder
with it, a rung down on mild and a rung up at white heat, so the
recipe's own rank is what forged fights (`campaign::rank_for`, clamped
to the ladder's ends and pinned in `campaign_check`). Death always
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
three rounds, against a bot picked by rank - F through X, each paced to
that rank's real league speed, blundering as often as its rank would, and
gated to its rank's technique: the low ranks hard-drop, tucks
arrive around B, quad-well building around A, spins and T-slot keeping
around S with one piece of lookahead - and from SS up the planning
changes kind: a deterministic beam search, full reachability at every
ply of the real preview queue with the hold weighed at every step, so a
spin set up now and hit two pieces later is seen and chosen, which no
hard-drop lookahead can do. F and E sit below the league on purpose: D is
a real TL average, and a real TL average already out-paces someone who met
the game this week, which left the gentlest fire with no foe a beginner
could beat.

The letters stay on the picker, where they are the compact handle a row of
buttons needs, and nowhere else. A fight names its foe in words - "The Keen
Underwarden", not "The Underwarden (B)" - because a league letter asks the
player to know what B means before it means anything, and outside TETR.IO
nobody does. A word carries the same ordering without a lookup, and the
picker prints the one it is offering under the buttons with the pace that
goes with it. The bot is our own, written referencing the
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
Play opens a mode picker of five fires, every one on pure rules.
Ignition (endless), Blaze (three minutes) and Inferno (the rising
floor) start at a click, while Meltdown / Bunker and Duel each open
their own window: the cheese one holds Meltdown's race at three
lengths, Bunker's survival at three paces, and the holes-per-row and
messiness dials; the duel one the bot's rank row, the first-to count
and the Fight button. Every Training Yard game scores into its mode's
own table (fusescore.dat, one per mode name, the retired Tempering's
kept as history); the trainer's three-table SFH file keeps its bytes
and its meaning as read-only history, and the scores screen shows all
ten side by side. Game history and replays carry the matching keys -
ignition, blaze, inferno, meltdown, bunker, duel, temper against the
frozen legacy names - so no record ever changes game under your feet. A
variant file written before a table was added is read as far as it goes
and the new tables start empty, so adding a mode never costs anyone a
score. Every dial is remembered in the config file, written the moment
a game starts, so the next launch picks up where the last fight left
off. Each entry carries a line on what it does, and Escape steps back
out of anything - a detail window, the picker, the settings, the layout
editor, a finished game's screen. How to Play lists every action
against the keys bound to it right now, and explains the Forge's fuse.

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

The palette lives in exactly two places: `gui/palette.hpp` for everything
drawn live, and the constants at the head of `tools/make_gfx.py` for
everything baked into a PNG. They hold the same numbers and are meant to
be edited together. Colours used to be spelled inline at the draw site
instead, and by the time anyone counted there were eight different
oranges all meaning "ember", two of them sitting next to each other on
every screen.

The blocks themselves are drawn rather than blitted: a dark seat in the
cell's own hue so a wall of one colour still reads as many blocks, a
four-band face lit at the crown and cooling to the foot, a mitred
chamfer whose lips stop a thickness short of each corner the way a real
one does, and a single specular where the forge light would land. Rubble
is the same block in dead iron with two cracks still glowing through it,
so garbage reads as the same material, only spoiled.

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
`FORCETRIS_SMOKE_ENDLESS=1` drives the same loop up the Endless Climb
at white heat, setting out again whenever a death ends it;
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
