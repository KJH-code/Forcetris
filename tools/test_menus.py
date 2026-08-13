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
import engine.environment as env

# The dummy video driver never reports keyboard focus, which the game reads as a
# lost window and turns into a pause on every single frame. Nothing here is
# simulating an alt-tab, so pretend the window is focused.
pg.key.get_focused = lambda: True

tetris = G.init(Namespace(debug=False, forced_delay=1.0, volume=100., sfx_volume=100.))
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

# --- Music volume, which has to reach the mixer and not just the user. ------
settings.reset()
press(settings, pg.K_DOWN, 5)
check('the music row is where it should be', settings.selected.action == 'music', settings.selected.action)
before = user.volume
press(settings, pg.K_LEFT, 4)
check(
    'left lowers the volume',
    abs(user.volume - (before - 4 * settings.volume_step)) < 1e-9,
    '{:.0%} -> {:.0%}'.format(before, user.volume)
)
check(
    'the volume reaches the music stream',
    abs(pg.mixer.music.get_volume() - user.volume) < 0.02,
    'user {:.2f}, mixer {:.2f}'.format(user.volume, pg.mixer.music.get_volume())
)
check('the row label shows the volume', '80%' in settings.label('music'), settings.label('music'))
press(settings, pg.K_LEFT, 40)
check(
    'the volume bottoms out at Off',
    user.volume == 0. and settings.label('music').endswith('Off'),
    settings.label('music')
)
press(settings, pg.K_RIGHT, 60)
check('the volume tops out at 100%', user.volume == 1., '{:.0%}'.format(user.volume))
# Fading the music out on a loss must not leave the next game silent.
pg.mixer.music.fadeout(1)
env.restart_music()
check(
    'restarting the music restores the chosen volume',
    abs(pg.mixer.music.get_volume() - user.volume) < 0.02,
    'mixer at {:.2f}'.format(pg.mixer.music.get_volume())
)

# Back is the last row and has to leave the menu.
# --- Sound effect volume, which lives on the Sound objects themselves. ------
settings.reset()
press(settings, pg.K_DOWN, 6)
check('the sound row is where it should be', settings.selected.action == 'sound', settings.selected.action)
press(settings, pg.K_LEFT, 5)
check(
    'left lowers the sound volume',
    abs(user.sfx_volume - 0.75) < 1e-9,
    '{:.0%}'.format(user.sfx_volume)
)
check(
    'the level reaches every loaded effect',
    env.sounds and all(abs(s.get_volume() - user.sfx_volume) < 0.02 for s in env.sounds.values()),
    '{} effects loaded'.format(len(env.sounds))
)
press(settings, pg.K_LEFT, 40)
check(
    'the sound volume bottoms out at Off',
    user.sfx_volume == 0. and settings.label('sound').endswith('Off'),
    settings.label('sound')
)
press(settings, pg.K_RIGHT, 60)
check('the sound volume tops out at 100%', user.sfx_volume == 1., '{:.0%}'.format(user.sfx_volume))

# --- Every cue the code fires has to exist. ---------------------------------
missing = [name for name in env.SFX_NAMES if name not in env.sounds]
check('every effect loaded', not missing, 'missing {}'.format(missing) if missing else '{} loaded'.format(len(env.sounds)))
fired = set()
for line in open(os.path.join(ROOT, 'engine', 'game.py')):
    if 'play_sound(' in line and 'def ' not in line:
        fired.update(part.split("'")[0] for part in line.split("play_sound(")[1].split("'")[1::2])
unknown = sorted(name for name in fired if name not in env.SFX_NAMES)
check(
    'every cue the game fires is a real effect',
    not unknown, 'unknown {}'.format(unknown) if unknown else 'fires {}'.format(sorted(fired))
)

# Back is the last row and has to leave the menu.
settings.reset()
press(settings, pg.K_DOWN, 9)
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

# --- The command line agrees with what the settings menu shows. -------------
# Volume is a percentage on the command line and a fraction internally, so the
# conversion is the one place a 60 could silently become a 6000%.
for given, expected in ((100., 1.0), (60., 0.6), (0., 0.0), (-5., 0.0), (400., 1.0)):
    user.eval_argv(Namespace(debug=False, forced_delay=1.0, volume=given, sfx_volume=given))
    check(
        '--volume {:g} becomes {:.0%}'.format(given, expected),
        abs(user.volume - expected) < 1e-9,
        'got {:.2f}'.format(user.volume)
    )

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
