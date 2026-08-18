"""Regression checks for the browser build's requirements.

The browser runs the game on the page's own event loop, so the program loop has
to hand control back between frames. A loop that stops yielding still works
perfectly on the desktop and freezes the tab outright, which is exactly the kind
of regression nobody notices until the phone is in their hand.

These checks need no browser: they assert the properties the browser depends on.

Run with: python tools/test_web.py
"""
import os
import sys
import asyncio
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
import engine.environment as env

pg.key.get_focused = lambda: True

FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


tetris = G.init(Namespace(debug=False, forced_delay=1.0, volume=0., sfx_volume=0.))
user = tetris.user

# --- The program loop is a coroutine that yields between frames. ------------
check('the program loop is a coroutine', asyncio.iscoroutinefunction(type(tetris).run))

# Let it quit without tearing pygame down under the rest of the checks.
env.quit = lambda *args: None


async def drive():
    """Run the game loop beside a sibling task and count the sibling's turns."""
    turns = 0

    async def sibling():
        nonlocal turns
        while user.state != 'quit':
            turns += 1
            await asyncio.sleep(0)

    async def stopper():
        for _ in range(20):
            await asyncio.sleep(0)
        user.state = 'quit'

    # wait_for so that a loop which stopped yielding fails the run instead of
    # hanging it, which is what a plain gather would do.
    await asyncio.wait_for(asyncio.gather(tetris.run(), sibling(), stopper()), timeout=20)
    return turns


user.state = 'main_menu'
tetris.main_menu.reset()
try:
    turns = asyncio.run(drive())
    check(
        'the loop hands control back between frames',
        turns > 5, 'a sibling task got {} turns while the game ran'.format(turns)
    )
except asyncio.TimeoutError:
    check('the loop hands control back between frames', False, 'the loop never yielded')

# --- The entry point survives arguments it does not recognise. --------------
# pygbag can hand the page's own arguments to the script, and argparse answers an
# unknown one by exiting, which on the web means killing the tab.
probe = subprocess.run(
    [sys.executable, os.path.join(ROOT, 'main.py'), '--help'],
    capture_output=True, text=True, timeout=120,
)
check(
    'the entry point still parses arguments on the desktop',
    probe.returncode == 0 and '--forced-delay' in probe.stdout,
    'exit {}'.format(probe.returncode)
)
source = open(os.path.join(ROOT, 'main.py')).read()
check(
    'unknown arguments are tolerated under emscripten',
    "parse_known_args" in source and "emscripten" in source,
    'main.py falls back to parse_known_args on the web'
)

# --- Assets the packager has to find. ---------------------------------------
# Derived from what the game actually asks for rather than listed by hand: a
# shortlist stops noticing the moment a new cue or texture is added, and the
# symptom on the web is a silent cue or a crash in a tab with no console open.
import re

wanted = ['main.py', os.path.join('music', 'tetris.ogg')]
wanted += [os.path.join('sound', name + '.wav') for name in env.SFX_NAMES]
for module in sorted(os.listdir(os.path.join(ROOT, 'engine'))):
    if module.endswith('.py'):
        wanted.append(os.path.join('engine', module))
# Every texture named in a load_image call anywhere in the engine.
for module in sorted(os.listdir(os.path.join(ROOT, 'engine'))):
    if not module.endswith('.py'):
        continue
    source = open(os.path.join(ROOT, 'engine', module)).read()
    for name in re.findall(r"load_image\(\s*'([^']+)'", source):
        wanted.append(os.path.join('textures', name))

missing = sorted(set(
    path for path in wanted if not os.path.exists(os.path.join(ROOT, path))))
check(
    'every packaged asset is where the build expects it',
    not missing, 'missing {}'.format(missing) if missing else '{} files'.format(len(set(wanted)))
)
check(
    'the check is actually looking at something',
    len(set(wanted)) > 25, '{} files'.format(len(set(wanted)))
)

# --- What the packager is told to leave out. --------------------------------
# pygbag packages the whole folder unless pygbag.ini says otherwise, so the ini
# is the only thing standing between the player and a page that also downloads
# the test suite. It is also one typo away from dropping something the game
# needs, which is the failure worth guarding.
import configparser
import json as _json

ini = configparser.ConfigParser()
read = ini.read(os.path.join(ROOT, 'pygbag.ini'))
check('the build config is there to be read', read, str(read))
try:
    skipped = _json.loads(ini.get('DEPENDENCIES', 'ignoreDirs'))
except (configparser.Error, ValueError) as err:
    skipped = None
    check('the build config parses', False, repr(err))
else:
    check('the build config parses', True, str(skipped))

if skipped is not None:
    check(
        'the test suite is left out of the browser build',
        'tools' in skipped, str(skipped)
    )
    # Nothing the game loads may live under a folder the packager is skipping.
    dropped = sorted(
        path for path in set(wanted)
        if path.replace(os.sep, '/').split('/')[0] in skipped
    )
    check(
        'nothing the game needs is skipped with it',
        not dropped, str(dropped)
    )
    check(
        'and folders written at runtime are skipped, not shipped',
        'data' in skipped, str(skipped)
    )

# --- Writes that fail must not take the tab down. ---------------------------
# The browser gets an in-memory filesystem that can refuse a write, and the two
# things the game writes on its own - the settings profile and a replay - both
# happen at moments where a crash would be worst: mid-menu, and the instant a
# run ends.
import engine.controls as ctl
import engine.replay as rp
import engine.userstate as us

blocked = os.path.join(tempfile.mkdtemp(), 'nope')
open(blocked, 'w').close()  # a file where a folder would have to go
kept_config, kept_folder = ctl.CONFIG, rp.FOLDER
ctl.CONFIG = os.path.join(blocked, 'settings.json')
rp.FOLDER = os.path.join(blocked, 'replays')
try:
    ctl.save(env.user)
    check('a settings write that cannot happen is survivable', True)
except Exception as err:
    check('a settings write that cannot happen is survivable', False, repr(err))
try:
    spare = rp.Replay({'played': '2020-01-01T00:00:00', 'gametype': 'free'}, [])
    check('a replay that cannot be saved reports as much', rp.save(spare) is None)
    check('and listing an unreachable folder is empty, not fatal', rp.listing() == [])
except Exception as err:
    check('a replay that cannot be saved reports as much', False, repr(err))
finally:
    ctl.CONFIG, rp.FOLDER = kept_config, kept_folder

# Reading a profile that is not there has to give the defaults rather than raise,
# which is what the very first launch in a fresh tab does.
fresh = us.User()
ctl.CONFIG = os.path.join(blocked, 'settings.json')
try:
    ctl.load(fresh)
    check(
        'a missing profile falls back to the defaults',
        fresh.forced_delay == us.DEFAULT_FORCED_DELAY and fresh.keys,
        str(fresh.forced_delay)
    )
finally:
    ctl.CONFIG = kept_config

# --- Quitting must not take the page down with it. --------------------------
# Run in a subprocess, because the honest way to check this is to actually call
# quit(), and that shuts pygame down for good.
probe = subprocess.run(
    [sys.executable, '-c',
     "import os, sys\n"
     "os.environ['SDL_VIDEODRIVER'] = 'dummy'\n"
     "os.environ['SDL_AUDIODRIVER'] = 'dummy'\n"
     "sys.path.insert(0, %r)\n"
     "import engine.environment as env\n"
     "env.WEB = True\n"
     "env.quit()\n"
     "print('returned')\n" % ROOT],
    capture_output=True, text=True, timeout=120,
)
check(
    'quitting under the web build returns instead of exiting',
    'returned' in probe.stdout,
    'quit() did not return' if not probe.stdout else ''
)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
