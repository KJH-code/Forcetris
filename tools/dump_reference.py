"""Write out what the Python engine does, for the C++ core to be checked against.

The C++ port is only worth having if it agrees with the implementation that is
already tested, so rather than writing a second set of tests by hand this dumps
the answers the Python side gives and lets the C++ side be graded against them.
Anywhere the two differ, one of them is wrong and the difference is printed.

The format is deliberately dull - one record per line, whitespace separated - so
the reader needs no JSON library and the diff of a regression is readable.

Run with: python tools/dump_reference.py [path]
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

import random
from argparse import Namespace

import pygame as pg
import engine.game as G
import engine.finesse as fin
import engine.replay as rp
from engine.shapes import Block

pg.key.get_focused = lambda: True

# Boards the rotation sweep runs against. Named so a mismatch says which.
#
#   flat      nothing at all, where every rotation should simply work
#   wall      a garbage stack, which is what forces the kicks to do their job
#   well      a narrow shaft, the classic case for the I piece kicks
#   comb      alternating full-height columns, so every gap is one wide
#   messN     seeded rubble at a few densities
#
# The last two are there because the hand-built boards turned out to exercise
# only the first kick candidate of each table: a wrong entry further down a
# table went undetected until there were boards awkward enough to reach it.
# Rubble is better at that than anything designed on purpose.
BOARDS = ('flat', 'wall', 'well', 'comb') + tuple(
    'mess{}'.format(i) for i in range(30))


def build_board (core, which):
    core.grid.set_cells()
    if which == 'wall':
        random.seed(20260818)
        for _ in range(8):
            core.grid.add_garbage()
    elif which == 'well':
        for y in range(12, 22):
            for x in range(10):
                if x == 4:
                    continue
                core.grid.cells[y][x] = Block([x, y], 7, fallen=True)
    elif which == 'comb':
        for y in range(8, 22):
            for x in range(0, 10, 2):
                core.grid.cells[y][x] = Block([x, y], 7, fallen=True)
    elif which.startswith('mess'):
        index = int(which[4:])
        # Seeded, so a failure can be reproduced exactly, and at a spread of
        # densities because a nearly full board and a sparse one wedge a piece
        # in different ways.
        random.seed(770000 + index)
        density = 0.15 + 0.025 * (index % 24)
        for y in range(4, 22):
            for x in range(10):
                if random.random() < density:
                    core.grid.cells[y][x] = Block([x, y], 7, fallen=True)
    core.grid.update()


def board_rows (core):
    return rp.board_to_rows(core.grid)


# Which kick candidate got used, counted per table entry. A sweep that runs
# millions of rotations still proves nothing about a table entry it never
# reaches, and the entries deep in a table are exactly the ones a port gets
# wrong, so the sweep reports its own coverage rather than being trusted.
USED = {}
TABLES = {}


def watch_kicks (core):
    """Wrap test_kicks so every candidate it settles on is counted."""
    original = core.test_kicks

    def wrapped (poslist):
        before = list(core.freeshape.pos)
        key = (core.freeshape.state, core.newshape.state,
               'I' if core.freeshape.form == 0 else 'other')
        offsets = [tuple(pos) for pos in poslist]
        TABLES[key] = offsets
        original(poslist)
        moved = (core.newshape.pos[0] - before[0], core.newshape.pos[1] - before[1])
        if core.user.twist_flag and moved in offsets:
            index = offsets.index(moved)
            USED[key + (index,)] = USED.get(key + (index,), 0) + 1
        # Every entry is registered so the report can name the ones never used.
        for i in range(len(offsets)):
            USED.setdefault(key + (i,), 0)

    core.test_kicks = wrapped


def unreachable (key, index):
    """True if this candidate can never be the one that fits.

    A kick that lifts the piece may only be tried once per piece, so the first
    upward candidate in a table spends the allowance and every upward candidate
    after it is skipped rather than tested. Those entries are decoration: they
    cannot be reached however awkward the board is, so a sweep that never
    reaches them is complete rather than lacking.
    """
    offsets = TABLES.get(key, ())
    if index >= len(offsets) or offsets[index][1] >= 0:
        return False
    return any(pos[1] < 0 for pos in offsets[:index])


def main (path):
    tetris = G.init(Namespace(debug=False, forced_delay=0., volume=0., sfx_volume=0.))
    user, core = tetris.user, tetris.game
    watch_kicks(core)
    user.state = 'game'
    user.gametype = 'free'
    user.reset()
    core.set_data()
    for _ in range(40):
        core.run()
        if core.entry_flag:
            break

    out = []
    out.append('# forcetris reference 1')
    out.append('geometry {} {} {} {}'.format(
        fin.WIDTH, rp.HEIGHT, fin.SPAWN_X, fin.SPAWN_Y))

    # Where each piece's cells sit, in each orientation.
    for form in range(7):
        for state in range(4):
            cells = fin.offsets(form, state)
            out.append('offsets {} {} {}'.format(
                form, state, ' '.join('{} {}'.format(x, y) for x, y in cells)))

    # The finesse tables, placement by placement. Placements are written as the
    # cells they cover so the two sides are compared on the same key.
    for form in range(7):
        table = fin.table(form)
        out.append('finesse_count {} {}'.format(form, len(table)))
        for key in sorted(tuple(sorted(cells)) for cells in table):
            presses = table[frozenset(key)]
            out.append('finesse {} {} {} {}'.format(
                form,
                ','.join('{}:{}'.format(x, y) for x, y in key),
                len(presses),
                ' '.join(presses) if presses else '-'))

    # Rotation, against boards that make the kicks work for it.
    for which in BOARDS:
        build_board(core, which)
        rows = board_rows(core)
        out.append('board {} {}'.format(which, len(rows)))
        out.extend('row ' + row for row in rows)
        for kicks in (1, 0):
            user.enablekicks = bool(kicks)
            for form in range(7):
                for state in range(4):
                    for column in range(-1, 11):
                        for depth in (1, 5, 8, 11, 14, 17, 19, 20, 21):
                            core.set_shape(form)
                            for _ in range(state):
                                core.freeshape.rotate(True)
                            core.freeshape.pos[0] = core.newshape.pos[0] = column
                            core.freeshape.pos[1] = core.newshape.pos[1] = depth
                            if core.check_collision(core.freeshape):
                                # Not a position the piece could be in, so there is
                                # nothing to rotate out of.
                                continue
                            for turns in (1, 2, 3):
                                core.set_shape(form)
                                for _ in range(state):
                                    core.freeshape.rotate(True)
                                core.freeshape.pos[0] = core.newshape.pos[0] = column
                                core.freeshape.pos[1] = core.newshape.pos[1] = depth
                                core.newshape = core.freeshape.copy()
                                # set_shape hands back a fresh floor kick, which is
                                # the state a rotation is normally attempted in.
                                core.floor_kick = True
                                for _ in range(turns):
                                    core.newshape.rotate(True)
                                core.wall_kick()
                                landed = core.freeshape
                                out.append('rotate {} {} {} {} {} {} {} {} {} {}'.format(
                                    which, kicks, form, state, column, depth, turns,
                                    landed.state, landed.pos[0], landed.pos[1]))
    user.enablekicks = True

    # Line clearing, and what a board looks like after a piece lands in it.
    for which in BOARDS:
        build_board(core, which)
        for form in range(7):
            for column in range(0, 10):
                build_board(core, which)
                core.set_shape(form)
                core.freeshape.pos[0] = core.newshape.pos[0] = column
                core.freeshape.pos[1] = core.newshape.pos[1] = fin.SPAWN_Y
                if core.check_collision(core.freeshape):
                    continue
                core.eval_ghost()
                landed = core.ghostshape
                out.append('drop {} {} {} {} {}'.format(
                    which, form, column, landed.pos[0], landed.pos[1]))

    with open(path, 'w') as handle:
        handle.write('\n'.join(out) + '\n')
    print('{} records -> {}'.format(len(out), path))

    # Say plainly how much of the kick tables the sweep actually reached, and
    # which of the rest could never have been reached by anything.
    cold = sorted(key for key, count in USED.items() if count == 0)
    dead = [key for key in cold if unreachable(key[:3], key[3])]
    missed = [key for key in cold if key not in dead]
    print('kick table entries reached: {} of {}'.format(
        len(USED) - len(cold), len(USED)))
    print('  {} unreachable by the floor kick rule'.format(len(dead)))
    print('  {} reachable but not reached'.format(len(missed)))
    for key in missed:
        print('    {} -> {} ({}), candidate {} of {}'.format(
            key[0], key[1], key[2], key[3] + 1, len(TABLES.get(key[:3], ()))))
    if missed:
        # A gap here means the sweep is not grading part of the port.
        raise SystemExit('kick coverage is incomplete')


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'reference.txt'))
