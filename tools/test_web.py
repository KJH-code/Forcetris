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
missing = [
    path for path in ('textures/tileset.png', 'music/tetris.ogg', 'sound/forced.wav', 'main.py')
    if not os.path.exists(os.path.join(ROOT, path))
]
check('every packaged asset is where the build expects it', not missing, 'missing {}'.format(missing))

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
