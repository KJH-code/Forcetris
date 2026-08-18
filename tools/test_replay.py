"""Regression checks for recording, saving and playing back a game.

Three things can go wrong here and only one of them is loud. A crash on the
analysis screen is obvious. A replay that records the wrong thing is not - it
looks like a replay right up until you read it and believe it. And a corrected
replay that quietly changes the *placements* rather than the presses would be
worse than useless, because it would be telling the player their own game went
differently than it did.

So the checks lean hardest on: what is recorded matches what was played, a round
trip through the file changes nothing, and the fix moves no piece.

Run with: python tools/test_replay.py
"""
import os
import sys
import json
import shutil
import tempfile
from argparse import Namespace

os.environ['SDL_VIDEODRIVER'] = 'dummy'
os.environ['SDL_AUDIODRIVER'] = 'dummy'
# Neither the player's settings nor their replays may be touched by a test run,
# and both are read the moment the engine is imported.
os.environ['FORCETRIS_CONFIG'] = os.path.join(tempfile.mkdtemp(), 'settings.json')
os.environ['FORCETRIS_REPLAYS'] = os.path.join(tempfile.mkdtemp(), 'replays')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(tempfile.mkdtemp())

import pygame as pg
import engine.game as G
import engine.replay as rp
import engine.finesse as fin
import engine.userstate as us
import engine.environment as env
from engine.shapes import Shape

pg.key.get_focused = lambda: True

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


# No timer, so every placement is the player's own and gets judged.
tetris = G.init(Namespace(debug=False, forced_delay=0., volume=0., sfx_volume=0.))
user = tetris.user
core = tetris.game
analysis = tetris.analysis_menu
replays = tetris.replay_menu
viewer = tetris.replay_viewer

I, O, T, S, Z, J, L = range(7)


def tap(key, times=1):
    for _ in range(times):
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
        core.run()
        pg.event.clear()
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
    menu.reset()
    for _ in range(limit):
        if menu.selected.action == action:
            return True
        press_menu(menu, pg.K_DOWN)
    return False


def new_game():
    user.state = 'game'
    user.gametype = 'free'
    user.finesse = us.FINESSE_COUNT
    user.reset()
    core.set_data()
    for _ in range(40):
        core.run()
        if core.entry_flag:
            break


def settle():
    """Run frames until the clearer has finished and the placement is recorded."""
    for _ in range(120):
        core.run()
        if core.recorder.pending is None and core.entry_flag:
            return True
    return False


def drop(form, presses):
    """Place one piece with a given set of presses, and return its record."""
    core.set_shape(form)
    core.eval_ghost()
    core.finesse_inputs = 0
    core.input_log = []
    before = len(core.recorder.placements)
    for key in presses:
        tap(key)
    tap(pg.K_SPACE)
    settle()
    if len(core.recorder.placements) == before:
        return None
    return core.recorder.placements[-1]


# --- What goes into the record. ---------------------------------------------
new_game()
place = drop(T, [pg.K_LEFT] * 3)
check('a placement is recorded at all', place is not None)
check(
    'it records the piece that went down',
    place.form == T, 'IOTSZJL'[place.form]
)
check(
    'it records the presses in the order they were made',
    place.presses == ['left', 'left', 'left'], str(place.presses)
)
check(
    'it records the verdict, not just the presses',
    place.judged and place.best == 1 and place.wasted == 2 and place.fault,
    'judged {} best {} wasted {}'.format(place.judged, place.best, place.wasted)
)
check(
    'it records where the piece ended up',
    place.x == 1, 'column {}'.format(place.x)
)

place = drop(T, [pg.K_z])
check(
    'a rotation is recorded by name',
    place.presses == ['ccw'], str(place.presses)
)
check('a clean placement is recorded as clean', not place.fault, str(place.wasted))

# The count and the log are two views of the same thing and must not drift.
new_game()
core.set_shape(T)
core.finesse_inputs = 0
core.input_log = []
tap(pg.K_LEFT, 2)
tap(pg.K_x)
check(
    'the press count and the press log agree',
    core.finesse_inputs == len(core.input_log) == 3,
    '{} against {}'.format(core.finesse_inputs, len(core.input_log))
)

# --- The board snapshot. ----------------------------------------------------
new_game()
place = drop(O, [])
rows = rp.padded(place.rows)
check(
    'the snapshot is the height of the matrix',
    len(rows) == rp.HEIGHT, '{} rows'.format(len(rows))
)
check(
    'the matrix really is that tall',
    len(core.grid.cells) - 1 == rp.HEIGHT,
    '{} against {}'.format(len(core.grid.cells) - 1, rp.HEIGHT)
)
check(
    'the snapshot holds exactly the piece that went down',
    sum(row.count(str(O)) for row in rows) == 4,
    str([r for r in rows if r.strip('.')])
)
check(
    'empty rows above the stack are not stored',
    len(place.rows) < rp.HEIGHT, '{} stored'.format(len(place.rows))
)
check(
    'padding puts them back',
    rp.padded(place.rows)[-len(place.rows):] == place.rows
)

# --- A whole game, then the file. -------------------------------------------
new_game()
for _ in range(8):
    drop(T, [pg.K_LEFT] * 3)
for _ in range(3):
    drop(T, [])
played = core.recorder.finish(user)
check(
    'the recording covers every placement',
    played is not None and len(played) == 11, str(None if played is None else len(played))
)

summary = played.summary()
fixed = played.summary(fixed=True)
check(
    'the summary counts the faults',
    (summary['faults'], summary['judged']) == (8, 11),
    '{} of {}'.format(summary['faults'], summary['judged'])
)
check(
    'and the presses they cost',
    (summary['presses'], summary['wasted']) == (24, 16),
    '{} presses, {} wasted'.format(summary['presses'], summary['wasted'])
)
check(
    'the corrected count is what the routes add up to',
    fixed['presses'] == 8 and fixed['faults'] == 0 and fixed['rate'] == 1.,
    '{} presses, {} faults'.format(fixed['presses'], fixed['faults'])
)
check(
    'correcting does not invent placements',
    fixed['placements'] == summary['placements'] == 11,
    '{} against {}'.format(fixed['placements'], summary['placements'])
)

path = rp.save(played)
check('the replay lands on disk', path and os.path.exists(path), str(path))
check(
    'the file name is one Windows will accept',
    path and not set(os.path.basename(path)) & set(':*?"<>|'),
    os.path.basename(path or '')
)

read = rp.load(path)
check('it reads back', read is not None)
check(
    'a round trip through the file changes nothing',
    read is not None and [p.to_dict() for p in read.placements] == [p.to_dict() for p in played.placements],
    'reloaded {} of {}'.format(0 if read is None else len(read), len(played))
)
check(
    'and the summary comes out the same',
    read is not None and read.summary() == summary
)

# --- The corrected view moves nothing. --------------------------------------
# This is the check that matters most. A corrected replay that changed where a
# piece went would be lying to the player about their own game.
# Correcting a run may change what it cost and nothing else. Anything outside
# this list differing between the two summaries means the fix moved a piece,
# cleared a line, or scored a point that the player did not.
COSTS = {'faults', 'wasted', 'presses', 'rate', 'ppp'}
differs = {
    key for key in summary
    if summary[key] != fixed[key]
}
check(
    'correcting changes what the run cost and nothing else',
    differs <= COSTS, str(sorted(differs - COSTS))
)
check(
    'and it does change the cost, so the check above means something',
    differs, str(sorted(differs))
)
# Every route is offered for the very placement it reaches, and is as short as
# the verdict said it would be.
astray = [
    i for i, p in enumerate(played.placements)
    if p.judged and (
        len(p.route()) != p.best
        or fin.placement(p.form, p.state, p.x) not in fin.table(p.form))
]
check(
    'every route offered is as short as the verdict claimed',
    not astray, str(astray)
)
snapshots = [tuple(p.rows) for p in played.placements]
check(
    'the boards are the same boards with the fix on',
    snapshots == [tuple(p.rows) for p in read.placements]
)

# --- A file the game did not write. -----------------------------------------
junk = os.path.join(rp.FOLDER, 'zzz-broken.json')
with open(junk, 'w') as out:
    out.write('{not json')
check('a corrupt replay reads as nothing', rp.load(junk) is None)
check('and is left out of the listing', all(r.path != junk for r in rp.listing()))

wrong = os.path.join(rp.FOLDER, 'zzz-future.json')
with open(wrong, 'w') as out:
    json.dump({'format': rp.FORMAT + 99, 'meta': {}, 'placements': []}, out)
check('a replay from a later format is declined', rp.load(wrong) is None)
os.remove(junk)
os.remove(wrong)

# A run too short to be worth anything is not written at all.
new_game()
for _ in range(2):
    drop(T, [])
check(
    'a run of two pieces is not saved',
    core.recorder.finish(user) is None, str(len(core.recorder.placements))
)

# --- Only so many are kept. -------------------------------------------------
# Cleared first: replays saved earlier in this run carry today's date and would
# sort newer than the dated spares below, which is true but not what is under
# test here.
shutil.rmtree(rp.FOLDER, ignore_errors=True)
before = len(rp.listing())
for i in range(rp.KEEP + 4):
    spare = rp.Replay(
        {'played': '2020-01-01T00:{:02d}:00'.format(i), 'gametype': 'free', 'score': i},
        list(played.placements))
    rp.save(spare)
check(
    'old replays are pruned rather than piling up',
    len(rp.listing()) <= rp.KEEP,
    '{} kept, was {}'.format(len(rp.listing()), before)
)
check(
    'and the newest are the ones kept',
    rp.listing()[0].meta.get('score') == rp.KEEP + 3,
    str(rp.listing()[0].meta.get('score'))
)

# --- The screens. -----------------------------------------------------------
analysis.show(played)
check('the analysis screen takes the replay', analysis.stats is not None)
check(
    'it shows both counts, the real one and the corrected one',
    any('24' in value for _, value in analysis.rows())
    and any('8  (' in value for _, value in analysis.rows()),
    str([v for _, v in analysis.rows() if '/piece' in v])
)
user.state = 'analysis_menu'
analysis.return_state = 'loss_menu'
analysis.run()
check('it draws without falling over', True)

analysis.show(None)
check('a game too short leaves it with nothing to show', analysis.stats is None)
analysis.run()
check('and it still draws', True)

# Watch, from the analysis screen.
analysis.show(played)
user.state = 'analysis_menu'
goto_row(analysis, 'watch')
press_menu(analysis, pg.K_RETURN)
check('Watch Replay opens the viewer', user.state == 'replay_viewer', user.state)
check('with the replay loaded', viewer.replay is played)

# Stepping.
viewer.index = 0
press_menu(viewer, pg.K_RIGHT)
check('right steps forward', viewer.index == 1, str(viewer.index))
press_menu(viewer, pg.K_LEFT)
check('left steps back', viewer.index == 0, str(viewer.index))
press_menu(viewer, pg.K_LEFT)
check('and stops at the beginning', viewer.index == 0, str(viewer.index))
viewer.index = len(played) - 1
press_menu(viewer, pg.K_RIGHT)
check('and at the end', viewer.index == len(played) - 1, str(viewer.index))

# Playing.
viewer.index = 0
viewer.playing = True
viewer.speed = len(viewer.speeds) - 1
for _ in range(60):
    viewer.run()
check('playing advances on its own', viewer.index > 0, str(viewer.index))
viewer.index = len(played) - 1
viewer.playing = True
for _ in range(20):
    viewer.run()
check('and stops itself at the end', not viewer.playing and viewer.index == len(played) - 1)

# The fix.
viewer.index = 0
viewer.fixed = False
viewer.run()
press_menu(viewer, pg.K_f)
check('F turns the fix on', viewer.fixed)
check(
    'the fix does not touch the recording',
    viewer.replay.placements[0].presses == ['left', 'left', 'left'],
    str(viewer.replay.placements[0].presses)
)
check(
    'the run reads as clean with it on',
    viewer.replay.summary(True)['rate'] == 1.
    and viewer.replay.summary(False)['rate'] < 1.,
    '{} against {}'.format(
        viewer.replay.summary(True)['rate'], viewer.replay.summary(False)['rate'])
)
viewer.run()
press_menu(viewer, pg.K_f)
check('and F turns it off again', not viewer.fixed)

press_menu(viewer, pg.K_ESCAPE)
check('the viewer backs out where it came from', user.state == 'analysis_menu', user.state)

# The list, from the main menu.
user.state = 'main_menu'
tetris.main_menu.reset()
found = goto_row(tetris.main_menu, 'replays')
check('the main menu has a Replays row', found, tetris.main_menu.selected.action)
press_menu(tetris.main_menu, pg.K_RETURN)
check('it opens the list', user.state == 'replay_menu', user.state)
check('which found the saved replays', len(replays.replays) > 0, str(len(replays.replays)))
press_menu(replays, pg.K_RETURN)
check('and opens one into the viewer', user.state == 'replay_viewer', user.state)
press_menu(viewer, pg.K_ESCAPE)
check('backing out of that one returns to the list', user.state == 'replay_menu', user.state)
press_menu(replays, pg.K_ESCAPE)
check('and the list backs out to the main menu', user.state == 'main_menu', user.state)

# Scrolling, which only matters because there are more replays than rows.
user.state = 'replay_menu'
replays.refresh()
check(
    'the list shows a window onto the replays',
    len(replays.selections[0]) <= replays.rows_shown, str(len(replays.selections[0]))
)
if len(replays.replays) > len(replays.selections[0]):
    press_menu(replays, pg.K_DOWN, len(replays.selections[0]) + 2)
    check('walking off the bottom scrolls it', replays.top > 0, str(replays.top))
    seen = replays.chosen()
    check('and the row under the cursor is a real replay', seen is not None)

# --- Nothing laid out past its panel. ---------------------------------------
for name in ('analysis_menu', 'replay_menu', 'replay_viewer', 'main_menu', 'loss_menu'):
    menu = getattr(tetris, name)
    rows = [option for column in menu.selections for option in column]
    reserved = 24 if hasattr(type(menu), 'display_hint') else 0
    overflow = [
        o.action for o in rows
        if not menu.rect.contains(o.rect) or o.rect.bottom > menu.rect.bottom - reserved
    ]
    check(
        '{} lays its rows out inside the panel'.format(name),
        not overflow, str(overflow)
    )
    check(
        '{} fits on the screen'.format(name),
        env.screct.contains(menu.rect), str(menu.rect)
    )

shutil.rmtree(rp.FOLDER, ignore_errors=True)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
