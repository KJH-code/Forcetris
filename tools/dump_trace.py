"""Play scripted games through the Python engine and write down what happened.

The C++ sim is graded against these traces: the same piece feed and the same
frame-stamped inputs go into both engines, and everything the piece visibly does
- every position it stands in, every lock, the loss if there is one, the final
board - has to come out the same. A sim that merely plays plausible Tetris would
pass a glance; one that agrees frame by frame with the engine it replaces is the
only kind worth building a game on.

The scripts are seeded pseudo-random button mashing across several handling
configurations, because hand-written scripts test the paths their author thought
of. Randomness under a fixed seed tests the paths nobody did, reproducibly.

Run with: python tools/dump_trace.py [path]
"""
import os
import sys
import tempfile

os.environ.setdefault('SDL_VIDEODRIVER', 'dummy')
os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
os.environ.setdefault('FORCETRIS_CONFIG', os.path.join(tempfile.mkdtemp(), 'settings.json'))
os.environ.setdefault('FORCETRIS_REPLAYS', os.path.join(tempfile.mkdtemp(), 'replays'))

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
os.chdir(tempfile.mkdtemp())

import random
from argparse import Namespace

import pygame as pg
import engine.game as G
import engine.replay as rp
import engine.userstate as us
from engine.shapes import Shape

pg.key.get_focused = lambda: True

# The abstract keys a script speaks in, and the defaults they land on.
KEYMAP = {
    'left': pg.K_LEFT, 'right': pg.K_RIGHT, 'soft': pg.K_DOWN,
    'hard': pg.K_SPACE, 'hold': pg.K_LSHIFT,
    'ccw': pg.K_z, 'cw': pg.K_x, 'flip': pg.K_a,
}

# One trace per entry: a name, the handling it plays under, and how the script
# leans. Every config includes the settings that change frame counts - DAS,
# ARR, DCD, SDF, ARE - plus the forced drop timer and the finesse rule, since
# a retry moves the piece.
CONFIGS = (
    # name, das, arr, dcd, sdf, are, forced, kicks, finesse, frames, flavour
    ('defaults',  140, 40,  0,  6,   0, 0.0, True,  1, 1500, 'mixed'),
    ('instant',    80,  0,  0, 40,   0, 0.0, True,  1, 1500, 'mixed'),
    # SDF 12 lands on the one rounding Python and C++ disagree about:
    # 30/12 = 2.5, which Python's half-to-even rounds down.
    ('das0',        0, 20,  0, 12,   0, 0.0, True,  1, 1200, 'mixed'),
    ('cutdelay',  160, 40, 40,  8, 100, 0.5, True,  1, 1500, 'mixed'),
    ('forced',    140, 40,  0,  6,   0, 0.3, True,  1, 1800, 'sparse'),
    ('retry',     140, 40,  0,  6,   0, 0.0, True,  2, 1500, 'taps'),
    ('nokicks',   140, 40,  0,  6,  60, 0.0, False, 1, 1200, 'spins'),
    # Deterministic clears: an all-O feed laid out in rows, so the line clear
    # timing is exercised on purpose rather than by luck.
    ('clears',    140, 40,  0,  6,   0, 0.0, True,  1, 1200, 'rows'),
    # Deterministic retries: four taps into the wall and a drop, every piece,
    # so the piece is handed back once per window and locked clean on the
    # second drop. The random scripts only stumble into a retry now and then.
    ('faults',    140, 40,  0,  6,   0, 0.0, True,  2, 1000, 'faultrows'),
    # A fault with the timer running: the handed-back piece keeps the time it
    # already spent, so the forced drop lands a moment later, not a full budget
    # later. Only this trace can tell those two apart.
    ('retryforce', 140, 40, 0,  6,   0, 2.0, True,  2, 1200, 'faultonce'),
    # A held direction cut back by DCD on rotation, deliberately: hold right
    # through the whole window and rotate mid-slide. Without the cut the slide
    # resumes on the ARR beat; with it, a beat later. The random scripts never
    # held a charge into a rotation for long enough to show the difference.
    ('dcdcut',    100, 60, 100, 6,   0, 0.0, True,  1, 900, 'chargecut'),
)


class Clock:
    def __init__(self):
        self.now = 0.

    def __call__(self):
        return self.now


def build_rows_script(frames):
    """Five Os to a row pair, hard dropped into place, round after round.

    Each piece gets a 70 frame window: presses early, the hard drop at 50. The
    windows are wide enough that a piece always spawns inside its own window,
    clears included, so the pattern holds together without predicting frames.
    """
    events = {}
    moves = (
        ('dasleft',), ('left', 'left'), (), ('right', 'right'), ('dasright',),
    )
    window = 0
    piece = 0
    while (window + 1) * 70 < frames:
        start = window * 70
        # Round three skips its last piece, leaving the pair for round four's
        # first piece to finish - a clear arriving from a stale row.
        skip = piece % 5 == 4 and (piece // 5) % 4 == 2
        if not skip:
            plan = moves[piece % 5]
            at = start + 6
            for press in plan:
                if press.startswith('das'):
                    key = 'left' if press == 'dasleft' else 'right'
                    events[at] = (key, 1)
                    events[at + 20] = (key, 0)
                    at += 24
                else:
                    events[at] = (press, 1)
                    events[at + 2] = (press, 0)
                    at += 5
            events[start + 50] = ('hard', 1)
        piece += 1
        window += 1
    return events


def build_faults_script(frames, second_drop=True, window_len=50):
    """Four taps left and a drop - a guaranteed finesse fault - then, if asked,
    a second drop to lock the returned piece where it spawned. Without it the
    returned piece is left to the forced drop timer, which is the only way to
    see whether the retry kept the time the piece had already spent."""
    events = {}
    window = 0
    while (window + 1) * window_len < frames:
        start = window * window_len
        at = start + 4
        for _ in range(4):
            events[at] = ('left', 1)
            events[at + 2] = ('left', 0)
            at += 4
        events[start + 26] = ('hard', 1)   # refused and handed back
        if second_drop:
            events[start + 34] = ('hard', 1)   # locked clean at spawn
        window += 1
    return events


def build_chargecut_script(frames):
    """Hold a direction across the whole window and rotate mid-slide."""
    events = {}
    window = 0
    while (window + 1) * 70 < frames:
        start = window * 70
        side = 'right' if window % 2 == 0 else 'left'
        events[start + 4] = (side, 1)
        events[start + 20] = ('cw', 1)
        events[start + 30] = ('ccw', 1)
        events[start + 48] = ('hard', 1)
        events[start + 52] = (side, 0)
        window += 1
    return events


def build_script(rng, frames, flavour):
    """A frame-stamped input script, at most one event per frame.

    Key releases for the momentary actions - rotations, hard drop, hold - are
    deliberately not emitted: both engines ignore them, and every event costs a
    frame of the one-per-frame input poll.
    """
    events = {}

    def free_slot(start):
        slot = start
        while slot in events:
            slot += 1
        return slot

    weights = {
        'mixed':  (0.10, 0.05, 0.05, 0.05, 0.015),
        'sparse': (0.05, 0.02, 0.03, 0.02, 0.010),
        'taps':   (0.18, 0.02, 0.04, 0.08, 0.010),
        'spins':  (0.08, 0.03, 0.12, 0.04, 0.015),
    }[flavour]
    shift_p, soft_p, spin_p, hard_p, hold_p = weights

    frame = 0
    while frame < frames:
        if frame in events:
            frame += 1
            continue
        roll = rng.random()
        if roll < shift_p:
            key = rng.choice(('left', 'right'))
            events[frame] = (key, 1)
            events[free_slot(frame + rng.randint(2, 30))] = (key, 0)
        elif roll < shift_p + soft_p:
            events[frame] = ('soft', 1)
            events[free_slot(frame + rng.randint(3, 40))] = ('soft', 0)
        elif roll < shift_p + soft_p + spin_p:
            events[frame] = (rng.choice(('ccw', 'cw', 'flip')), 1)
        elif roll < shift_p + soft_p + spin_p + hard_p:
            events[frame] = ('hard', 1)
        elif roll < shift_p + soft_p + spin_p + hard_p + hold_p:
            events[frame] = ('hold', 1)
        frame += 1
    return {f: ev for f, ev in events.items() if f < frames}


def run_trace(name, das, arr, dcd, sdf, are, forced, kicks, finesse, frames, flavour, out):
    rng = random.Random('forcetris-' + name)
    if flavour == 'rows':
        pieces = [1] * 600
        script = build_rows_script(frames)
    elif flavour == 'chargecut':
        pieces = []
        while len(pieces) < 600:
            bag = list(range(7))
            rng.shuffle(bag)
            pieces.extend(bag)
        script = build_chargecut_script(frames)
    elif flavour in ('faultrows', 'faultonce'):
        pieces = []
        while len(pieces) < 600:
            bag = list(range(7))
            rng.shuffle(bag)
            pieces.extend(bag)
        script = build_faults_script(
            frames, second_drop=flavour == 'faultrows',
            window_len=50 if flavour == 'faultrows' else 60)
    else:
        pieces = []
        while len(pieces) < 600:
            bag = list(range(7))
            rng.shuffle(bag)
            pieces.extend(bag)
        script = build_script(rng, frames, flavour)

    clock = Clock()
    G.time.perf_counter = clock
    tetris = G.init(Namespace(debug=False, forced_delay=forced, volume=0., sfx_volume=0.))
    user, core = tetris.user, tetris.game
    user.das, user.arr, user.dcd, user.sdf, user.are = das, arr, dcd, sdf, are
    user.enablekicks = kicks
    user.finesse = finesse
    user.cleartype = us.CLEAR_NAIVE
    user.state = 'game'
    user.gametype = 'free'
    user.reset()

    feed = {'at': 0}

    def deal():
        start = feed['at']
        feed['at'] = start + 7
        return [Shape(form) for form in pieces[start:start + 7]]

    core.gen_shapelist = deal
    core.set_data()

    locks = []
    in_forced = [False]
    real_fallen = core.eval_fallen
    real_forced = core.eval_forced_drop
    frame_at = [0]

    def noting_fallen(posdif):
        locks.append((
            frame_at[0], core.freeshape.form, core.freeshape.state,
            core.freeshape.pos[0], core.freeshape.pos[1], 1 if in_forced[0] else 0))
        return real_fallen(posdif)

    def noting_forced():
        in_forced[0] = True
        try:
            return real_forced()
        finally:
            in_forced[0] = False

    core.eval_fallen = noting_fallen
    core.eval_forced_drop = noting_forced

    out.append('trace {}'.format(name))
    out.append('config {} {} {} {} {} {!r} {} {} 30'.format(
        das, arr, dcd, sdf, are, float(forced), int(kicks), finesse))
    out.append('pieces ' + ' '.join(str(form) for form in pieces))
    for frame in sorted(script):
        key, down = script[frame]
        out.append('ev {} {} {}'.format(frame, key, down))

    last = None
    loss_frame = -1
    ran = 0
    for frame in range(frames):
        frame_at[0] = frame
        clock.now = frame * 0.02
        pg.event.clear()
        if frame in script:
            key, down = script[frame]
            pg.event.post(pg.event.Event(
                pg.KEYDOWN if down else pg.KEYUP, key=KEYMAP[key]))
        core.run()
        ran = frame + 1
        snap = (
            core.freeshape.form, core.freeshape.state,
            core.freeshape.pos[0], core.freeshape.pos[1],
            1 if core.entry_flag else 0)
        if snap != last:
            out.append('p {} {} {} {} {} {}'.format(frame, *snap))
            last = snap
        if user.state in ('loss_menu', 'save_menu'):
            loss_frame = frame
            break

    for lock in locks:
        out.append('lock {} {} {} {} {} {}'.format(*lock))
    out.append('loss {}'.format(loss_frame))
    out.append('frames {}'.format(ran))
    rows = rp.board_to_rows(core.grid)
    out.append('board {}'.format(len(rows)))
    out.extend('row ' + row for row in rows)
    out.append('end')
    return len(locks), loss_frame


def main(path):
    out = ['# forcetris trace 1']
    for config in CONFIGS:
        locks, loss = run_trace(*config, out)
        print('{:>10}: {} locks{}'.format(
            config[0], locks, ', lost at frame {}'.format(loss) if loss >= 0 else ''))
    with open(path, 'w') as handle:
        handle.write('\n'.join(out) + '\n')
    print('{} records -> {}'.format(len(out), path))


if __name__ == '__main__':
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'traces.txt'))
