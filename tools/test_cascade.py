"""The cascade clear styles: groups fall, chains count, and nothing hangs.

The sticky and linked clear styles shipped broken in ways the naive default
hid: the fills walked off the board, the fills were seeded from positions a
row splice had already invalidated, sticky groups were declared stuck by
fiat so nothing ever fell, and a shape blocked by another floating shape
was re-queued unmoved forever. These checks pin the repaired behaviour -
the behaviour the settings screen has claimed all along - before the C++
side is graded against it.

Run with: python tools/test_cascade.py
"""
import os
import sys
import tempfile

os.environ.setdefault('SDL_VIDEODRIVER', 'dummy')
os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
os.environ.setdefault('FORCETRIS_CONFIG', os.path.join(tempfile.mkdtemp(), 'settings.json'))
os.environ.setdefault('FORCETRIS_REPLAYS', os.path.join(tempfile.mkdtemp(), 'replays'))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

from argparse import Namespace

import pygame as pg
import engine.game as G
import engine.replay as rp
import engine.userstate as us
from engine.shapes import Block

pg.key.get_focused = lambda: True

failures = []


def check (name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name,
                           ' -- ' + detail if detail and not ok else ''))
    if not ok:
        failures.append(name)


tetris = G.init(Namespace(debug=False, forced_delay=0., volume=0., sfx_volume=0.))
user, core = tetris.user, tetris.game
user.state = 'game'
user.gametype = 'free'


def fresh (cleartype):
    user.cleartype = cleartype
    user.reset()
    core.set_data()
    core.grid.set_cells()


def put (x, y, color=3, links=None, fallen=True):
    block = Block([x, y], color, list(links or []), fallen=fallen)
    core.grid.cells[y][x] = block
    return block


def drain (limit=300):
    """Drive the generator to the end, or report the old hang as None."""
    clearer = core.grid.clear_lines()
    yields = 0
    while True:
        alive = next(clearer)
        yields += 1
        if not alive:
            return yields
        if yields > limit:
            return None


def board ():
    return rp.board_to_rows(core.grid)


# --- Sticky groups actually fall. -------------------------------------------
fresh(us.CLEAR_STICKY)
# The bottom row lacks only column nine; a lone block floats high above it,
# and a vertical stub stands on the row about to go.
for x in range(9):
    put(x, 21)
put(4, 19)
for y in (18, 19, 20):
    put(9, y, color=0)
put(9, 21, color=0)
steps = drain()
check('a sticky clear finishes', steps is not None, 'never finished')
check(
    'and the leftovers fall to the floor',
    board() == ['.........0', '.........0', '....3....0'],
    str(board())
)

# The same shove under the old rule left everything hanging where it was;
# the row scan alone says which world we are in.
check('sticky cleared the one full row', user.line_list == [1], str(user.line_list))

# --- Linked groups fall piece by piece, and only where their links say. -----
fresh(us.CLEAR_LINKED)
for x in range(9):
    put(x, 21)
# Two linkless singles side by side; the right one stands over a garbage
# block, which never floats. Linked clearing drops them one by one: the left
# single falls free, the right one stays parked on the garbage.
put(0, 19)
put(1, 19)
put(1, 20, color=7)
for y in (18, 19, 20):
    put(9, y, color=0)
put(9, 21, color=0)
steps = drain()
check('a linked clear finishes', steps is not None, 'never finished')
rows = board()
check(
    'the free single falls, the propped one stays',
    rows == ['.3.......0', '.7.......0', '3........0'],
    str(rows)
)

# Under sticky the two singles are one side-connected group, and the garbage
# block under half of it props the whole thing.
fresh(us.CLEAR_STICKY)
for x in range(9):
    put(x, 21)
put(0, 19)
put(1, 19)
put(1, 20, color=7)
for y in (18, 19, 20):
    put(9, y, color=0)
put(9, 21, color=0)
steps = drain()
check('the sticky version finishes too', steps is not None, 'never finished')
check(
    'and the bridged pair is propped as one group',
    board() == ['33.......0', '.7.......0', '.........0'],
    str(board())
)

# --- A chain: what falls can clear again, and the score knows. --------------
fresh(us.CLEAR_LINKED)
for x in range(9):
    put(x, 21)
for x in range(8):
    put(x, 20)
put(8, 19)
for y in (18, 19, 20):
    put(9, y, color=0)
put(9, 21, color=0)
before = user.score
steps = drain()
check('the chain finishes', steps is not None, 'never finished')
check(
    'two chains of one line each',
    user.line_list == [1, 1],
    str(user.line_list)
)
# 500 + 500, times cascade_factor for the second chain, to the nearest fifty.
check(
    'the cascade multiplier pays out',
    user.score - before == 1300,
    str(user.score - before)
)
check('only the stub that fed both chains remains',
      board() == ['.........0', '.........0'], str(board()))

# --- A garbage row cleared under cascade: split out, counted, and no hang. --
fresh(us.CLEAR_LINKED)
garbage = []
for x in range(10):
    links = []
    if x != 0:
        links.append(3)
    if x != 9:
        links.append(1)
    put(x, 21, color=7, links=links)
for x in range(9):
    put(x, 20)
put(9, 20, color=0)
put(9, 19, color=0)
put(4, 18)
before_down = user.downstack
steps = drain()
check('a garbage row under cascade finishes', steps is not None, 'never finished')
check('and counts as downstack', user.downstack - before_down == 1,
      str(user.downstack - before_down))
check(
    'the rows above it settle where the splice left them',
    board() == ['....3....0'],
    str(board())
)
check('two chains: the piece rows and the garbage row apart',
      user.line_list == [1, 1], str(user.line_list))

# --- A fall stopped by settled ground mid-flight settles where it stops. ----
fresh(us.CLEAR_LINKED)
for x in range(9):
    put(x, 21)
# A single whose down link points at nothing - the shape a stale link leaves
# behind - falling onto a garbage block. The dangling link exempts it from
# the resting test (a linked block is supposed to be resting on its own
# mate), so it is the movement collision, not the pre-test, that has to
# stop it: the fill refuses the settled garbage, the block falls a row, and
# the step onto the garbage pastes it back settled. Both halves are pinned
# here - the final resting place, and the fallen flag only the collision
# branch raises.
put(5, 17, links=[2])
put(5, 19, color=7)
for y in (18, 19, 20):
    put(9, y, color=0)
put(9, 21, color=0)
steps = drain()
check('a fall onto garbage finishes', steps is not None, 'never finished')
check(
    'and stops on the garbage, one row short',
    board() == ['.....3....', '.....7...0', '.........0', '.........0'],
    str(board())
)
check(
    'settled by the collision branch, not parked by the resting test',
    core.grid.cells[18][5] is not None and core.grid.cells[18][5].fallen,
    'the landing was pasted back floating'
)

# --- Two floating shapes never block each other: they fall in lockstep. -----
fresh(us.CLEAR_LINKED)
for x in range(9):
    put(x, 21)
# A two-tall linked domino with a single sitting on its head. The single is
# never actually blocked: the fills cut the domino out of the grid before
# the single's turn in the same bottom-up scan, so both fall a row per
# yield in lockstep and land together. This is why the movement loop's
# blocked-by-floating branch is a backstop rather than a graded path: a
# sticky fill absorbs any side-adjacent floater outright, and a linked
# fill either follows a down link into the cell below - one group again -
# or, with no down link, the resting pre-test parks the shape before it
# moves. A floating blocker would need a state no fill hands the loop; the
# branch stays because termination must not hinge on that argument.
put(0, 19, links=[2])
put(0, 20, links=[0])
put(0, 18)
for y in (18, 19, 20):
    put(9, y, color=0)
put(9, 21, color=0)
steps = drain()
check('the follower finishes', steps is not None, 'never finished')
check(
    'stacked where they started, one row down',
    board() == ['3........0', '3........0', '3........0'],
    str(board())
)

print()
if failures:
    print('{} check(s) failed.'.format(len(failures)))
    raise SystemExit(1)
print('All checks passed.')
