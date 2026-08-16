"""Regression checks for spin detection and the banner it puts on screen.

Boards are built by hand here rather than played into, because the interesting
cases - a T wedged under an overhang, an S slotted into a notch - take a
specific stack that a random bag will not hand you.

Run with: python tools/test_spins.py
"""
import os
import sys
import tempfile
from argparse import Namespace

os.environ['SDL_VIDEODRIVER'] = 'dummy'
os.environ['SDL_AUDIODRIVER'] = 'dummy'

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(tempfile.mkdtemp())

import pygame as pg
import engine.game as G
import engine.controls as ctl
import engine.userstate as us
from engine.shapes import Shape, Block

pg.key.get_focused = lambda: True
ctl.CONFIG = os.path.join(tempfile.mkdtemp(), 'controls.json')

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


tetris = G.init(Namespace(debug=False, forced_delay=600., volume=0., sfx_volume=0.))
user = tetris.user
core = tetris.game
settings = tetris.settings_menu
ctl.reset(user)


def blank_board():
    user.state = 'game'
    user.gametype = 'free'
    user.reset()
    core.set_data()
    for _ in range(40):
        core.run()
        if core.entry_flag:
            break


def fill(cells):
    """Drop blocks into the given (column, row) cells."""
    for x, y in cells:
        core.grid[y][x] = Block([x, y], 7, fallen=True)
    core.grid.update()


def place(form, state, x, y):
    """Put a piece of the given form and orientation at a spot on the board."""
    core.set_shape(form)
    for _ in range(state):
        core.freeshape.rotate(True)
    core.freeshape.pos[0] = core.newshape.pos[0] = x
    core.freeshape.pos[1] = core.newshape.pos[1] = y
    core.newshape = core.freeshape.copy()
    core.eval_ghost()


def spin_of(rule, rotated=True):
    """Ask the detector what the current placement counts as."""
    user.spinrule = rule
    core.rotated_last = rotated
    return core.eval_spin()


# A T only needs its diagonals filled to satisfy the corner rule, and a piece
# never occupies its own diagonals - so rather than building a stack and hoping
# the T lands in it, put the T down and fill the corners around it.
CORNERS = {'nw': (-1, -1), 'ne': (1, -1), 'sw': (-1, 1), 'se': (1, 1)}


def t_with_corners(state, corners, x=5, y=18):
    blank_board()
    place(2, state, x, y)
    fill([(x + dx, y + dy) for name, (dx, dy) in CORNERS.items() if name in corners])


# --- The corner rule, and its bounds. ---------------------------------------
# Facing down, the front corners are the two below.
t_with_corners(2, ('sw', 'se', 'nw'))
count, fronts = core.eval_corners()
check('three filled diagonals satisfy the corner rule', count == 3, '{} corners'.format(count))
check('both front corners make it a full spin', fronts, 'fronts {}'.format(fronts))

t_with_corners(2, ('sw', 'nw', 'ne'))
count, fronts = core.eval_corners()
check('one front corner short makes it a mini', count == 3 and not fronts, '{} corners, fronts {}'.format(count, fronts))

blank_board()
place(2, 0, 5, 5)       # out in the open
count, fronts = core.eval_corners()
check('an unobstructed T has no corners', count == 0, '{} corners'.format(count))
check('an unobstructed T is not immobile', not core.is_immobile())

# Cells off the board count as filled, and must not wrap to the far wall.
blank_board()
check('outside the left edge reads as filled', core.filled(-1, 10))
check('outside the right edge reads as filled', core.filled(len(core.grid[0]), 10))
check('above the board reads as filled', core.filled(4, -1))
check(
    'a cell just outside is not read from the other side',
    core.filled(-1, 10) and core.grid[10][len(core.grid[0]) - 1] is None,
    'the far column is empty, so a wrap would have reported False'
)

# --- What each rule detects. ------------------------------------------------
for rule, expected in (
    (us.SPIN_OFF, None), (us.SPIN_TSPIN, 'T'), (us.SPIN_ALL, 'T'), (us.SPIN_ALL_MINI, 'T'),
):
    t_with_corners(2, ('sw', 'se', 'nw'))
    spin = spin_of(rule)
    check(
        'a T on three corners under {} is {}'.format(us.SPIN_RULES[rule], expected or 'nothing'),
        (spin[0] if spin else None) == expected, str(spin)
    )

# Nothing counts unless the last thing the player did was rotate.
t_with_corners(2, ('sw', 'se', 'nw'))
check('a T that was shifted into place is not a spin', spin_of(us.SPIN_ALL, rotated=False) is None)

# An S wedged into a notch: all-spin sees it, classic does not.
def sspin_board():
    blank_board()
    fill([(x, 21) for x in range(10) if x != 4])
    fill([(x, 20) for x in range(10) if x not in (4, 5)])
    fill([(x, 19) for x in range(10) if x not in (5, 6)])

sspin_board()
place(3, 1, 5, 20)      # S on its side, slotted into the stair
immobile = core.is_immobile()
check('the S sits wedged in the notch', immobile, 'immobile {}'.format(immobile))
check('classic ignores a wedged S', spin_of(us.SPIN_TSPIN) is None)
check('all-spin sees a wedged S', (spin_of(us.SPIN_ALL) or (None,))[0] == 'S', str(spin_of(us.SPIN_ALL)))

blank_board()
place(3, 0, 5, 5)
check('all-spin ignores an S dropped in the open', spin_of(us.SPIN_ALL) is None)

# --- Full against mini. -----------------------------------------------------
t_with_corners(2, ('sw', 'nw', 'ne'))     # a mini by the corner rule
core.user.twist_flag = False
check(
    'classic reports that T as a mini',
    spin_of(us.SPIN_TSPIN)[1] is False, str(spin_of(us.SPIN_TSPIN))
)
check(
    'plain all-spin calls the same placement full',
    spin_of(us.SPIN_ALL)[1] is True, str(spin_of(us.SPIN_ALL))
)
sspin_board()
place(3, 1, 5, 20)
core.user.twist_flag = False
spin = spin_of(us.SPIN_ALL_MINI)
check(
    'with minis on, a non-T that needed no kick is a mini',
    spin is not None and spin[1] is False, str(spin)
)
core.user.twist_flag = True
spin = spin_of(us.SPIN_ALL_MINI)
check(
    'and one that needed a kick is full',
    spin is not None and spin[1] is True, str(spin)
)

# --- The banner. ------------------------------------------------------------
user.spinrule = us.SPIN_ALL
t_with_corners(2, ('sw', 'se', 'nw'))
core.rotated_last = True
core.spin_frames = 0
core.announce_spin()
check('a spin raises a banner', core.spin_frames > 0 and core.spin_label == 'T-SPIN', core.spin_label)
check('the spin is marked for the scorer', user.tspin_flag)
core.announce_clear(2)
check('a clear that follows joins the banner', core.spin_count == 'DOUBLE', core.spin_count)
core.announce_clear(4)
check('four lines read as a quad', core.spin_count == 'QUAD', core.spin_count)

# The banner clears itself rather than sitting there for the rest of the game.
core.spin_frames = 2
for _ in range(4):
    core.run()
check('the banner times out', core.spin_frames == 0, '{} frames left'.format(core.spin_frames))

# A clear with no spin behind it must not resurrect the old banner.
core.spin_frames = 0
core.spin_count = ''
core.announce_clear(4)
check(
    'a plain clear puts no banner up',
    core.spin_frames == 0,
    'the count is held as {} in case a perfect clear a moment later wants it'.format(core.spin_count)
)

# A mini reads as one.
sspin_board()
place(3, 1, 5, 20)
user.spinrule = us.SPIN_ALL_MINI
core.rotated_last = True
core.user.twist_flag = False
core.announce_spin()
check('a mini says so on the banner', core.spin_label == 'MINI S-SPIN', core.spin_label)

# It has to fit the panel it is drawn in, which is about 140px wide.
widest = max(
    core.bannerfont.size('MINI {}-SPIN'.format(name))[0] for name in G.SHAPE_NAMES
)
check('the widest banner fits the side panel', widest <= 140, '{}px'.format(widest))

# --- Spin bonuses actually reach the score. ---------------------------------
# The flags used to be cleared at lock, before the line clearer got round to
# scoring, so the multiplier never once applied.
user.spinrule = us.SPIN_ALL
t_with_corners(2, ('sw', 'se', 'nw'))
core.rotated_last = True
user.twist_flag = False
user.tspin_flag = False
core.eval_fallen(0)
check(
    'the spin flag survives the lock',
    user.tspin_flag, 'tspin_flag is {} once the piece is down'.format(user.tspin_flag)
)
for _ in range(160):
    core.run()
    if not core.clearing:
        break
check('the clear resolved', not core.clearing)
check('and the spin scored something', user.score > 0, 'score {}'.format(user.score))

# Spawning is what clears the flags now.
core.set_shape(0)
check('spawning clears the spin flags', not user.tspin_flag and not user.twist_flag)

# --- Perfect clears. --------------------------------------------------------
# One row, one gap, and an I laid flat into it: the board ends up empty.
def perfect_board():
    blank_board()
    fill([(x, 21) for x in range(10) if x not in (3, 4, 5, 6)])

perfect_board()
check('the board is not empty with a row down', not core.board_empty())
place(0, 1, 5, 20)      # I stood up, so the row does not complete
check('a placement that leaves blocks is no perfect clear', not core.board_empty())

blank_board()
check('an untouched board reads as empty', core.board_empty())

# The floor row underneath the playing field must not be mistaken for a block.
check(
    'the fixed floor does not count against it',
    core.board_empty() and any(core.grid[len(core.grid) - 1]),
    'the last row is full, and is the floor rather than play area'
)

# Played for real: fill a row bar four cells, drop an I into the gap, and let the
# clearer run to the end.
perfect_board()
user.spinrule = us.SPIN_OFF
place(0, 0, 4, 21)
core.rotated_last = False
core.eval_fallen(0)
for _ in range(200):
    core.run()
    if not core.clearing:
        break
check('the clear resolved', not core.clearing)
check('the board really is empty', core.board_empty(), 'grid still holds blocks')
check(
    'a perfect clear raises its own banner',
    core.spin_perfect and core.spin_frames > 0,
    'perfect {}, {} frames'.format(core.spin_perfect, core.spin_frames)
)
check('and names what cleared alongside it', core.spin_count == 'SINGLE', core.spin_count)

# A clear that leaves the board occupied says nothing. Same placement as above,
# with one block parked where the clear cannot reach it.
perfect_board()
fill([(9, 15)])
place(0, 0, 4, 21)
core.rotated_last = False
core.eval_fallen(0)
for _ in range(200):
    core.run()
    if not core.clearing:
        break
check(
    'an ordinary clear raises no banner',
    not core.spin_perfect and core.spin_frames == 0,
    'perfect {}, {} frames'.format(core.spin_perfect, core.spin_frames)
)

# It has to fit the panel, like the other two lines.
check(
    'the perfect clear line fits the side panel',
    core.bannerfont.size('PERFECT CLEAR')[0] <= 140,
    '{}px'.format(core.bannerfont.size('PERFECT CLEAR')[0])
)
check('the cue exists', 'perfect' in G.env.sounds, '{} effects loaded'.format(len(G.env.sounds)))

# --- Back to back, and the combo counter beside it. -------------------------
def clear_event(lines, spin=False):
    """Run one placement's worth of clear through the real scorer."""
    user.line_list = [lines]
    user.tspin_flag = spin
    user.eval_clear_score(False)


user.reset()
check('a fresh game has no chain', user.b2b == 0 and user.combo_ctr == 0)

clear_event(4)
check('a quad opens the chain', user.b2b == 1, 'b2b {}'.format(user.b2b))
clear_event(4)
check('a second quad carries it', user.b2b == 2, 'b2b {}'.format(user.b2b))
clear_event(2, spin=True)
check('a spin clear carries it too', user.b2b == 3, 'b2b {}'.format(user.b2b))
clear_event(2)
check('a plain double breaks it', user.b2b == 0, 'b2b {}'.format(user.b2b))

# A placement that clears nothing leaves the chain where it was - it only ends
# when a clear that is not difficult actually happens.
clear_event(4)
before = user.b2b
clear_event(0)
check('a placement with no clear leaves the chain alone', user.b2b == before, 'b2b {}'.format(user.b2b))

# The combo counter is the base game's, and counts consecutive clears.
user.reset()
for expected in (1, 2, 3):
    clear_event(1)
    check('clear {} runs the combo to {}'.format(expected, expected), user.combo_ctr == expected, str(user.combo_ctr))
clear_event(0)
check('a placement with no clear breaks the combo', user.combo_ctr == 0, str(user.combo_ctr))

# --- And both say so out loud. ----------------------------------------------
fired = []
real_play = G.env.play_sound
G.env.play_sound = lambda name: fired.append(name)


def chains_after(combo, b2b, cleared=True):
    """Set the counters as the scorer would have, then ask for the cues."""
    del fired[:]
    user.combo_ctr = combo
    user.b2b = b2b
    core.cleared_lines = cleared
    core.announce_chains()
    return list(fired)


check('a first clear is no combo yet', chains_after(1, 0) == [])
check('a second clear opens the ladder', chains_after(2, 0) == ['combo1'], str(chains_after(2, 0)))
check('the ladder climbs a rung per clear', chains_after(5, 0) == ['combo4'], str(chains_after(5, 0)))
check(
    'the ladder stops climbing at its top rung',
    chains_after(40, 0) == ['combo{}'.format(G.env.COMBO_STEPS)], str(chains_after(40, 0))
)
check('a lone difficult clear is not back to back yet', chains_after(1, 1) == [])
check('a second one is', 'b2b' in chains_after(2, 2), str(chains_after(2, 2)))
check('both can sound together', chains_after(3, 3) == ['combo2', 'b2b'], str(chains_after(3, 3)))
check('a placement that cleared nothing is silent', chains_after(5, 5, cleared=False) == [])

# Every rung the ladder can reach has to exist as a file.
G.env.play_sound = real_play
missing = [
    'combo{}'.format(step) for step in range(1, G.env.COMBO_STEPS + 1)
    if 'combo{}'.format(step) not in G.env.sounds
]
check('every rung of the ladder is loaded', not missing, 'missing {}'.format(missing))
check('the back to back cue is loaded', 'b2b' in G.env.sounds)

# Both are drawn beside the queue, and only once they mean something.
user.reset()
blank_board()
user.b2b = 3
user.combo_ctr = 4
core.display()
user.b2b = 1
user.combo_ctr = 1
core.display()
check('the counters draw without complaint at either end of their range', True)

# --- The settings row. ------------------------------------------------------
user.state = 'settings_menu'
settings.reset()
for _ in range(5):
    pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_DOWN))
    settings.run()
    pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYUP, key=pg.K_DOWN))
    settings.run()
check('the spins row is where it should be', settings.selected.action == 'spins', settings.selected.action)
before = user.spinrule
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_RIGHT))
settings.run()
check(
    'right cycles the spin rule',
    user.spinrule == (before + 1) % len(us.SPIN_RULES),
    '{} -> {}'.format(us.SPIN_RULES[before], us.SPIN_RULES[user.spinrule])
)
check('the row names the rule', us.SPIN_RULES[user.spinrule] in settings.label('spins'), settings.label('spins'))

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
