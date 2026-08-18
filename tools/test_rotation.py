"""Regression checks for 180 rotation.

The dangerous failure here is not a missing kick, it is a rotation that collides
and gets committed anyway: wall_kick only reverts a piece when a branch of its
kick table matched the state change, so a transition nobody wrote a branch for
lands the piece inside the stack. The exhaustive check at the end is there to
catch exactly that, for every piece, every orientation and every board it can be
rotated against.

Run with: python tools/test_rotation.py
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
import engine.controls as ctl
from engine.shapes import Shape

pg.key.get_focused = lambda: True

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


tetris = G.init(Namespace(debug=False, forced_delay=600., volume=0., sfx_volume=0.))
user = tetris.user
core = tetris.game
ctl.reset(user)

FORMS = {0: 'I', 1: 'O', 2: 'T', 3: 'S', 4: 'Z', 5: 'J', 6: 'L'}


def start_game():
    user.state = 'game'
    user.gametype = 'free'
    user.reset()
    core.set_data()
    for _ in range(40):
        core.run()
        if core.entry_flag:
            return


def rotate_180():
    pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_a))
    core.run()
    pg.event.clear()


# --- It is bound, and it is its own action. ---------------------------------
check('180 has a default binding', ctl.describe(user, 'rotate_180') == 'A', ctl.describe(user, 'rotate_180'))
check(
    '180 is listed on the rebinding screen',
    any(action == 'rotate_180' for action, label in ctl.ACTIONS)
)
check(
    'the rebinding screen has a row for it',
    any(o.action == 'rotate_180' for o in tetris.controls_menu.selections[0])
)

# --- On open ground it is simply two turns. ---------------------------------
start_game()
for form in sorted(FORMS):
    if form == 1:
        continue   # O has no rotation states to move between.
    core.set_shape(form)
    before = core.freeshape.state
    rotate_180()
    check(
        '{} turns two states on open ground'.format(FORMS[form]),
        core.freeshape.state == (before + 2) % 4,
        'state {} -> {}'.format(before, core.freeshape.state)
    )

# The O piece has no rotation to do, but must not be left somewhere else.
core.set_shape(1)
where = core.freeshape.pos[:]
rotate_180()
check('O does not wander when rotated', core.freeshape.pos == where, str(core.freeshape.pos))

# --- A rebound key drives it. -----------------------------------------------
ctl.bind(user, 'rotate_180', pg.K_v)
start_game()
core.set_shape(2)
before = core.freeshape.state
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_a))
core.run()
check('the old 180 key stops rotating', core.freeshape.state == before, 'state {}'.format(core.freeshape.state))
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_v))
core.run()
check('the rebound 180 key rotates', core.freeshape.state == (before + 2) % 4, 'state {}'.format(core.freeshape.state))
ctl.reset(user)

# --- Nothing it does may leave the piece inside the stack. ------------------
# Every piece, every orientation, against a board with a wall of garbage, from
# every column. A transition with no kick branch shows up here as an overlap.
start_game()
overlaps = []
refused = []
tried = jammed = rescued = 0
for row in range(14, 22):
    core.grid.add_garbage()
for form in sorted(FORMS):
    if form == 1:
        continue   # O never changes state, so there is nothing to refuse or rescue.
    for state in range(4):
        for column in range(0, 12):
            for depth in (0, 6, 12, 17):
                # Rebuilt every iteration: wall_kick replaces freeshape, so reusing
                # it would silently start the next case from the rotated piece.
                core.set_shape(form)
                for _ in range(state):
                    core.freeshape.rotate(True)
                core.freeshape.pos[0] = core.newshape.pos[0] = column
                core.freeshape.pos[1] = core.newshape.pos[1] = depth
                if core.check_collision(core.freeshape):
                    continue
                core.newshape = core.freeshape.copy()
                tried += 1
                core.newshape.rotate(True)
                core.newshape.rotate(True)
                # Whether the plain rotation would have overlapped decides which
                # of the two outcomes below this position is evidence for.
                blocked = core.check_collision(core.newshape)
                core.wall_kick()
                turned = core.freeshape.state == (state + 2) % 4
                if core.check_collision(core.freeshape):
                    overlaps.append((FORMS[form], state, column, depth))
                if blocked:
                    jammed += 1
                    if turned:
                        rescued += 1
                elif not turned:
                    refused.append((FORMS[form], state, column, depth))
check(
    'a 180 never commits a piece that collides',
    not overlaps,
    '{} positions rotated cleanly'.format(tried) if not overlaps
    else '{} overlaps, first {}'.format(len(overlaps), overlaps[0])
)
check('the sweep actually exercised something', tried > 200, '{} positions'.format(tried))
check(
    'a 180 that would overlap gets kicked clear rather than refused',
    jammed > 0 and rescued > 0,
    '{} of {} blocked rotations were saved by a kick'.format(rescued, jammed)
)
# --- The same sweep with wall kicks switched off. ---------------------------
# Kicks off is a setting, not a hypothetical, and the rule is the same either
# way: a rotation may be refused, but it may never be committed on top of the
# stack. Before this check the piece was left overlapping, and the next
# placement took paste_shape's assert down with it.
start_game()
for row in range(14, 22):
    core.grid.add_garbage()
user.enablekicks = False
unkicked = []
tried_off = refused_off = 0
try:
    for form in sorted(FORMS):
        if form == 1:
            continue
        for state in range(4):
            for column in range(0, 12):
                for depth in (0, 6, 12, 17):
                    for turns in (1, 2, 3):
                        core.set_shape(form)
                        for _ in range(state):
                            core.freeshape.rotate(True)
                        core.freeshape.pos[0] = core.newshape.pos[0] = column
                        core.freeshape.pos[1] = core.newshape.pos[1] = depth
                        if core.check_collision(core.freeshape):
                            continue
                        core.newshape = core.freeshape.copy()
                        tried_off += 1
                        for _ in range(turns):
                            core.newshape.rotate(True)
                        core.wall_kick()
                        if core.check_collision(core.freeshape):
                            unkicked.append((FORMS[form], state, turns, column, depth))
                        elif core.freeshape.state == state:
                            refused_off += 1
finally:
    user.enablekicks = True
check(
    'with kicks off, a rotation is refused rather than forced through',
    not unkicked,
    '{} positions'.format(tried_off) if not unkicked
    else '{} overlaps, first {}'.format(len(unkicked), unkicked[0])
)
check(
    'and that sweep really did have rotations to refuse',
    tried_off > 200 and refused_off > 0,
    '{} tried, {} refused'.format(tried_off, refused_off)
)

check(
    'a 180 with room is never refused',
    not refused, 'refused {} unobstructed rotations, first {}'.format(len(refused), refused[:1])
)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
