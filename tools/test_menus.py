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
# The saved profile is read the moment engine.environment is imported, so this has
# to be pointed somewhere disposable before that happens - otherwise a test run
# reads, and then overwrites, the player's own settings.
os.environ['FORCETRIS_CONFIG'] = os.path.join(tempfile.mkdtemp(), 'settings.json')

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(tempfile.mkdtemp())

import pygame as pg
import engine.game as G
import engine.environment as env
import engine.userstate as us

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


def goto_row(menu, action, limit=30):
    """Move the cursor onto a named row.

    Counting Down presses breaks every time a row is inserted above the one a
    check cares about, which is a property of the test rather than the menu.
    """
    menu.reset()
    for _ in range(limit):
        if menu.selected.action == action:
            return True
        press(menu, pg.K_DOWN)
    return False


def current():
    return getattr(tetris, user.state)


def goto_main():
    user.state = 'main_menu'
    tetris.main_menu.reset()


# --- Every main menu entry goes somewhere. ----------------------------------
# Found by name rather than by counting Down presses, so inserting a row above
# one of these does not silently start testing a different button.
for action, expected in (
    ('help', 'help_menu'), ('replays', 'replay_menu'), ('settings', 'settings_menu')
):
    goto_main()
    check('the main menu has a {} row'.format(action), goto_row(tetris.main_menu, action))
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
goto_row(tetris.main_menu, 'settings')
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
for action, attr in (('ghost', 'showghost'), ('kicks', 'enablekicks'), ('tiles', 'linktiles')):
    goto_row(settings, action)
    before = getattr(user, attr)
    press(settings, pg.K_RETURN)
    check(
        'the {} toggle flips {}'.format(action, attr),
        settings.selected.action == action and getattr(user, attr) is not before,
        '{} -> {}'.format(before, getattr(user, attr))
    )
    press(settings, pg.K_RETURN)  # put it back

goto_row(settings, 'clears')
before = user.cleartype
press(settings, pg.K_RIGHT)
check(
    'the line clear mode cycles',
    settings.selected.action == 'clears' and user.cleartype == (before + 1) % 3,
    '{} -> {}'.format(before, user.cleartype)
)

# --- Music volume, which has to reach the mixer and not just the user. ------
goto_row(settings, 'music')
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
goto_row(settings, 'sound')
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
# A cue named by a format string, like combo{}, stands for the family of names
# that share its prefix.
def known(name):
    if '{}' not in name:
        return name in env.SFX_NAMES
    prefix = name.split('{}')[0]
    return any(real.startswith(prefix) for real in env.SFX_NAMES)


unknown = sorted(name for name in fired if not known(name))
check(
    'every cue the game fires is a real effect',
    not unknown, 'unknown {}'.format(unknown) if unknown else 'fires {}'.format(sorted(fired))
)

# Back is the last row and has to leave the menu.
goto_row(settings, 'back')
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
goto_row(tetris.loss_menu, 'settings')
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
# Set the budget below what this piece has already spent, rather than to a fixed
# number of milliseconds: these frames run as fast as the loop can go, not at the
# 50fps the game paces itself to, so a literal here is a coin flip.
user.forced_delay = max(0.001, core.piece_elapsed / 2)   # what the settings menu writes
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


# --- The mode screen starts modes, and carries the timer switch. ------------
play = tetris.play_menu

for action, gametype in (('arcade', 'arcade'), ('timed', 'timed'), ('free', 'free')):
    goto_main()
    user.state = 'play_menu'
    check('the mode screen has a {} row'.format(action), goto_row(play, action), play.selected.action)
    press(play, pg.K_RETURN)
    check(
        '{} starts a {} game'.format(action, gametype),
        user.state == 'game' and user.gametype == gametype,
        '{} / {}'.format(user.state, user.gametype)
    )

user.state = 'play_menu'
check('the mode screen has a timer row', goto_row(play, 'timer'), play.selected.action)

# The switch, and the thing that makes it a switch rather than a way of losing
# the number: the budget has to come back.
user.forced_delay = 0.75
# Deliberately stale, so the budget can only come back if switching off is what
# put it there. Seeding both halves would let a toggle that forgets still pass.
user.forced_hold = 0.05
play.set_labels()
press(play, pg.K_LEFT)
check('the timer row switches the forced drop off', user.forced_delay == 0., str(user.forced_delay))
check('switching off does not start the game', user.state == 'play_menu', user.state)
check('the row says so', 'Off' in play.label('timer'), play.label('timer'))
press(play, pg.K_RIGHT)
check(
    'switching back on returns the budget that was set',
    user.forced_delay == 0.75, str(user.forced_delay)
)
check('the row says that too', '0.75s' in play.label('timer'), play.label('timer'))

# Confirm has to work on that row as well, or a player who never presses the
# arrow keys there is stuck with whatever it happened to be.
press(play, pg.K_RETURN)
check(
    'confirm switches the row rather than starting a game',
    user.forced_delay == 0. and user.state == 'play_menu',
    '{} / {}'.format(user.forced_delay, user.state)
)
press(play, pg.K_RETURN)

# Holding the key must not flap the switch back and forth every frame.
pg.event.clear()
pg.event.post(pg.event.Event(pg.KEYDOWN, key=pg.K_LEFT))
for _ in range(8):
    play.run()
    pg.event.clear()
check('holding the key switches it once, not once a frame', user.forced_delay == 0., str(user.forced_delay))
pg.event.post(pg.event.Event(pg.KEYUP, key=pg.K_LEFT))
play.run()
pg.event.clear()
press(play, pg.K_RIGHT)

# A budget set in the settings menu is what the switch turns back on, and the
# row has to show it even though the two screens never talk to each other.
goto_main()
user.state = 'settings_menu'
goto_row(tetris.settings_menu, 'delay')
press(tetris.settings_menu, pg.K_LEFT, 4)
tuned = user.forced_delay
user.state = 'play_menu'
goto_row(play, 'timer')
play.run()
check(
    'the row follows a budget changed in the settings menu',
    '{:.2f}s'.format(tuned) in play.label('timer'),
    '{} against {}'.format(play.label('timer'), tuned)
)
press(play, pg.K_LEFT)
press(play, pg.K_RIGHT)
check(
    'and that is the budget the switch brings back',
    user.forced_delay == tuned, '{} against {}'.format(user.forced_delay, tuned)
)

# Left and right on a mode row must not wrap the cursor sideways: there is only
# one column, so it would just flicker.
goto_row(play, 'free')
press(play, pg.K_LEFT)
press(play, pg.K_RIGHT)
check('sideways on a mode row does nothing', play.selected.action == 'free', play.selected.action)

goto_main()

# --- The defaults a new player gets. ----------------------------------------
fresh = us.User()
check(
    'line clears default to naive, the way every guideline game clears',
    fresh.cleartype == us.CLEAR_NAIVE,
    tetris.settings_menu.clear_names[fresh.cleartype]
)
check(
    'the forced drop starts on, with a budget behind the switch',
    fresh.forced_delay > 0. and fresh.forced_hold == fresh.forced_delay,
    '{} / {}'.format(fresh.forced_delay, fresh.forced_hold)
)

# --- Nothing may be laid out past the panel it belongs to. ------------------
# Adding a row is easy; noticing that it pushed the hint line on top of the last
# one is not, and no behavioural check would catch it.
for name in ('settings_menu', 'controls_menu', 'handling_menu', 'help_menu', 'main_menu', 'pause_menu', 'play_menu'):
    menu = getattr(tetris, name)
    rows = [option for column in menu.selections for option in column]
    # Menus that print a hint along the bottom need that strip left clear. Asked of
    # the class, not the instance: Menu.__getattr__ answers None for any name it
    # does not know, so hasattr on an instance is true for everything.
    reserved = 24 if hasattr(type(menu), 'display_hint') else 0
    overflow = [
        o.action for o in rows
        if not menu.rect.contains(o.rect) or o.rect.bottom > menu.rect.bottom - reserved
    ]
    check(
        '{} lays its rows out inside the panel'.format(name),
        not overflow,
        'rows past the edge or under the hint line: {}'.format(overflow)
    )

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
