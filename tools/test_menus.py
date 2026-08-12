"""Regression checks for the menu screens.

Drives the real menu objects with posted key events, so a dead button or a
mis-wired state name fails here instead of in front of the player.

Run with: python tools/test_menus.py
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

tetris = G.init(Namespace(debug=False, forced_delay=1.0))
user = tetris.user

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


def press(menu, key, times=1):
    """Send a full keypress to a menu and let it run a frame for each half."""
    for _ in range(times):
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYDOWN, key=key))
        menu.run()
        pg.event.clear()
        pg.event.post(pg.event.Event(pg.KEYUP, key=key))
        menu.run()


def current():
    return getattr(tetris, user.state)


def goto_main():
    user.state = 'main_menu'
    tetris.main_menu.reset()


# --- Every main menu entry goes somewhere. ----------------------------------
# Order of the options is play, help, hiscore, settings, quit.
for downs, action, expected in ((1, 'help', 'help_menu'), (3, 'settings', 'settings_menu')):
    goto_main()
    press(tetris.main_menu, pg.K_DOWN, downs)
    selected = tetris.main_menu.selected.action
    press(tetris.main_menu, pg.K_RETURN)
    check(
        'main menu {} opens {}'.format(action, expected),
        selected == action and user.state == expected,
        'landed on {} after selecting {}'.format(user.state, selected)
    )
    # And backing out returns to the main menu rather than stranding the player.
    press(current(), pg.K_ESCAPE)
    check('{} backs out to the main menu'.format(expected), user.state == 'main_menu', user.state)

# --- The settings menu actually edits settings. -----------------------------
goto_main()
press(tetris.main_menu, pg.K_DOWN, 3)
press(tetris.main_menu, pg.K_RETURN)
settings = tetris.settings_menu

before = user.forced_delay
press(settings, pg.K_LEFT)
check(
    'left lowers the forced delay by one step',
    abs(before - user.forced_delay - settings.delay_step) < 1e-9,
    '{:.2f}s -> {:.2f}s'.format(before, user.forced_delay)
)
press(settings, pg.K_RIGHT, 2)
check(
    'right raises it again',
    abs(user.forced_delay - (before + settings.delay_step)) < 1e-9,
    '{:.2f}s'.format(user.forced_delay)
)
# The label the player reads has to follow the value.
check(
    'the row label shows the current value',
    '{:.2f}s'.format(user.forced_delay) in settings.label('delay'),
    settings.label('delay')
)

# Holding left must bottom out at Off rather than going negative.
press(settings, pg.K_LEFT, 40)
check(
    'the delay bottoms out at Off',
    user.forced_delay == 0. and settings.label('delay').endswith('Off'),
    settings.label('delay')
)
press(settings, pg.K_RIGHT, 200)
check(
    'the delay tops out at its maximum',
    user.forced_delay == settings.delay_max,
    '{:.2f}s'.format(user.forced_delay)
)
user.forced_delay = 1.0
settings.set_labels()

# --- The toggles below it work too. -----------------------------------------
for downs, action, attr in ((1, 'ghost', 'showghost'), (2, 'kicks', 'enablekicks'), (3, 'tiles', 'linktiles')):
    settings.reset()
    press(settings, pg.K_DOWN, downs)
    before = getattr(user, attr)
    press(settings, pg.K_RETURN)
    check(
        'the {} toggle flips {}'.format(action, attr),
        settings.selected.action == action and getattr(user, attr) is not before,
        '{} -> {}'.format(before, getattr(user, attr))
    )
    press(settings, pg.K_RETURN)  # put it back

settings.reset()
press(settings, pg.K_DOWN, 4)
before = user.cleartype
press(settings, pg.K_RIGHT)
check(
    'the line clear mode cycles',
    settings.selected.action == 'clears' and user.cleartype == (before + 1) % 3,
    '{} -> {}'.format(before, user.cleartype)
)

# Back is the last row and has to leave the menu.
settings.reset()
press(settings, pg.K_DOWN, 5)
press(settings, pg.K_RETURN)
check('the Back row leaves the menu', user.state == 'main_menu', user.state)

# --- Settings reached mid-game returns to where it was opened from. ---------
user.state = 'pause_menu'
tetris.pause_menu.reset()
press(tetris.pause_menu, pg.K_DOWN, 2)  # resume, restart, options
press(tetris.pause_menu, pg.K_RETURN)
check(
    'the pause menu opens settings',
    user.state == 'settings_menu' and settings.return_state == 'pause_menu',
    '{}, returns to {}'.format(user.state, settings.return_state)
)
press(settings, pg.K_ESCAPE)
check('settings returns to the pause menu', user.state == 'pause_menu', user.state)

user.state = 'loss_menu'
tetris.loss_menu.reset()
press(tetris.loss_menu, pg.K_DOWN, 1)  # restart, settings, quit
press(tetris.loss_menu, pg.K_RETURN)
check(
    'the loss menu opens settings',
    user.state == 'settings_menu' and settings.return_state == 'loss_menu',
    '{}, returns to {}'.format(user.state, settings.return_state)
)
press(settings, pg.K_ESCAPE)
check('settings returns to the loss menu', user.state == 'loss_menu', user.state)

# --- A delay changed in the menu reaches the piece already in play. ---------
user.state = 'game'
user.gametype = 'free'
user.reset()
core = tetris.game
core.set_data()
for _ in range(30):
    core.run()
check('a piece is in play', core.piece_elapsed is not None)
user.forced_delay = 0.01  # what the settings menu writes
core.run()
check(
    'a delay lowered in the settings drops the piece already in play',
    core.piece_elapsed is None,
    'piece was locked on the next frame'
)

# --- Help renders whether the timer is on or off. ---------------------------
for delay in (1.0, 0.):
    user.forced_delay = delay
    user.state = 'help_menu'
    tetris.help_menu.return_state = 'main_menu'
    try:
        tetris.help_menu.run()
        ok, detail = True, ''
    except Exception as err:
        ok, detail = False, repr(err)
    check('help renders with the timer at {}'.format(delay or 'off'), ok, detail)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
