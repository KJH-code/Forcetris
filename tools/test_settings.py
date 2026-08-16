"""Regression checks for the saved profile.

The thing worth guarding here is precedence. Three sources set the same values -
the built-in defaults, the file on disk, and the command line - and they are read
in that order by three different pieces of code at three different times. Get the
order wrong and the symptom is not a crash, it is a settings menu that silently
forgets, or a --forced-delay flag that does nothing.

The last check relaunches the game in a second process, because that is the only
way to prove the values survive the thing they are supposed to survive.

Run with: python tools/test_settings.py
"""
import os
import sys
import json
import tempfile
import subprocess
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
import engine.userstate as us

pg.key.get_focused = lambda: True

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


tetris = G.init(Namespace(debug=False, forced_delay=None, volume=None, sfx_volume=None))
user = tetris.user
settings = tetris.settings_menu


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


def reload_into_fresh_user():
    """A brand new User, loaded from the file, as a relaunch would build it."""
    fresh = us.User()
    ctl.load(fresh)
    return fresh


def with_profile(contents):
    """Write a profile by hand and read it back into a fresh User."""
    with open(ctl.CONFIG, 'w') as config:
        config.write(contents)
    return reload_into_fresh_user()


# --- Everything the settings menu can change comes back. --------------------
ctl.reset(user)
user.forced_delay = 0.35
user.volume = 0.4
user.sfx_volume = 0.15
user.cleartype = 0
user.spinrule = us.SPIN_TSPIN
user.enablekicks = False
user.showghost = False
user.linktiles = False
ctl.set_handling(user, 'das', 100)
ctl.set_handling(user, 'arr', 0)
ctl.set_handling(user, 'sdf', 40)
ctl.set_handling(user, 'are', 60)
ctl.bind(user, 'harddrop', pg.K_w)
ctl.bind(user, 'rotate_180', pg.K_s, replace=False)
ctl.save(user)

saved = json.load(open(ctl.CONFIG))
check(
    'the file carries keys, handling and settings',
    set(saved) == {'keys', 'handling', 'settings'},
    str(sorted(saved))
)

restarted = reload_into_fresh_user()
mismatched = [
    name for name, clean in ctl.SETTINGS
    if getattr(restarted, name) != getattr(user, name)
]
check('every setting survives a restart', not mismatched, str(mismatched))
check(
    'the forced delay survives a restart',
    restarted.forced_delay == 0.35, str(restarted.forced_delay)
)
check(
    'the volumes survive a restart',
    (restarted.volume, restarted.sfx_volume) == (0.4, 0.15),
    '{} / {}'.format(restarted.volume, restarted.sfx_volume)
)
check(
    'handling survives a restart',
    (restarted.das, restarted.arr, restarted.sdf, restarted.are) == (100, 0, 40, 60),
    'das {} arr {} sdf {} are {}'.format(restarted.das, restarted.arr, restarted.sdf, restarted.are)
)
check(
    'bindings survive a restart',
    ctl.matches(restarted, 'harddrop', pg.K_w)
    and restarted.keys['rotate_180'] == (pg.K_a, pg.K_s),
    ctl.describe(restarted, 'rotate_180')
)

# --- The menu writes as it goes, without being told to save. ----------------
ctl.reset(user)
user.forced_delay = 1.0
ctl.save(user)
goto_row(settings, 'delay')
press_menu(settings, pg.K_LEFT)
check(
    'changing a row in the menu writes it out',
    reload_into_fresh_user().forced_delay == user.forced_delay,
    '{} on disk against {} in play'.format(reload_into_fresh_user().forced_delay, user.forced_delay)
)

goto_row(settings, 'ghost')
before = user.showghost
press_menu(settings, pg.K_RIGHT)
check(
    'a toggled row is written out too',
    reload_into_fresh_user().showghost == user.showghost != before,
    str(user.showghost)
)

# --- Precedence: defaults, then the file, then the command line. ------------
ctl.reset(user)
user.forced_delay = 0.25
user.volume = 0.5
user.sfx_volume = 0.5
ctl.save(user)

profiled = reload_into_fresh_user()
profiled.eval_argv(Namespace(debug=False, forced_delay=None, volume=None, sfx_volume=None))
check(
    'a flag nobody passed leaves the saved value alone',
    (profiled.forced_delay, profiled.volume, profiled.sfx_volume) == (0.25, 0.5, 0.5),
    '{} {} {}'.format(profiled.forced_delay, profiled.volume, profiled.sfx_volume)
)

overridden = reload_into_fresh_user()
overridden.eval_argv(Namespace(debug=False, forced_delay=0.8, volume=20., sfx_volume=None))
check(
    'a flag that was passed wins over the saved value',
    (overridden.forced_delay, overridden.volume) == (0.8, 0.2),
    '{} / {}'.format(overridden.forced_delay, overridden.volume)
)
check(
    'overriding one flag does not disturb the others',
    overridden.sfx_volume == 0.5, str(overridden.sfx_volume)
)

# The command line is the last word, not a new saved default: a flag passed once
# must not quietly become the value every later launch starts from.
check(
    'the command line is not written back to the file',
    reload_into_fresh_user().forced_delay == 0.25,
    str(reload_into_fresh_user().forced_delay)
)

# --- A file the game did not write must not be able to break it. ------------
broken = with_profile('{not json at all')
check(
    'a corrupt profile falls back to the defaults',
    broken.forced_delay == us.DEFAULT_FORCED_DELAY and ctl.matches(broken, 'harddrop', pg.K_SPACE),
    str(broken.forced_delay)
)

partial = with_profile(json.dumps({'settings': {'volume': 0.3}}))
check(
    'a profile holding one setting leaves the rest at their defaults',
    partial.volume == 0.3
    and partial.sfx_volume == us.DEFAULT_SFX_VOLUME
    and partial.das == ctl.HANDLING_DEFAULTS['das'],
    '{} / {} / {}'.format(partial.volume, partial.sfx_volume, partial.das)
)

nonsense = with_profile(json.dumps({'settings': {
    'volume': 'loud',           # not a number
    'spinrule': 99,             # past the end of the rule list
    'forced_delay': -4.,        # below the floor
    'sfx_volume': 0.6,          # perfectly fine, and must not be lost with the rest
}}))
check(
    'an unusable value is skipped rather than taken',
    nonsense.volume == us.DEFAULT_VOLUME, str(nonsense.volume)
)
check(
    'out of range values are clamped into range',
    nonsense.spinrule == len(us.SPIN_RULES) - 1 and nonsense.forced_delay == 0.,
    '{} / {}'.format(nonsense.spinrule, nonsense.forced_delay)
)
check(
    'one bad value does not throw away the good ones beside it',
    nonsense.sfx_volume == 0.6, str(nonsense.sfx_volume)
)

# A profile from a version that knew about settings this one does not.
future = with_profile(json.dumps({
    'settings': {'volume': 0.7, 'gravity_style': 'sonic'},
    'handling': {'das': 80, 'wiggle': 3},
    'keys': {'harddrop': [pg.K_w], 'moonwalk': [pg.K_m]},
}))
check(
    'settings this version does not know about are ignored',
    future.volume == 0.7 and future.das == 80 and ctl.matches(future, 'harddrop', pg.K_w),
    '{} / {}'.format(future.volume, future.das)
)

# --- An upgrade keeps the bindings the old file already held. ---------------
legacy_dir = tempfile.mkdtemp()
legacy = os.path.join(legacy_dir, 'controls.json')
with open(legacy, 'w') as config:
    json.dump({'keys': {'hold': [pg.K_c]}, 'handling': {'das': 60}}, config)

kept_config, kept_legacy = ctl.CONFIG, ctl.LEGACY
ctl.CONFIG = os.path.join(legacy_dir, 'settings.json')
ctl.LEGACY = legacy
try:
    upgraded = reload_into_fresh_user()
    check(
        'an old controls.json is read when there is no settings.json',
        ctl.matches(upgraded, 'hold', pg.K_c) and upgraded.das == 60,
        '{} / {}'.format(ctl.describe(upgraded, 'hold'), upgraded.das)
    )
    # Once the new file exists it is the only one that counts, so an old file left
    # lying around cannot resurrect bindings that were deliberately changed.
    ctl.bind(upgraded, 'hold', pg.K_v)
    after = reload_into_fresh_user()
    check(
        'the new file takes over once it is written',
        ctl.matches(after, 'hold', pg.K_v) and not ctl.matches(after, 'hold', pg.K_c),
        ctl.describe(after, 'hold')
    )
finally:
    ctl.CONFIG, ctl.LEGACY = kept_config, kept_legacy

# --- Naming a profile keeps the game off the default one. -------------------
check(
    'FORCETRIS_CONFIG picks the file that gets read and written',
    ctl.CONFIG == os.environ['FORCETRIS_CONFIG'], ctl.CONFIG
)
check(
    'naming a profile means the old file is not consulted at all',
    ctl.LEGACY is None, str(ctl.LEGACY)
)

# --- The real thing: a second process, started from scratch. ----------------
# Everything above reloads inside one interpreter, where a stale module-level value
# would go unnoticed. This one actually restarts the game.
relaunch_dir = tempfile.mkdtemp()
relaunch = os.path.join(relaunch_dir, 'settings.json')
env = dict(os.environ, FORCETRIS_CONFIG=relaunch, PYTHONPATH=ROOT)

WRITE = (
    "import pygame as pg, engine.game as G, engine.controls as ctl\n"
    "from argparse import Namespace\n"
    "t = G.init(Namespace(debug=False, forced_delay=None, volume=None, sfx_volume=None))\n"
    "u = t.user\n"
    "u.forced_delay = 0.45\n"
    "u.sfx_volume = 0.25\n"
    "u.spinrule = 1\n"
    "ctl.set_handling(u, 'das', 80)\n"
    "ctl.bind(u, 'hold', pg.K_c)\n"
    "ctl.save(u)\n"
)
READ = (
    "import pygame as pg, engine.game as G, engine.controls as ctl\n"
    "from argparse import Namespace\n"
    "t = G.init(Namespace(debug=False, forced_delay=None, volume=None, sfx_volume=None))\n"
    "u = t.user\n"
    "print(u.forced_delay, u.sfx_volume, u.spinrule, u.das, ctl.matches(u, 'hold', pg.K_c))\n"
)

first = subprocess.run(
    [sys.executable, '-c', WRITE], cwd=relaunch_dir, env=env,
    capture_output=True, text=True, timeout=120
)
second = subprocess.run(
    [sys.executable, '-c', READ], cwd=relaunch_dir, env=env,
    capture_output=True, text=True, timeout=120
)
check(
    'a fresh process starts up against the saved profile',
    second.returncode == 0 and second.stdout.strip().endswith('0.45 0.25 1 80 True'),
    (second.stdout.strip() or second.stderr.strip()[-200:]) if second.returncode == 0
    else first.stderr.strip()[-200:] + second.stderr.strip()[-200:]
)

OVERRIDE = READ.replace('forced_delay=None', 'forced_delay=0.9')
third = subprocess.run(
    [sys.executable, '-c', OVERRIDE], cwd=relaunch_dir, env=env,
    capture_output=True, text=True, timeout=120
)
check(
    'a fresh process still lets the command line override the profile',
    third.returncode == 0 and third.stdout.strip().endswith('0.9 0.25 1 80 True'),
    third.stdout.strip() or third.stderr.strip()[-200:]
)

ctl.reset(user)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
