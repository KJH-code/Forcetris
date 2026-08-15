"""Regression checks for the handling settings: DAS, ARR, DCD and SDF.

These are measured by stepping frames and watching where the piece actually is,
rather than by reading back the numbers that were just written. A handling
setting that stores correctly and moves nothing is worth nothing.

Run with: python tools/test_handling.py
"""
import os
import sys
import json
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

pg.key.get_focused = lambda: True
ctl.CONFIG = os.path.join(tempfile.mkdtemp(), 'controls.json')

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


# A delay long enough that the forced drop never interrupts a measurement.
tetris = G.init(Namespace(debug=False, forced_delay=600., volume=0., sfx_volume=0.))
user = tetris.user
core = tetris.game
handling = tetris.handling_menu


def fresh_piece(column=None):
    """Start a game and step until a piece is in play, returning its column.

    Which piece the bag hands out decides how much room it has before the wall,
    so a measurement that needs several cells of travel says where to put it.
    """
    user.state = 'game'
    user.gametype = 'free'
    user.reset()
    core.set_data()
    for _ in range(40):
        core.run()
        if core.entry_flag:
            break
    if column is not None:
        was = core.freeshape.pos[0]
        core.freeshape.pos[0] = core.newshape.pos[0] = column
        if core.check_collision(core.freeshape):
            core.freeshape.pos[0] = core.newshape.pos[0] = was
        core.eval_ghost()
    return core.freeshape.pos[0]


def hold(direction, frames):
    """Press and keep holding a direction, returning the column after each frame."""
    pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_LEFT if direction == 'l' else pg.K_RIGHT))
    columns = []
    for _ in range(frames):
        core.run()
        columns.append(core.freeshape.pos[0])
        pg.event.clear()
    return columns


def release():
    pg.event.clear()
    for key in (pg.K_LEFT, pg.K_RIGHT, pg.K_DOWN):
        pg.event.post(pg.event.Event(pg.KEYUP, key=key))
        core.run()
    pg.event.clear()


def press_menu(menu, key, times=1):
    for _ in range(times):
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
        menu.run()
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYUP, key=key))
        menu.run()


def goto_row(menu, action, limit=30):
    """Move the cursor onto a named row.

    Counting Down presses breaks every time a row is inserted above the one a
    check cares about, which is a property of the test rather than the menu.
    """
    menu.reset()
    for _ in range(limit):
        if menu.selected.action == action:
            return True
        press_menu(menu, pg.K_DOWN)
    return False


# --- DAS: how long a held key waits before it repeats. ----------------------
ctl.reset(user)
ctl.set_handling(user, 'das', 200)   # 10 frames
ctl.set_handling(user, 'arr', 20)    # 1 frame
start = fresh_piece()
columns = hold('l', 16)
# The first frame is the initial press. Auto-shift may not start before DAS.
first_repeat = next((i for i, x in enumerate(columns) if x < columns[0]), None)
check(
    'DAS 200ms holds the repeat for ten frames',
    columns[0] == start - 1 and first_repeat is not None and 9 <= first_repeat <= 11,
    'moved on press, then repeated at frame {}'.format(first_repeat)
)
release()

ctl.set_handling(user, 'das', 0)
start = fresh_piece()
columns = hold('l', 4)
check(
    'DAS 0 repeats straight away',
    columns[1] < columns[0], 'columns {}'.format(columns)
)
release()

# --- ARR: how fast it repeats once DAS is up. -------------------------------
# Measured as the gap between steps, not the number of them: the piece runs out
# of board long before it runs out of frames.
ctl.set_handling(user, 'das', 0)
ctl.set_handling(user, 'arr', 40)   # 2 frames per cell
fresh_piece(1)
columns = hold('r', 12)
moves = [i for i in range(1, len(columns)) if columns[i] != columns[i - 1]]
gaps = [b - a for a, b in zip(moves, moves[1:])]
check(
    'ARR 40ms moves one cell every other frame',
    len(gaps) >= 2 and all(gap == 2 for gap in gaps),
    'steps at frames {}, gaps {} (the wall ends the run)'.format(moves, gaps)
)
release()

ctl.set_handling(user, 'arr', 0)
fresh_piece(1)
start = core.freeshape.pos[0]
columns = hold('r', 3)
check(
    'ARR 0 reaches the wall in a single frame',
    columns[0] > start + 1 and columns[0] == columns[1],
    'column {} to {} on the first frame, then still {}'.format(start, columns[0], columns[1])
)
check(
    'the wall stops it rather than the piece escaping',
    not core.check_collision(core.freeshape), 'the piece is inside the board at {}'.format(columns[-1])
)
release()

# --- DCD: the charge a new or rotated piece inherits. -----------------------
# Counted in cells travelled after a rotation, which is what the player feels.
# ARR is slow enough here that the piece still has board left to cross when the
# measurement window opens.
def cells_after_rotation (dcd):
    ctl.set_handling(user, 'dcd', dcd)
    fresh_piece(1)
    pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_RIGHT))
    core.run()                # initial press, and auto-shift charges at DAS 0
    pg.event.clear()
    charged = core.das_charged
    before = core.freeshape.pos[0]
    pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_x))   # rotate
    core.run()
    pg.event.clear()
    for _ in range(8):
        core.run()
    moved = core.freeshape.pos[0] - before
    release()
    return charged, moved

ctl.reset(user)
ctl.set_handling(user, 'das', 0)
ctl.set_handling(user, 'arr', 60)   # 3 frames per cell
charged, without = cells_after_rotation(0)
check('auto-shift reports itself charged', charged)
check(
    'DCD 0 lets the piece keep sliding through a rotation',
    without >= 2, '{} cells in the eight frames after rotating'.format(without)
)
charged, with_dcd = cells_after_rotation(120)   # 6 frames of silence
check(
    'DCD pauses auto-shift after a rotation',
    with_dcd < without, '{} cells with DCD against {} without'.format(with_dcd, without)
)
ctl.set_handling(user, 'dcd', 0)

# --- SDF: soft drop speed, and the slam at the top of the range. ------------
ctl.reset(user)
ctl.set_handling(user, 'sdf', 5)
fresh_piece()
top = core.freeshape.pos[1]
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_DOWN))
core.run()
pg.event.clear()
core.run()
slow = core.freeshape.pos[1] - top
release()

ctl.set_handling(user, 'sdf', ctl.SDF_INSTANT)
fresh_piece()
top = core.freeshape.pos[1]
ghost = core.ghostshape.pos[1]
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_DOWN))
core.run()
core.run()
check(
    'SDF at maximum drops the piece to the floor at once',
    core.freeshape.pos[1] == ghost and ghost > top + 2,
    'row {} of a possible {}, slow SDF managed {} rows'.format(core.freeshape.pos[1], ghost, slow)
)
check('the slam does not lock the piece', core.entry_flag, 'the piece is still in play')
release()

# --- ARE: the pause between one piece locking and the next appearing. -------
def frames_between_pieces():
    """Lock a piece and count frames until the next one is in play."""
    fresh_piece()
    core.hard_drop()
    for waited in range(1, 200):
        core.run()
        if core.entry_flag:
            return waited
    return None

ctl.reset(user)
check('ARE defaults to nothing', user.are == 0, '{}ms'.format(user.are))
instant = frames_between_pieces()
check(
    'with ARE at zero the next piece is there almost at once',
    instant is not None and instant <= 3, '{} frames'.format(instant)
)
ctl.set_handling(user, 'are', 400)
slow = frames_between_pieces()
check(
    'ARE 400ms holds the board for twenty frames',
    slow is not None and 19 <= slow <= 23, '{} frames against {} with none'.format(slow, instant)
)
check('the pause is what ARE says it is', core.entry_delay == 20, '{} frames'.format(core.entry_delay))
ctl.set_handling(user, 'are', 0)

# --- Arcade's difficulty ramp must not overwrite the player's handling. -----
ctl.reset(user)
ctl.set_handling(user, 'das', 300)
ctl.set_handling(user, 'arr', 100)
user.state = 'game'
user.gametype = 'arcade'
user.reset()
core.set_data()
user.lines_cleared = 900   # deep into the ramp
for _ in range(30):
    core.run()
check(
    'the arcade ramp leaves DAS and ARR alone',
    core.shift_delay == 15 and core.shift_fdelay == 5,
    'DAS {} frames, ARR {} frames at level {}'.format(core.shift_delay, core.shift_fdelay, user.level)
)
check(
    'the arcade ramp still speeds gravity up',
    core.fall_delay < 45, 'fall delay {}'.format(core.fall_delay)
)
check(
    'the arcade ramp leaves the spawn pause alone',
    core.entry_delay == 0, '{} frames at level {}'.format(core.entry_delay, user.level)
)
user.gametype = 'free'

# --- The screen, and the file it writes. ------------------------------------
ctl.reset(user)
user.state = 'settings_menu'
settings = tetris.settings_menu
goto_row(settings, 'handling')
press_menu(settings, pg.K_RETURN)
check(
    'settings opens the handling screen',
    user.state == 'handling_menu' and handling.return_state == 'settings_menu',
    user.state
)

handling.reset()
before = user.das
press_menu(handling, pg.K_RIGHT)
check(
    'right raises DAS by one step',
    handling.selected.action == 'das' and user.das == before + 20,
    '{}ms -> {}ms'.format(before, user.das)
)
press_menu(handling, pg.K_LEFT, 60)
check('DAS bottoms out at zero', user.das == 0, '{}ms'.format(user.das))
press_menu(handling, pg.K_RIGHT, 60)
check('DAS tops out at its maximum', user.das == 500, '{}ms'.format(user.das))

goto_row(handling, 'arr')
press_menu(handling, pg.K_LEFT, 40)
check(
    'ARR reads Instant at zero',
    user.arr == 0 and handling.label('arr').endswith('Instant'), handling.label('arr')
)
goto_row(handling, 'sdf')
press_menu(handling, pg.K_RIGHT, 60)
check(
    'SDF reads Instant at its maximum',
    user.sdf == ctl.SDF_INSTANT and handling.label('sdf').endswith('Instant'), handling.label('sdf')
)

saved = json.load(open(ctl.CONFIG))
check(
    'handling is saved next to the keybinds',
    saved['handling']['sdf'] == ctl.SDF_INSTANT and 'keys' in saved,
    str(saved['handling'])
)
user.das = user.arr = user.dcd = user.sdf = 0
ctl.load(user)
check('handling is read back', user.sdf == ctl.SDF_INSTANT and user.arr == 0, 'sdf {}'.format(user.sdf))

goto_row(handling, 'reset')
press_menu(handling, pg.K_RETURN)
check(
    'reset restores the defaults',
    user.sdf == ctl.HANDLING_DEFAULTS['sdf'] and user.das == ctl.HANDLING_DEFAULTS['das'],
    'DAS {}ms SDF {}x'.format(user.das, user.sdf)
)
goto_row(handling, 'back')
press_menu(handling, pg.K_RETURN)
check('back returns to settings', user.state == 'settings_menu', user.state)

ctl.reset(user)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
