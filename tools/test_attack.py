"""Regression checks for the attack numbers: the table, and the game feeding it.

The table is arithmetic and is checked against the canonical TETR.IO values by
name - a wrong entry is wrong for every player forever. The rest is plumbing,
and the failure that matters there is the silent kind: a bonus read from the
wrong counter, a spin scored as a plain clear, a garbage row dug out and not
counted. Those are checked by playing real placements through the engine and
reading what was recorded.

Run with: python tools/test_attack.py
"""
import os
import sys
import tempfile
from argparse import Namespace

os.environ['SDL_VIDEODRIVER'] = 'dummy'
os.environ['SDL_AUDIODRIVER'] = 'dummy'
os.environ['FORCETRIS_CONFIG'] = os.path.join(tempfile.mkdtemp(), 'settings.json')
os.environ['FORCETRIS_REPLAYS'] = os.path.join(tempfile.mkdtemp(), 'replays')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(tempfile.mkdtemp())

import pygame as pg
import engine.game as G
import engine.attack as atk
import engine.userstate as us
from engine.shapes import Block

pg.key.get_focused = lambda: True

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


# --- The table, against the numbers TETR.IO players know by heart. ----------
CANON = (
    # (lines, spin, b2b, combo, perfect) -> attack
    ('a single sends nothing',            (1, atk.NOT_SPIN, False, 0, False), 0),
    ('a double sends one',                (2, atk.NOT_SPIN, False, 0, False), 1),
    ('a triple sends two',                (3, atk.NOT_SPIN, False, 0, False), 2),
    ('a quad sends four',                 (4, atk.NOT_SPIN, False, 0, False), 4),
    ('a spin single sends two',           (1, atk.SPIN_FULL, False, 0, False), 2),
    ('a spin double sends four',          (2, atk.SPIN_FULL, False, 0, False), 4),
    ('a spin triple sends six',           (3, atk.SPIN_FULL, False, 0, False), 6),
    ('a spin quad sends ten',             (4, atk.SPIN_FULL, False, 0, False), 10),
    ('a mini single sends nothing',       (1, atk.SPIN_MINI, False, 0, False), 0),
    ('a mini double sends one',           (2, atk.SPIN_MINI, False, 0, False), 1),
    ('back to back adds one to a quad',   (4, atk.NOT_SPIN, True, 0, False), 5),
    ('back to back adds one to a spin',   (2, atk.SPIN_FULL, True, 0, False), 5),
    ('a spin that cleared nothing sends nothing', (0, atk.SPIN_FULL, True, 0, False), 0),
    ('a perfect clear adds ten',          (1, atk.NOT_SPIN, False, 0, True), 10),
    ('a perfect quad is a mouthful',      (4, atk.NOT_SPIN, True, 3, True), 16),
)
for name, args, expected in CANON:
    got = atk.attack_for(*args)
    check(name, got == expected, '{} against {}'.format(got, expected))

# The combo ladder climbs the classic table and stays flat past the end.
LADDER = [atk.attack_for(1, combo=c) for c in range(16)]
check(
    'the combo ladder climbs the classic table',
    LADDER[:13] == [0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5],
    str(LADDER[:13])
)
check('and stays flat past the end', LADDER[13:] == [5, 5, 5], str(LADDER[13:]))
check(
    'a run of more than four lines is scored as a quad',
    atk.attack_for(6) == atk.attack_for(4), str(atk.attack_for(6))
)

# The banner labels map to the right table rows.
check('no banner reads as no spin', atk.spin_kind('') == atk.NOT_SPIN)
check('a full banner reads as full', atk.spin_kind('T-SPIN') == atk.SPIN_FULL)
check('a mini banner reads as mini', atk.spin_kind('MINI S-SPIN') == atk.SPIN_MINI)

# The rates are plain arithmetic, but the divide-by-zero guard is not optional.
check('APM is attack per minute', abs(atk.apm(30, 60.) - 30.) < 1e-9)
check('APM of an instant game is zero', atk.apm(5, 0.) == 0.)
check('VS counts the downstack too', abs(atk.vs_score(30, 10, 100.) - 40.) < 1e-9)


# --- The game feeding the table. --------------------------------------------
tetris = G.init(Namespace(debug=False, forced_delay=0., volume=0., sfx_volume=0.))
user = tetris.user
core = tetris.game


def tap(key, times=1):
    for _ in range(times):
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
        core.run()
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYUP, key=key))
        core.run()
    pg.event.clear()


def new_game():
    user.state = 'game'
    user.gametype = 'free'
    user.reset()
    core.set_data()
    for _ in range(40):
        core.run()
        if core.entry_flag:
            break


def settle():
    for _ in range(120):
        core.run()
        if core.recorder.pending is None and core.entry_flag:
            return True
    return False


def fill(cells, colour=7):
    for x, y in cells:
        core.grid.cells[y][x] = Block([x, y], colour, fallen=True)
    core.grid.update()


def place(form, state, x, y):
    core.set_shape(form)
    for _ in range(state):
        core.freeshape.rotate(True)
    core.freeshape.pos = core.newshape.pos = [x, y]
    core.newshape = core.freeshape.copy()
    core.eval_ghost()
    tap(pg.K_SPACE)
    settle()
    return core.recorder.placements[-1] if core.recorder.placements else None


# A quad: an I stood on end into a four-deep shaft. The stray block above the
# shaft keeps the clear from emptying the board, which would quietly turn every
# quad here into a perfect clear and add ten to the numbers under test.
new_game()
fill([(x, y) for y in range(18, 22) for x in range(10) if x != 9], colour=2)
fill([(0, 16)], colour=5)
before = user.attack_sent
placed = place(0, 1, 8, 18)
check(
    'a quad on the board sends four',
    placed is not None and placed.lines == 4 and placed.attack == 4,
    'lines {}, attack {}'.format(placed and placed.lines, placed and placed.attack)
)
check('and the running total keeps it', user.attack_sent - before == 4, str(user.attack_sent))

# Back to back: a second quad right after. The stray from the first dropped
# into the rows about to clear, so a fresh one keeps this from being a perfect.
fill([(x, y) for y in range(18, 22) for x in range(10) if x != 9], colour=2)
fill([(0, 16)], colour=5)
placed = place(0, 1, 8, 18)
check(
    'the second quad carries back to back',
    placed is not None and placed.attack == 5 and user.b2b > 1,
    'attack {}, b2b {}'.format(placed and placed.attack, user.b2b)
)

# Combo: two clears in a row, the second one a single extending the combo.
check(
    'the combo bonus reads the combo the clear extended',
    placed.combo == 1, 'combo {}'.format(placed.combo)
)

# A T-spin double, built the classic way: a slot under an overhang, and a
# block off to the side so clearing it does not empty the board.
new_game()
fill([(x, 20) for x in range(10) if x not in (3, 4, 5)], colour=3)
fill([(x, 21) for x in range(10) if x != 4], colour=3)
fill([(3, 19)], colour=3)
fill([(5, 19)], colour=3)
fill([(0, 18)], colour=5)
core.set_shape(2)
core.freeshape.rotate(True)
core.freeshape.rotate(True)
core.freeshape.pos = core.newshape.pos = [4, 20]
core.newshape = core.freeshape.copy()
core.rotated_last = True
user.twist_flag = True
core.eval_ghost()
tap(pg.K_SPACE)
settle()
placed = core.recorder.placements[-1]
check(
    'a spin double sends four through the whole pipeline',
    bool(placed.spin) and placed.lines == 2 and placed.attack == 4,
    'spin {!r}, lines {}, attack {}'.format(placed.spin, placed.lines, placed.attack)
)

# A perfect clear: one flat row finished off with a flat I.
new_game()
fill([(x, 21) for x in range(10) if x not in (3, 4, 5, 6)], colour=5)
placed = place(0, 0, 4, 20)
check(
    'a perfect clear adds its ten',
    placed is not None and placed.perfect and placed.attack == 10,
    'perfect {}, attack {}'.format(placed and placed.perfect, placed and placed.attack)
)

# Downstack: garbage rows dug out are counted for VS.
new_game()
before = user.downstack
fill([(x, 21) for x in range(10) if x not in (3, 4, 5, 6)], colour=7)
placed = place(0, 0, 4, 20)
check(
    'digging out a garbage row counts as downstack',
    user.downstack - before == 1 and placed.lines == 1,
    'downstack {}'.format(user.downstack - before)
)
check(
    'a clear with no garbage in it does not',
    (lambda: (
        fill([(x, 21) for x in range(10) if x not in (3, 4, 5, 6)], colour=3),
        place(0, 0, 4, 20),
        user.downstack - before == 1)[-1])(),
    str(user.downstack - before)
)

# The replay carries it all: totals, APM and VS in the summary.
new_game()
for _ in range(3):
    fill([(x, y) for y in range(18, 22) for x in range(10) if x != 9], colour=2)
    fill([(0, 16)], colour=5)
    place(0, 1, 8, 18)
for column in (1, 4, 7):
    # Spread across the board, since forcing three pieces onto one spot is how
    # a test crashes into the paste assert rather than testing anything.
    place(2, 0, column, 2)
played = core.recorder.finish(user)
check('a game long enough was recorded', played is not None and len(played) == 6)
summary = played.summary()
sent = sum(p.attack or 0 for p in played.placements)
check(
    'the summary sums the attack the placements carry',
    summary['attack'] == sent == user.attack_sent,
    '{} / {} / {}'.format(summary['attack'], sent, user.attack_sent)
)
check('and derives APM from it', summary['apm'] > 0., str(summary['apm']))
check(
    'VS never reads below APS x100',
    summary['vs'] * 0.6 >= summary['apm'] - 1e-9,
    'vs {} apm {}'.format(summary['vs'], summary['apm'])
)
check(
    'the corrected view does not change what was sent',
    played.summary(True)['attack'] == summary['attack'],
    str(played.summary(True)['attack'])
)

# A new game starts the counters over.
user.reset()
check('a new game clears the counters', user.attack_sent == 0 and user.downstack == 0)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
