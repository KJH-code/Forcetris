"""Regression checks for finesse counting and the retry rule.

Two things are being checked and they fail in different ways. The table is
arithmetic - a wrong minimum is wrong for every player forever, so it is checked
against the published figures rather than against itself. The counting is
behaviour, and the failure that matters there is a false fault: a trainer that
hands the piece back when the player did nothing wrong is worse than one that
does not count at all. So the checks lean on placements that must NOT be
faulted - tucks, spins, forced drops - as hard as on the ones that must.

Run with: python tools/test_finesse.py
"""
import os
import sys
import tempfile
from argparse import Namespace

os.environ['SDL_VIDEODRIVER'] = 'dummy'
os.environ['SDL_AUDIODRIVER'] = 'dummy'
# The saved profile is read the moment engine.environment is imported, so this has
# to be pointed somewhere disposable before that happens - otherwise a test run
# reads, and then overwrites, the player's own settings.
os.environ['FORCETRIS_CONFIG'] = os.path.join(tempfile.mkdtemp(), 'settings.json')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(tempfile.mkdtemp())

import pygame as pg
import engine.game as G
import engine.finesse as fin
import engine.userstate as us
import engine.environment as env
from engine.shapes import Shape, Block

pg.key.get_focused = lambda: True

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


# A delay long enough that the forced drop never interrupts a placement.
tetris = G.init(Namespace(debug=False, forced_delay=600., volume=0., sfx_volume=0.))
user = tetris.user
core = tetris.game
settings = tetris.settings_menu

NAMES = 'IOTSZJL'
I, O, T, S, Z, J, L = range(7)

# --- The table. -------------------------------------------------------------
# How many distinct placements each piece has on a ten wide board. A piece
# spanning n columns has 11-n of them per distinct orientation, and the pieces
# have 4, 1, 4, 2, 2, 4 and 4 distinct orientations respectively.
PLACEMENTS = {I: 17, O: 9, T: 34, S: 17, Z: 17, J: 34, L: 34}
# The published tables: nothing needs more than two presses to move and one to
# turn, and a piece that needs no turning needs no more than two.
LONGEST = {I: 2, O: 2, T: 3, S: 3, Z: 3, J: 3, L: 3}

def walk(form, presses):
    """Follow a route from spawn, one press at a time, and say where it ends up."""
    state, x = fin.SPAWN_STATE, fin.SPAWN_X
    for name in presses:
        if name in ('cw', 'ccw', 'flip'):
            state = (state + {'cw': 1, 'ccw': 3, 'flip': 2}[name]) % 4
        elif name in ('left', 'right'):
            x += -1 if name == 'left' else 1
        else:
            step = -1 if name == 'das_left' else 1
            while fin.fits(form, state, x + step):
                x += step
        assert fin.fits(form, state, x), 'route left the board'
    return fin.placement(form, state, x)


for form in range(7):
    table = fin.table(form)
    check(
        '{} has every placement it should'.format(NAMES[form]),
        len(table) == PLACEMENTS[form],
        '{} against {}'.format(len(table), PLACEMENTS[form])
    )
    worst = max(len(r) for r in table.values())
    check(
        '{} never needs more than {} presses'.format(NAMES[form], LONGEST[form]),
        worst == LONGEST[form], 'worst is {}'.format(worst)
    )
    # A route is advice the replay hands the player, so it has to actually work:
    # walked press by press from spawn, it must arrive at the placement it is
    # filed under. A route that is merely the right length would still be wrong.
    astray = [
        fin.describe(r) for key, r in table.items()
        if walk(form, r) != key
    ]
    check(
        '{} routes arrive where they are filed'.format(NAMES[form]),
        not astray, str(astray[:3])
    )

check(
    'the search runs over the board that is actually played on',
    fin.WIDTH == len(core.grid[0]), '{} against {}'.format(fin.WIDTH, len(core.grid[0]))
)
check(
    'the search starts where pieces really spawn',
    (fin.SPAWN_X, fin.SPAWN_STATE) == (Shape().pos[0], 0), str(fin.SPAWN_X)
)

# Leaving the piece where it spawned costs nothing at all.
check(
    'the spawn placement is free',
    all(fin.optimal(form, 0, fin.SPAWN_X) == 0 for form in range(7)),
    str([fin.optimal(form, 0, fin.SPAWN_X) for form in range(7)])
)
# The case the whole measure exists for: the wall is one press away however far
# it is, because auto-shift covers the distance off that one press.
check(
    'the far wall is one press away, not four',
    fin.optimal(T, 0, 1) == 1 and fin.optimal(T, 0, 8) == 1,
    'left {} right {}'.format(fin.optimal(T, 0, 1), fin.optimal(T, 0, 8))
)
# Turning the piece as well as walking it to the wall is two.
check(
    'turning at the wall costs the turn as well',
    fin.optimal(L, 1, 0) == 2, str(fin.optimal(L, 1, 0))
)
# Symmetry: the two ways of standing an I on end are one placement, not two, and
# the O has one orientation however many times it is turned.
check(
    'the two vertical I orientations are one placement',
    fin.optimal(I, 1, 4) == fin.optimal(I, 3, 5) == 1,
    '{} / {}'.format(fin.optimal(I, 1, 4), fin.optimal(I, 3, 5))
)
check(
    'turning an O is never worth a press',
    all(fin.optimal(O, state, 4) == 0 for state in range(4)),
    str([fin.optimal(O, state, 4) for state in range(4)])
)
check(
    'an S turned once and turned three times are one placement',
    fin.optimal(S, 1, 4) == fin.optimal(S, 3, 5),
    '{} / {}'.format(fin.optimal(S, 1, 4), fin.optimal(S, 3, 5))
)
# 180 has to be worth a single press, or every upside down placement reads as a
# fault for anyone who uses the key.
check(
    'a 180 costs one press, not two',
    fin.optimal(T, 2, 4) == 1, str(fin.optimal(T, 2, 4))
)


# --- Driving the real game. -------------------------------------------------
def start(form=None, rule=us.FINESSE_COUNT):
    """Begin a game with a piece in play, optionally forcing which piece."""
    user.finesse = rule
    user.state = 'game'
    user.gametype = 'free'
    user.reset()
    core.set_data()
    for _ in range(40):
        core.run()
        if core.entry_flag:
            break
    if form is not None:
        core.set_shape(form)
        core.eval_ghost()
    core.finesse_inputs = 0
    return core.freeshape.form


def tap(key, times=1):
    """Press and release a key, one game frame each way."""
    for _ in range(times):
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
        core.run()
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYUP, key=key))
        core.run()
    pg.event.clear()


def hold_key(key, frames):
    """Press a key and keep holding it, so auto-shift takes over."""
    pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
    for _ in range(frames):
        core.run()
        pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYUP, key=key))
    core.run()
    pg.event.clear()


def wall(column, height, hole=None):
    """Stack blocks into a column, so a piece has something to tuck under."""
    for row in range(22 - height, 22):
        if hole is not None and row == hole:
            continue
        core.grid.cells[row][column] = Block([column, row], 7, fallen=True)


# --- Counting what the player actually pressed. -----------------------------
start(T)
tap(pg.K_RIGHT, 3)
check('taps are counted one for one', core.finesse_inputs == 3, str(core.finesse_inputs))

start(T)
before = core.freeshape.pos[0]
hold_key(pg.K_RIGHT, 40)
check(
    'holding a direction is one press however far it goes',
    core.finesse_inputs == 1 and core.freeshape.pos[0] > before + 1,
    '{} press(es), {} to {}'.format(core.finesse_inputs, before, core.freeshape.pos[0])
)

start(T)
tap(pg.K_z)
tap(pg.K_x)
tap(pg.K_a)
check('rotations are counted', core.finesse_inputs == 3, str(core.finesse_inputs))

start(T)
tap(pg.K_RIGHT, 2)
tap(pg.K_DOWN)
check('soft drop is not a finesse input', core.finesse_inputs == 2, str(core.finesse_inputs))

start(T)
tap(pg.K_RIGHT, 3)
tap(pg.K_LSHIFT)
check(
    'holding a piece starts the count over',
    core.finesse_inputs == 0, str(core.finesse_inputs)
)


# --- The verdict, on placements the player committed themselves. ------------
def settled():
    """How many cells the board holds, so a lock can be told from a retry.

    entry_flag is no use for that: with ARE at its default of none, the next
    piece spawns on the very frame after the last one locked, so the flag is
    back to True by the time anyone looks at it.
    """
    return sum(cell is not None for row in core.grid.cells[:22] for cell in row)


def place(form, presses, rule=us.FINESSE_COUNT):
    """Make a placement out of a list of keys and report what finesse made of it.

    Returns the judgement this one placement drew, not the running totals, so a
    check can add up several without start() wiping the count between them.
    """
    start(form, rule)
    for key in presses:
        tap(key)
    before = (user.finesse_judged, user.finesse_faults, user.finesse_wasted)
    tap(pg.K_SPACE)
    return tuple(
        now - was for now, was
        in zip((user.finesse_judged, user.finesse_faults, user.finesse_wasted), before)
    )


judged, faults, wasted = place(T, [pg.K_LEFT] * 3)
check(
    'three taps where one hold would do is a fault',
    (judged, faults) == (1, 1), '{} judged, {} faulted'.format(judged, faults)
)
check(
    'the fault says how much was wasted',
    core.finesse_label == 'FINESSE +2', core.finesse_label
)
check('the fault puts a banner up', core.finesse_frames > 0, str(core.finesse_frames))

judged, faults, wasted = place(T, [pg.K_LEFT])
check(
    'one tap for one column is not a fault',
    (judged, faults) == (1, 0), '{} judged, {} faulted'.format(judged, faults)
)

judged, faults, wasted = place(T, [])
check(
    'dropping the piece where it spawned is not a fault',
    (judged, faults) == (1, 0), '{} judged, {} faulted'.format(judged, faults)
)

# Rotating back and forth to end up where you started is pure waste, and is the
# fault a counter that only watched movement would miss.
judged, faults, wasted = place(T, [pg.K_z, pg.K_x])
check(
    'turning a piece back to where it was is a fault',
    (judged, faults) == (1, 1), '{} judged, {} faulted'.format(judged, faults)
)

# Beating the table is not a fault. Sliding a piece into a wall with a held key
# and letting go early takes one press for a distance the table charges two for.
start(T)
hold_key(pg.K_LEFT, 6)
judged, faults = user.finesse_judged, user.finesse_faults
tap(pg.K_SPACE)
check(
    'using fewer presses than the table is never a fault',
    user.finesse_faults == faults, str(user.finesse_faults - faults)
)


# --- What must never be judged. ---------------------------------------------
# A tuck: the piece ends up under a shelf, which no drop from the spawn row
# reaches. Faulting these would punish the downstacking this trainer is for, so
# the check is built so that it WOULD fault if the exemption were missing - three
# taps to a column the table reaches in one.
start(T, us.FINESSE_COUNT)
for column in range(4):
    core.grid.cells[18][column] = Block([column, 18], 7, fallen=True)
core.set_shape(T)
# Already on the floor, so the hard drop below moves it nowhere and what is
# judged is exactly what is being looked at here.
core.freeshape.pos = core.newshape.pos = [4, 21]
core.eval_ghost()
core.finesse_inputs = 0
tap(pg.K_LEFT, 3)
tucked = core.drop_reachable()
before = (user.finesse_judged, core.freeshape.pos[:])
tap(pg.K_SPACE)
check(
    'a piece tucked under a shelf is left alone',
    not tucked and user.finesse_judged == before[0],
    'reachable {}, at {}, {} judged'.format(
        tucked, before[1], user.finesse_judged - before[0])
)
# ...and the same placement out in the open, with the shelf gone, is faulted, so
# the check above is measuring the exemption rather than a piece nobody judged.
start(T, us.FINESSE_COUNT)
core.set_shape(T)
core.freeshape.pos = core.newshape.pos = [4, 21]
core.eval_ghost()
core.finesse_inputs = 0
tap(pg.K_LEFT, 3)
open_reach = core.drop_reachable()
before = (user.finesse_judged, user.finesse_faults)
tap(pg.K_SPACE)
check(
    'the same three taps in the open are a fault',
    open_reach and (user.finesse_judged, user.finesse_faults) == (before[0] + 1, before[1] + 1),
    'reachable {}, {} judged, {} faulted'.format(
        open_reach, user.finesse_judged - before[0], user.finesse_faults - before[1])
)

# A spin, for the same reason: a T wedged into a slot got there by a rotation the
# tables do not model, and every press that set it up would read as waste. The
# spin bonus and a finesse fault must never arrive together.
start(T, us.FINESSE_COUNT)
core.set_shape(T)
core.freeshape.rotate(True)
core.freeshape.rotate(True)
core.freeshape.pos = core.newshape.pos = [5, 18]
core.newshape = core.freeshape.copy()
core.eval_ghost()
# The overhang that makes it a spin rather than a drop: the cells either side of
# the T's nub, one row above it.
for column in (4, 6):
    core.grid.cells[17][column] = Block([column, 17], 7, fallen=True)
for column, row in ((4, 19), (6, 19), (4, 18), (6, 18)):
    core.grid.cells[row][column] = Block([column, row], 7, fallen=True)
core.grid.update()
core.finesse_inputs = 4
spun = core.drop_reachable()
before = user.finesse_judged
tap(pg.K_SPACE)
check(
    'a piece spun into a slot is left alone',
    not spun and user.finesse_judged == before,
    'reachable {}, {} judged'.format(spun, user.finesse_judged - before)
)

# The forced drop: the timer chose the placement, so the player is not charged
# for the presses they had not finished making.
start(T, us.FINESSE_COUNT)
user.forced_delay = 0.001
judged, faults = user.finesse_judged, user.finesse_faults
tap(pg.K_LEFT, 4)
for _ in range(40):
    core.run()
    if not core.entry_flag:
        break
check(
    'a placement the timer took is not judged',
    user.finesse_judged == judged and user.finesse_faults == faults,
    '{} judged, {} faulted'.format(user.finesse_judged - judged, user.finesse_faults - faults)
)
user.forced_delay = 600.

# Off means off, all the way down to the counters.
judged, faults, wasted = place(T, [pg.K_LEFT] * 4, us.FINESSE_OFF)
check(
    'nothing is counted with finesse off',
    (judged, faults) == (0, 0), '{} judged, {} faulted'.format(judged, faults)
)
user.finesse = us.FINESSE_COUNT


# --- Retry: the piece comes back. -------------------------------------------
start(T, us.FINESSE_RETRY)
tap(pg.K_LEFT, 3)
filled = settled()
core.piece_elapsed = 0.4
tap(pg.K_SPACE)
check(
    'a faulted piece is handed back rather than locked',
    core.entry_flag and core.freeshape.pos == list(Shape().pos),
    '{} at {}'.format('in play' if core.entry_flag else 'gone', core.freeshape.pos)
)
check(
    'the board is left exactly as it was',
    settled() == filled, '{} against {}'.format(settled(), filled)
)
check(
    'the piece handed back is the same piece',
    core.freeshape.form == T, NAMES[core.freeshape.form]
)
check(
    'the retry starts the count over',
    core.finesse_inputs == 0, str(core.finesse_inputs)
)
check(
    'the retry keeps the time the piece has already spent',
    0.4 <= core.piece_elapsed < 0.45, str(core.piece_elapsed)
)

# The retry has to be survivable: place it properly and it goes down.
tap(pg.K_LEFT)
judged, faults, filled = user.finesse_judged, user.finesse_faults, settled()
tap(pg.K_SPACE)
check(
    'placing the retry cleanly locks it',
    settled() > filled and (user.finesse_judged, user.finesse_faults) == (judged + 1, faults),
    '{} cells against {}, {} new fault(s)'.format(
        settled(), filled, user.finesse_faults - faults)
)

# Counting must not hand pieces back, or the two rules would be the same rule.
start(T, us.FINESSE_COUNT)
tap(pg.K_LEFT, 3)
filled = settled()
tap(pg.K_SPACE)
check(
    'counting alone never takes a placement back',
    settled() > filled, '{} cells against {}'.format(settled(), filled)
)


# --- The running figure. ----------------------------------------------------
start(T, us.FINESSE_COUNT)
user.reset()
check('a game with nothing placed reads as clean', user.finesse_rate() == 1.)
user.finesse_judged, user.finesse_faults = 4, 1
check('the rate is clean placements over judged ones', user.finesse_rate() == 0.75, str(user.finesse_rate()))

rounds = [place(T, [pg.K_LEFT] * 3) for _ in range(3)]
check(
    'every placement is judged the same way',
    [tuple(r) for r in rounds] == [(1, 1, 2)] * 3, str(rounds)
)
check(
    'the last of them left the rate on the floor',
    user.finesse_rate() == 0., str(user.finesse_rate())
)

user.reset()
check('a new game clears the record', user.finesse_judged == 0 and user.finesse_wasted == 0)


# --- The screen. ------------------------------------------------------------
start(T, us.FINESSE_COUNT)
user.finesse_judged, user.finesse_faults, user.finesse_wasted = 20, 3, 5
user.b2b, user.combo_ctr = 4, 3
core.finesse_label = 'FINESSE +2'
core.finesse_frames = core.banner_frames
# Drawn the way a real frame draws it, background first, or the shot shows the
# leftovers of the last one and hides whatever is actually overlapping.
env.screen.blit(core.bg, (0, 0))
core.grid.update()
core.display()
# Into the throwaway working directory, not the repository.
shot = os.path.join(os.getcwd(), 'finesse-hud.png')
pg.image.save(env.screen, shot)

# Nothing on that side of the panel may sit on top of anything else. The rows
# there are drawn at fixed heights, so a new one is only ever one careless
# number away from landing on the combo counters.
STACKED = (
    ('finesse', core.grid.rect.y + 375),
    ('faults', core.grid.rect.y + 400),
    ('b2b', core.grid.rect.y + 420),
    ('combo', core.grid.rect.y + 450),
    ('attack', core.grid.rect.y + 480),
)
line = core.font.get_height()
overlap = [
    '{} on {}'.format(STACKED[i][0], STACKED[i + 1][0])
    for i in range(len(STACKED) - 1)
    if STACKED[i][1] + line > STACKED[i + 1][1]
]
print('     (HUD rendered to {})'.format(shot))
check('the right hand readouts do not sit on each other', not overlap, str(overlap))
check(
    'they all stay inside the panel',
    STACKED[-1][1] + line <= core.grid.rect.bottom,
    '{} against {}'.format(STACKED[-1][1] + line, core.grid.rect.bottom)
)

# Every row of the settings screen has to stay inside its own panel.
settings.reset()
overflow = [
    option.action for option in settings.selections[0]
    if option.rect.bottom > settings.rect.bottom - 28
]
check('the settings rows still fit the panel', not overflow, str(overflow))
check(
    'the finesse row is on the settings screen',
    any(option.action == 'finesse' for option in settings.selections[0])
)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
