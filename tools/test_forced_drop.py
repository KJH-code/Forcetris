"""Regression checks for the forced hard drop timer.

Drives Core headlessly with a fake clock instead of playing the game.

Frames are stepped by hand at a fixed 50 fps and time.perf_counter is replaced
inside engine.game, so the timer can be measured exactly without waiting on the
wall clock.

Run with: python tools/test_forced_drop.py
"""
import os
import sys
import tempfile
from argparse import Namespace

os.environ['SDL_VIDEODRIVER'] = 'dummy'
os.environ['SDL_AUDIODRIVER'] = 'dummy'
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
# Deliberately run from somewhere unrelated: the game resolves its textures, music
# and save data against its own folder, and regressing that breaks IDE run buttons.
os.chdir(tempfile.mkdtemp())

import pygame as pg
import engine.game as G

# The dummy video driver never reports keyboard focus, which the game reads as
# "window lost focus" and turns into a pause. Pretend the window is focused.
pg.key.get_focused = lambda: True

FRAME = 0.02  # 50 fps, matching env.clock.tick(50) in Game.run().


class Clock:
    def __init__(self):
        self.now = 1000.0

    def __call__(self):
        return self.now

    def step(self, dt=FRAME):
        self.now += dt


def build(forced_delay, gametype='free'):
    """Return (game core, clock, spawn/lock event log)."""
    clock = Clock()
    G.time.perf_counter = clock
    tetris = G.init(Namespace(debug=False, forced_delay=forced_delay))
    core = tetris.game
    core.user.state = 'game'
    core.user.gametype = gametype
    core.user.reset()
    core.set_data()

    log = []
    inside_timer = []  # Non-empty while eval_forced_drop is on the stack.
    spawn = core.set_shape
    fallen = core.eval_fallen
    forced = core.eval_forced_drop

    def set_shape(shape):
        spawn(shape)
        if core.piece_elapsed is not None:
            log.append(('spawn', clock.now, core.freeshape.pos[:]))

    def eval_fallen(posdif):
        # Recorded before the call, while the piece is still where it locked.
        pos = core.freeshape.pos[:]
        ghost = core.ghostshape.pos[:]
        fallen(posdif)
        log.append(('forced' if inside_timer else 'lock', clock.now, (pos, ghost)))

    def eval_forced_drop():
        inside_timer.append(True)
        try:
            return forced()
        finally:
            inside_timer.pop()

    core.set_shape = set_shape
    core.eval_fallen = eval_fallen
    core.eval_forced_drop = eval_forced_drop
    return core, clock, log


def spin(core, clock, frames, dt=FRAME):
    for _ in range(frames):
        clock.step(dt)
        core.run()
        if core.user.state != 'game':
            return False
    return True


def lifetimes(log):
    """(seconds the piece was in play, locked by timer?, landed on ghost?) per piece."""
    out = []
    spawn = None
    for entry in log:
        kind, at = entry[0], entry[1]
        if kind == 'spawn':
            spawn = at
        elif spawn is not None:
            pos, ghost = entry[2]
            out.append((at - spawn, kind == 'forced', pos == ghost))
            spawn = None
    return out


FAILED = []


def check(name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name, ' -- ' + detail if detail else ''))
    if not ok:
        FAILED.append(name)


# --- Forced drops fire at the configured delay. -----------------------------
for delay in (1.0, 0.5, 0.3):
    core, clock, log = build(delay)
    spin(core, clock, int(12 * delay / FRAME) + 400)
    lives = lifetimes(log)
    timed = [t for t, was_forced, _ in lives if was_forced]
    check(
        'forced-delay={} fires on schedule'.format(delay),
        len(timed) >= 5 and all(delay <= t <= delay + 2 * FRAME for t in timed),
        '{} of {} pieces dropped by the timer, lifetimes {}'.format(
            len(timed), len(lives), ['{:.3f}'.format(t) for t in timed[:5]])
    )
    # A piece may still lock early under normal gravity once the stack is high,
    # but nothing may ever survive past its deadline.
    check(
        'forced-delay={} no piece outlives its deadline'.format(delay),
        all(t <= delay + 2 * FRAME for t, _, _ in lives),
        'longest {:.3f}s'.format(max(t for t, _, _ in lives))
    )
    check(
        'forced-delay={} forced pieces land on the ghost'.format(delay),
        all(on_ghost for _, was_forced, on_ghost in lives if was_forced)
    )

# --- A delay of 0 leaves the base game untouched. ---------------------------
core, clock, log = build(0.0)
spin(core, clock, 200)  # 4 real seconds
check(
    'forced-delay=0 disables the timer',
    not [e for e in log if e[0] == 'forced'],
    '{} locks, none forced'.format(len([e for e in log if e[0] == 'lock']))
)

# --- Holding hands the incoming piece a fresh timer. ------------------------
core, clock, log = build(1.0)
spin(core, clock, 30)  # let a piece spawn and age
before = core.piece_elapsed
core.hold_shape()  # first hold ever: pulls a fresh piece out of the queue
check(
    'first hold restarts the timer', before > 0. and core.piece_elapsed == 0.,
    'elapsed {:.3f}s before, {:.3f}s after'.format(before, core.piece_elapsed)
)
core, clock, log = build(60.0)  # long enough that no drop interferes
spin(core, clock, 30)
core.hold_shape()  # seed storage
spin(core, clock, 40)
core.hold_lock = False
before = core.piece_elapsed
core.hold_shape()  # a real swap
check(
    'hold swap restarts the timer', before > 0. and core.piece_elapsed == 0.,
    'elapsed {:.3f}s before, {:.3f}s after'.format(before, core.piece_elapsed)
)
# The per-piece hold lock is the only thing stopping a player from stalling
# forever, so a refused hold must leave the timer alone.
core, clock, log = build(60.0)
spin(core, clock, 30)
core.hold_shape()  # seed storage, which spawns a piece with a fresh hold allowance
spin(core, clock, 40)
core.hold_shape()  # spends that allowance and sets the hold lock
locked = core.hold_lock
spin(core, clock, 20)
before = core.piece_elapsed
core.hold_shape()  # refused, the lock is still set
check(
    'a refused hold does not restart the timer',
    locked and before > 0. and core.piece_elapsed == before,
    'lock {}, elapsed {:.3f}s before, {:.3f}s after'.format(locked, before, core.piece_elapsed)
)
# Holding during the spawn delay reorders the queue but must not start a timer
# for a piece that is not in play yet.
core, clock, log = build(60.0)
spin(core, clock, 30)
core.hard_drop()  # into the spawn delay, no active piece
core.hold_shape()
check('holding between pieces starts no timer', core.piece_elapsed is None)

# --- Soft drop does not stop the clock either. ------------------------------
core, clock, log = build(1.0)
spin(core, clock, 25)
core.soft_drop = True
before = core.piece_elapsed
spin(core, clock, 10)
check(
    'soft drop keeps the timer running', core.piece_elapsed - before > 0.15,
    'advanced {:.3f}s over 10 frames'.format(core.piece_elapsed - before)
)

# --- Wall kicks reset gravity but never the drop timer. ---------------------
core, clock, log = build(1.0)
spin(core, clock, 25)
before = core.piece_elapsed
core.newshape.rotate(True)
core.wall_kick()
check(
    'wall kick does not reset the drop timer', core.piece_elapsed == before,
    'elapsed {:.3f}s'.format(core.piece_elapsed)
)

# --- A long stall (pause, alt-tab) does not instantly slam the piece. -------
core, clock, log = build(1.0)
spin(core, clock, 25)
before = core.piece_elapsed
clock.step(30.0)  # half a minute spent in the pause menu
core.run()
check(
    'a 30s stall costs at most one clamped frame',
    core.piece_elapsed - before <= core.max_frame_delta + 1e-9,
    'timer advanced {:.3f}s'.format(core.piece_elapsed - before)
)

# --- The timer stays off during the spawn delay and after a loss. ----------
core, clock, log = build(1.0)
spin(core, clock, 25)
core.hard_drop()
check('timer stops while no piece is in play', core.piece_elapsed is None)

# --- Timed and arcade modes work the same way. ------------------------------
for mode in ('arcade', 'timed'):
    core, clock, log = build(0.5, gametype=mode)
    spin(core, clock, 300)
    lives = lifetimes(log)
    timed = [t for t, was_forced, _ in lives if was_forced]
    check(
        '{} mode honours the timer'.format(mode),
        len(timed) >= 3 and all(0.5 <= t <= 0.5 + 2 * FRAME for t in timed),
        '{} of {} pieces dropped by the timer'.format(len(timed), len(lives))
    )

# --- The game does not care where it was started from. ----------------------
# Reaching this point at all proves the textures and music loaded from a foreign
# working directory; this covers the save file, which is written much later.
import engine.filehandler as fh
with fh.SFH() as sfh:
    tables = sfh.decode()
check(
    'save data goes next to the game, not into the working directory',
    len(tables) == 3 and not os.listdir(os.getcwd()),
    'cwd left with {} entries'.format(len(os.listdir(os.getcwd())))
)

print()
if FAILED:
    print('{} check(s) failed: {}'.format(len(FAILED), ', '.join(FAILED)))
    sys.exit(1)
print('All checks passed.')
