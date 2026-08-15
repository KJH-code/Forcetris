"""Regression checks for key rebinding.

The point of the last check here is that rebinding is only real if the game
obeys the new key. Everything else - the menu, the file, the conflict rules -
is scaffolding around that.

Run with: python tools/test_controls.py
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

# Never touch the player's real controls.json.
ctl.CONFIG = os.path.join(tempfile.mkdtemp(), 'controls.json')

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


def press(menu, key, times=1):
    for _ in range(times):
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
        menu.run()
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYUP, key=key))
        menu.run()


tetris = G.init(Namespace(debug=False, forced_delay=60., volume=0., sfx_volume=0.))
user = tetris.user
controls = tetris.controls_menu
ctl.reset(user)

# --- The defaults are all there and readable. -------------------------------
check(
    'every action has a default binding',
    all(user.keys.get(action) for action, label in ctl.ACTIONS),
    ', '.join('{}={}'.format(a, ctl.describe(user, a)) for a, l in ctl.ACTIONS)
)
check('the defaults keep both rotate alternates', len(user.keys['rotate_cw']) == 2, ctl.describe(user, 'rotate_cw'))
check('key names are readable', ctl.describe(user, 'harddrop') == 'Space', ctl.describe(user, 'harddrop'))

# --- Binding, and what it does to a key already in use. ---------------------
ctl.bind(user, 'harddrop', pg.K_w)
check('binding replaces the old key', user.keys['harddrop'] == (pg.K_w,), ctl.describe(user, 'harddrop'))
check('the old key stops matching', not ctl.matches(user, 'harddrop', pg.K_SPACE))
check('the new key matches', ctl.matches(user, 'harddrop', pg.K_w))

# Taking a key that another action holds must not leave it bound to both.
ctl.bind(user, 'hold', pg.K_w)
check(
    'a stolen key leaves only one owner',
    ctl.matches(user, 'hold', pg.K_w) and not ctl.matches(user, 'harddrop', pg.K_w),
    'harddrop is now {}'.format(ctl.describe(user, 'harddrop'))
)
check('an action stripped of its last key reads as Unbound', ctl.describe(user, 'harddrop') == 'Unbound')
check('an unbound action matches nothing', not ctl.matches(user, 'harddrop', pg.K_w))

# Only one half of a pair is taken when the pair shares nothing with the thief.
ctl.reset(user)
ctl.bind(user, 'left', pg.K_z)
check(
    'stealing one key of a pair leaves the other',
    user.keys['rotate_ccw'] == (pg.K_LCTRL,),
    ctl.describe(user, 'rotate_ccw')
)

# --- One action, several keys. ----------------------------------------------
# The case that prompted this: A and the up arrow both turning counter-clockwise.
ctl.reset(user)
ctl.bind(user, 'rotate_ccw', pg.K_a)
ctl.bind(user, 'rotate_ccw', pg.K_UP, replace=False)
check(
    'a second key joins the binding instead of replacing it',
    ctl.matches(user, 'rotate_ccw', pg.K_a) and ctl.matches(user, 'rotate_ccw', pg.K_UP),
    ctl.describe(user, 'rotate_ccw')
)
check(
    'the up arrow is taken off the action that had it',
    not ctl.matches(user, 'rotate_cw', pg.K_UP),
    'rotate_cw is now {}'.format(ctl.describe(user, 'rotate_cw'))
)
check('both keys are listed on the row', ctl.describe(user, 'rotate_ccw') == 'A, Up Arrow', ctl.describe(user, 'rotate_ccw'))

# Adding the same key twice must not double it up.
ctl.bind(user, 'rotate_ccw', pg.K_UP, replace=False)
check('a key already bound is not added twice', len(user.keys['rotate_ccw']) == 2, ctl.describe(user, 'rotate_ccw'))

# The cap exists so a row cannot grow past the width of the panel.
for code in (pg.K_q, pg.K_w, pg.K_e, pg.K_r):
    ctl.bind(user, 'rotate_ccw', code, replace=False)
check(
    'an action stops accepting keys at the cap',
    len(user.keys['rotate_ccw']) == ctl.MAX_KEYS,
    '{} keys: {}'.format(len(user.keys['rotate_ccw']), ctl.describe(user, 'rotate_ccw'))
)

# Removing takes the most recent one, so a mistake is undoable.
was = user.keys['rotate_ccw']
ctl.unbind_last(user, 'rotate_ccw')
check(
    'removing drops the key added last',
    user.keys['rotate_ccw'] == was[:-1], ctl.describe(user, 'rotate_ccw')
)
while ctl.unbind_last(user, 'rotate_ccw'):
    pass
check('removing every key leaves it Unbound', ctl.describe(user, 'rotate_ccw') == 'Unbound')
check('and removing from an empty action is harmless', not ctl.unbind_last(user, 'rotate_ccw'))

# Replacing still replaces, so the old behaviour is intact.
ctl.reset(user)
ctl.bind(user, 'rotate_ccw', pg.K_a)
check('replacing collapses the binding to one key', user.keys['rotate_ccw'] == (pg.K_a,), ctl.describe(user, 'rotate_ccw'))
ctl.reset(user)

# --- Bindings survive a restart. --------------------------------------------
ctl.reset(user)
ctl.bind(user, 'harddrop', pg.K_w)
saved = json.load(open(ctl.CONFIG))
check('bindings are written to disk', saved['keys'].get('harddrop') == [pg.K_w], str(saved.get('keys')))
user.keys = {}
ctl.load(user)
check('bindings are read back', ctl.matches(user, 'harddrop', pg.K_w), ctl.describe(user, 'harddrop'))
check('actions absent from the file fall back to defaults', ctl.matches(user, 'hold', pg.K_LSHIFT))

# A corrupt file must not stop the game from starting.
with open(ctl.CONFIG, 'w') as broken:
    broken.write('{not json at all')
user.keys = {}
ctl.load(user)
check('a corrupt file falls back to the defaults', ctl.matches(user, 'harddrop', pg.K_SPACE))
ctl.reset(user)

# --- The rebinding screen. --------------------------------------------------
user.state = 'settings_menu'
settings = tetris.settings_menu
settings.reset()
press(settings, pg.K_DOWN, 7)
press(settings, pg.K_RETURN)
check(
    'settings opens the controls screen',
    user.state == 'controls_menu' and controls.return_state == 'settings_menu',
    user.state
)

# Selecting a row waits for a key rather than acting immediately.
controls.reset()
press(controls, pg.K_RETURN)
check('selecting a row waits for a key', controls.listening == 'left', str(controls.listening))
check('the row says so', controls.label('left').endswith('Press a key...'), controls.label('left'))

# The next key pressed is the binding, arrow keys and Enter included.
press(controls, pg.K_q)
check(
    'the next key becomes the binding',
    controls.listening is None and ctl.matches(user, 'left', pg.K_q),
    ctl.describe(user, 'left')
)

# Escape backs out of the prompt without binding anything.
controls.reset()
press(controls, pg.K_RETURN)
press(controls, pg.K_ESCAPE)
check(
    'escape cancels without binding',
    controls.listening is None and ctl.matches(user, 'left', pg.K_q),
    ctl.describe(user, 'left')
)
check('cancelling does not leave the screen', user.state == 'controls_menu', user.state)

# Reset is the row above Back, below one row per action.
controls.reset()
press(controls, pg.K_DOWN, len(ctl.ACTIONS))
press(controls, pg.K_RETURN)
check(
    'reset restores every default',
    controls.selected.action == 'reset' and ctl.matches(user, 'left', pg.K_LEFT),
    ctl.describe(user, 'left')
)
controls.reset()
press(controls, pg.K_DOWN, len(ctl.ACTIONS) + 1)
press(controls, pg.K_RETURN)
check('back returns to settings', user.state == 'settings_menu', user.state)

# Right adds a key to the highlighted row, left takes one away.
ctl.reset(user)
controls.reset()
press(controls, pg.K_DOWN, 4)          # rotate_ccw
check('the cursor is on the right row', controls.selected.action == 'rotate_ccw', controls.selected.action)
press(controls, pg.K_RIGHT)
check('right opens an adding prompt', controls.listening == 'rotate_ccw' and controls.listening_adds)
check('the row says it is adding', controls.label('rotate_ccw').endswith('to add...'), controls.label('rotate_ccw'))
press(controls, pg.K_UP)
check(
    'the added key joins the two defaults',
    ctl.describe(user, 'rotate_ccw') == 'Z, Left Ctrl, Up Arrow',
    ctl.describe(user, 'rotate_ccw')
)
press(controls, pg.K_LEFT)
check(
    'left removes the key that was just added',
    ctl.describe(user, 'rotate_ccw') == 'Z, Left Ctrl',
    ctl.describe(user, 'rotate_ccw')
)
# A full row must refuse rather than open a prompt that discards the key.
ctl.reset(user)
for code in (pg.K_q, pg.K_w):
    ctl.bind(user, 'rotate_ccw', code, replace=False)
controls.set_labels()
press(controls, pg.K_RIGHT)
check('a full row does not open an adding prompt', controls.listening is None, str(controls.listening))
ctl.reset(user)
controls.set_labels()

# --- The game obeys the rebound key. ----------------------------------------
# Everything above is scaffolding if this does not hold.
ctl.reset(user)
ctl.bind(user, 'harddrop', pg.K_q)
user.state = 'game'
user.gametype = 'free'
user.reset()
core = tetris.game
core.set_data()
for _ in range(30):
    core.run()
check('a piece is in play', core.entry_flag, 'entry_flag {}'.format(core.entry_flag))

pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_SPACE))
core.run()
check('the unbound default no longer hard drops', core.entry_flag, 'space still dropped the piece')

pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_q))
core.run()
check('the rebound key hard drops', not core.entry_flag, 'Q did not drop the piece')

# Pause is rebindable too, and the old key must stop pausing.
ctl.reset(user)
ctl.bind(user, 'pause', pg.K_p)
user.state = 'game'
core.set_data()
for _ in range(25):
    core.run()
core.paused = False
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_ESCAPE))
core.run()
check('the old pause key no longer pauses', user.state == 'game', user.state)
user.state = 'game'
core.paused = False
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_p))
core.run()
check('the rebound pause key pauses', user.state == 'pause_menu', user.state)

# --- A full row still fits the panel it is drawn in. ------------------------
# The cap exists for this reason, so it is worth checking against the widest
# key names rather than trusting the number.
ctl.reset(user)
ctl.bind(user, 'rotate_ccw', pg.K_LCTRL)
for code in (pg.K_RSHIFT, pg.K_BACKSPACE, pg.K_RIGHT):
    ctl.bind(user, 'rotate_ccw', code, replace=False)
controls.set_labels()
row = next(o for o in controls.selections[0] if o.action == 'rotate_ccw')
check(
    'a row filled with long key names stays inside its box',
    row.text.get_width() <= row.rect.w - 8,
    '"{}" renders {}px into a {}px row'.format(controls.label('rotate_ccw'), row.text.get_width(), row.rect.w)
)
ctl.reset(user)
controls.set_labels()

# Both keys of a two-key binding have to work in the game, not just in the menu.
ctl.reset(user)
ctl.bind(user, 'rotate_ccw', pg.K_a)
ctl.bind(user, 'rotate_ccw', pg.K_UP, replace=False)
user.state = 'game'
user.gametype = 'free'
user.reset()
core.set_data()
for _ in range(30):
    core.run()
for key, name in ((pg.K_a, 'A'), (pg.K_UP, 'the up arrow')):
    core.set_shape(2)
    before = core.freeshape.state
    pg.event.clear()
    pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
    core.run()
    check(
        '{} turns the piece counter-clockwise'.format(name),
        core.freeshape.state == (before - 1) % 4,
        'state {} -> {}'.format(before, core.freeshape.state)
    )

ctl.reset(user)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
