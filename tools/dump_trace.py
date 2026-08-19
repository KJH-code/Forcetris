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
import engine.attack as atk
import engine.game as G
import engine.replay as rp
import engine.userstate as us
from engine.shapes import Block, Shape

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
# a retry moves the piece. The spin rule changes nothing about where pieces
# go, only what the placements score, which the traces also grade. The last
# column names a seeded starting board, for the scripts that need a stack
# already standing.
CONFIGS = (
    # name, das, arr, dcd, sdf, are, forced, kicks, finesse, frames, flavour, spinrule, seed
    ('defaults',  140, 40,  0,  6,   0, 0.0, True,  1, 1500, 'mixed', 2, None),
    ('instant',    80,  0,  0, 40,   0, 0.0, True,  1, 1500, 'mixed', 2, None),
    # SDF 12 lands on the one rounding Python and C++ disagree about:
    # 30/12 = 2.5, which Python's half-to-even rounds down.
    ('das0',        0, 20,  0, 12,   0, 0.0, True,  1, 1200, 'mixed', 2, None),
    ('cutdelay',  160, 40, 40,  8, 100, 0.5, True,  1, 1500, 'mixed', 2, None),
    ('forced',    140, 40,  0,  6,   0, 0.3, True,  1, 1800, 'sparse', 2, None),
    ('retry',     140, 40,  0,  6,   0, 0.0, True,  2, 1500, 'taps', 2, None),
    ('nokicks',   140, 40,  0,  6,  60, 0.0, False, 1, 1200, 'spins', 3, None),
    # Deterministic clears: an all-O feed laid out in rows, so the line clear
    # timing is exercised on purpose rather than by luck. Finesse is off here
    # - the only trace where it is - so the rule-zero path is graded too: no
    # counting, no cue, no retry, but the judgement still recorded.
    ('clears',    140, 40,  0,  6,   0, 0.0, True,  0, 1200, 'rows', 2, None),
    # Twelve I pieces straight down a four-wide chimney, each finishing the
    # bottom row: a twelve clear combo, which is what walks the combo cue up
    # its whole ladder and past the cap, and the combo multipliers with it.
    ('combochain', 140, 40, 0,  6,   0, 0.0, True,  1, 700, 'chimney', 2, 'chimney'),
    # Deterministic retries: four taps into the wall and a drop, every piece,
    # so the piece is handed back once per window and locked clean on the
    # second drop. The random scripts only stumble into a retry now and then.
    ('faults',    140, 40,  0,  6,   0, 0.0, True,  2, 1000, 'faultrows', 2, None),
    # A fault with the timer running: the handed-back piece keeps the time it
    # already spent, so the forced drop lands a moment later, not a full budget
    # later. Only this trace can tell those two apart.
    ('retryforce', 140, 40, 0,  6,   0, 2.0, True,  2, 1200, 'faultonce', 2, None),
    # A held direction cut back by DCD on rotation, deliberately: hold right
    # through the whole window and rotate mid-slide. Without the cut the slide
    # resumes on the ARR beat; with it, a beat later. The random scripts never
    # held a charge into a rotation for long enough to show the difference.
    ('dcdcut',    100, 60, 100, 6,   0, 0.0, True,  1, 900, 'chargecut', 2, None),
    # Two quads down the same well: the second extends the back to back chain
    # and empties the board, so one trace grades the b2b bonus, the perfect
    # clear bonus and the combo bookkeeping on known numbers. The third I
    # lands on an empty board and clears nothing, which breaks the combo.
    ('b2bwell',   140, 40,  0,  6,   0, 0.0, True,  1, 440, 'quadwell', 2, 'eightrows'),
    # Gravity locks with a soft drop still held: the only locks whose drop
    # score uses the third-rate distance, and the only ones that can tell a
    # hard drop flag that was never put down. The windows alternate a plain
    # hard drop with a soft drop ridden into the ground until the grace runs
    # out, tower rising all the while.
    ('gravlock',  140, 40,  0, 40,   0, 0.0, True,  1, 1000, 'ride', 2, None),
    # A T walked to the left wall and kicked off it into the hole that
    # finishes the bottom row: the one trace where a clear's last rotation
    # needed a kick, which is what the twist multiplier on the score turns on.
    ('lwall',     140, 40,  0, 40,   0, 0.0, True,  1, 260, 'wallkick', 2, 'lwall'),
    # A clear that empties the bottom row while a band of rubble floats above:
    # the base game's perfect - the bottom row alone - pays out here, and the
    # banner's perfect - the whole board - does not. The one board where the
    # two disagree.
    ('floatclear', 140, 40, 0,  6,   0, 0.0, True,  1, 200, 'plunge', 2, 'floated'),
    # A tucked T on the floor, under the plain T-spin rule for once: soft
    # dropped, slid under a lone corner block, and armed by a rotation the
    # kicks refuse - the engine marks the attempt, not the success, and the
    # verdict has to survive a zero-distance hard drop. A mini T-spin single,
    # so the clear, the b2b from a spin and the banner verdict are all graded
    # on known numbers. The next window's T gets kicked off the rubble and
    # scores nothing - the verdict has to go away again, not stick.
    ('minispin',  140, 40,  0, 40,   0, 0.0, True,  1, 300, 'tslot', 1, 'tslot'),
    # Timed mode down the chimney: the ten-frame entry and gravity, the score
    # multiplier climbing as the clock drains, and the game ending the frame
    # after the clock does.
    ('timedrun',  140, 40,  0,  6,   0, 0.0, True,  1, 400, 'chimney', 2, 'chimney'),
    # The clock running out mid-clear, both ways it can land: on a row-scan
    # resume ('timedcut') and on the frame the clear resolves its score
    # ('timedcusp'). eval_loss runs between the clock tick and the clearing
    # block, so the gameover cue comes before that frame's clear cue - the
    # one ordering timedrun's clock, expiring between clears, never grades.
    ('timedcut',  140, 40,  0,  6,   0, 0.0, True,  1, 200, 'chimney', 2, 'chimney'),
    ('timedcusp', 140, 40,  0,  6,   0, 0.0, True,  1, 200, 'chimney', 2, 'chimney'),
    # Arcade at each garbage tier: the level read off the lines counter, the
    # gravity ramp, the garbage rows pushing the stack - and the piece - up,
    # and each tier's spawn cadence. The holes are dealt to both engines.
    ('arcade64',  140, 40,  0,  6,   0, 0.0, True,  1, 1400, 'chimney', 2, 'chimney'),
    # And one where a piece is parked on the floor when the garbage arrives,
    # so the push that keeps it above the risen stack is on the record.
    ('arcaderide', 140, 40, 0, 40,   0, 0.0, True,  1, 700, 'sitting', 2, 'chimney'),
    ('arcade128', 140, 40,  0,  6,   0, 0.0, True,  1, 800, 'chimney', 2, 'chimney'),
    ('arcade192', 140, 40,  0,  6,   0, 0.0, True,  1, 800, 'chimney', 2, 'chimney'),
    ('arcade256', 140, 40,  0,  6,   0, 0.0, True,  1, 800, 'chimney', 2, 'chimney'),
    # The cascade styles, over seeded rubble that clears keep disturbing: the
    # sticky style drops everything side-connected as one mass, the linked
    # style drops the surviving fragments of each piece by its own links, and
    # every fall is one row per frame with re-clears chaining. Random play on
    # a rubble board makes the clears - and the cascades - constant.
    ('sticky',    140, 40,  0,  6,   0, 0.0, True,  1, 1500, 'mixed', 2, 'rubble'),
    ('linked',    140, 40,  0,  6,   0, 0.0, True,  1, 1500, 'mixed', 2, 'rubble'),
    # Linked cascade in arcade: garbage rows push up mid-clear, their linked
    # chains ride the cascades, and digging them out splits the chain the way
    # the splice always has.
    ('linkedarcade', 140, 40, 0, 6,  0, 0.0, True,  1, 1000, 'chimney', 2, 'chimney'),
    # A clear high up a shelf: the floating band above it reaches exactly one
    # row below the cleared rows, and the settled shelf underneath must not be
    # dragged along by the sticky fill.
    ('stickyshelf', 140, 40, 0, 6,  0, 0.0, True,  1, 300, 'quadwell', 2, 'shelf'),
    # A garbage block left hanging over a cleared row: garbage never floats,
    # so it stays nailed in the air while its normal neighbour falls past it -
    # and a fill that absorbed settled cells would tear it down instead.
    ('stickygarbage', 140, 40, 0, 6, 0, 0.0, True,  1, 300, 'quadwell', 2, 'hung'),
)


# The traces that leave free mode behind: gametype (0 free, 1 timed,
# 2 arcade), the timed clock in milliseconds, and a head start on the lines
# counter so arcade's high levels are within a trace's reach. Timed runs on a
# shortened clock so the expiry - and the loss it causes - happens on the
# record; the four arcade entries pin each garbage tier and its gravity.
MODES = {
    'timedrun':  (1, 4000, 0),
    'timedcut':  (1, 2600, 0),
    'timedcusp': (1, 2740, 0),
    'arcaderide': (2, 300000, 645),
    'linkedarcade': (2, 300000, 630),
    'arcade64':  (2, 300000, 630),
    'arcade128': (2, 300000, 1921),
    'arcade192': (2, 300000, 3841),
    'arcade256': (2, 300000, 6401),
}
GAMETYPE_NAMES = {0: 'free', 1: 'timed', 2: 'arcade'}

# The clear style each trace runs under: naive unless named here. The cascade
# styles change the whole shape of a clear - rows blank in place, what was
# left hanging falls a row per frame, and what lands can clear again.
CLEARTYPES = {
    'sticky': 1,
    'linked': 2,
    'linkedarcade': 2,
    'stickyshelf': 1,
    'stickygarbage': 1,
}


# The seeded boards, by name. Row 21 is the lowest playable row; the floor
# sits below it.
def seed_board (core, which):
    if which == 'eightrows':
        # Eight garbage rows with the last column open: a well four deep twice.
        for y in range(14, 22):
            for x in range(9):
                core.grid.cells[y][x] = Block([x, y], 7, fallen=True)
    elif which == 'chimney':
        # Twelve rows lacking only the four columns a flat I fills. The I
        # falls through the gap to the floor each time, finishing the bottom
        # row, so every drop is a single and the combo never breaks.
        for y in range(10, 22):
            for x in range(10):
                if not 3 <= x <= 6:
                    core.grid.cells[y][x] = Block([x, y], 7, fallen=True)
    elif which == 'shelf':
        # Two full-but-for-the-well rows high up, a lone floater above the
        # band's lower edge, and a settled shelf below with its own hole so
        # nothing else clears. The well is column nine; the I dives down it.
        for y in (12, 13):
            for x in range(9):
                core.grid.cells[y][x] = Block([x, y], 3, fallen=True)
        core.grid.cells[14][0] = Block([0, 14], 3, fallen=True)
        for y in range(15, 22):
            for x in range(1, 10):
                if x != 9 or y == 15:
                    core.grid.cells[y][x] = Block([x, y], 3, fallen=True)
    elif which == 'hung':
        # The bottom row all but done, a garbage block two rows up, and a
        # normal block hanging just beneath it.
        for x in range(9):
            core.grid.cells[21][x] = Block([x, 21], 3, fallen=True)
        core.grid.cells[19][3] = Block([3, 19], 7, fallen=True)
        core.grid.cells[20][3] = Block([3, 20], 3, fallen=True)
    elif which == 'rubble':
        # Seeded rubble that floats: normal-coloured, linkless, and dense
        # enough that most clears leave something hanging to cascade.
        rng = random.Random(20260819)
        for y in range(12, 22):
            for x in range(10):
                if rng.random() < 0.55:
                    core.grid.cells[y][x] = Block([x, y], 3, fallen=True)
    elif which == 'lwall':
        # The bottom row lacking only its wall-side corner, which is exactly
        # the cell the kicked T's foot lands in.
        for x in range(1, 10):
            core.grid.cells[21][x] = Block([x, 21], 7, fallen=True)
    elif which == 'floated':
        # A full-width band floating three rows above an almost-finished
        # bottom row, with a chimney down the middle for the I to fall through.
        for y in (17, 18, 19, 21):
            for x in range(10):
                if not 3 <= x <= 6:
                    core.grid.cells[y][x] = Block([x, y], 7, fallen=True)
    elif which == 'tslot':
        # The bottom row already full - nothing clears until something locks -
        # the row above open exactly where the tucked T's cells land, and one
        # block higher still: the T slides along the full row until that block
        # stops it, exactly where it becomes the third corner. The lock then
        # completes the row above, and both rows go as a mini T-spin double.
        for x in range(10):
            core.grid.cells[21][x] = Block([x, 21], 7, fallen=True)
        for x in (0, 1, 2, 7, 8, 9):
            core.grid.cells[20][x] = Block([x, 20], 7, fallen=True)
        core.grid.cells[19][6] = Block([6, 19], 7, fallen=True)
    core.grid.update()


def build_quadwell_script (frames):
    """Rotate the I upright, auto-shift it to the wall, and drop it."""
    events = {}
    window = 0
    while (window + 1) * 100 <= frames:
        start = window * 100
        events[start + 30] = ('cw', 1)
        events[start + 34] = ('right', 1)
        events[start + 70] = ('right', 0)
        events[start + 78] = ('hard', 1)
        window += 1
    return events


def build_tslot_script (frames):
    """Soft drop the T to the floor, tuck it under the corner block, arm it.

    The instant soft drop sinks the T beside the slot without locking it, one
    tap right tucks it under the corner block - a move no straight drop could
    have made, which is the whole point of a spin - and the CCW is refused by
    every kick in the tight spot, which still counts as the last thing done
    to the piece. The hard drop covers no distance at all. The grace lock
    would fire around thirty frames after the landing, so everything happens
    before it.
    """
    events = {}
    window = 0
    while (window + 1) * 80 <= frames:
        start = window * 80
        if window % 2 == 0:
            # The T: the tucked mini spin single described above.
            events[start + 24] = ('soft', 1)
            events[start + 26] = ('soft', 0)
            events[start + 28] = ('right', 1)
            events[start + 30] = ('right', 0)
            events[start + 34] = ('ccw', 1)
            events[start + 38] = ('hard', 1)
        else:
            # The I, upright, down the column the spin left open: a plain
            # single right after a spin, which is what proves the spin flag
            # went away again - a stale one would keep back to back alive.
            # The taps left also disarm the rotation before the lock.
            events[start + 24] = ('cw', 1)
            events[start + 28] = ('left', 1)
            events[start + 30] = ('left', 0)
            events[start + 32] = ('left', 1)
            events[start + 34] = ('left', 0)
            events[start + 38] = ('soft', 1)
            events[start + 40] = ('soft', 0)
            events[start + 44] = ('hard', 1)
        window += 1
    return events


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
    # The centre piece wastes two presses that cancel out: it lands where it
    # would have anyway, but the presses are on the record - which is what
    # grades the finesse-off rule this trace runs under, since a fault that
    # is neither counted nor heard must still be a fault nowhere else.
    moves = (
        ('dasleft',), ('left', 'left'), ('left', 'right'), ('right', 'right'),
        ('dasright',),
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


def run_trace(name, das, arr, dcd, sdf, are, forced, kicks, finesse, frames, flavour,
              spinrule, seedname, out):
    rng = random.Random('forcetris-' + name)
    if flavour == 'rows':
        pieces = [1] * 600
        script = build_rows_script(frames)
    elif flavour == 'quadwell':
        pieces = [0] * 600
        script = build_quadwell_script(frames)
    elif flavour == 'sitting':
        # Forty-five frame windows, the instant soft drop parking each I on
        # the floor for most of its window: the garbage spawn at frame 300
        # arrives under a parked piece.
        pieces = [0] * 600
        script = {}
        for window in range((frames - 20) // 45):
            start = window * 45
            script[start + 10] = ('soft', 1)
            script[start + 12] = ('soft', 0)
            script[start + 40] = ('hard', 1)
    elif flavour == 'chimney':
        pieces = [0] * 600
        script = {}
        for window in range((frames - 20) // 50):
            start = window * 50
            # A held soft drop on the way down, so the soft delay - re-derived
            # from gravity every frame, which is how arcade's ramp drags it -
            # is part of what the trajectory grades.
            script[start + 14] = ('soft', 1)
            script[start + 26] = ('soft', 0)
            script[start + 30] = ('hard', 1)
    elif flavour == 'ride':
        pieces = [1] * 600
        script = {}
        for window in range(frames // 90):
            start = window * 90
            if window % 2 == 0:
                script[start + 30] = ('hard', 1)
            else:
                # Held through the landing and the whole lock grace: the lock
                # must find the soft drop still down, or the distance is zero.
                script[start + 24] = ('soft', 1)
                script[start + 85] = ('soft', 0)
    elif flavour == 'wallkick':
        pieces = [2] * 600
        script = {}
        for window in range(frames // 80):
            start = window * 80
            script[start + 24] = ('left', 1)
            script[start + 40] = ('left', 0)
            script[start + 44] = ('soft', 1)
            script[start + 46] = ('soft', 0)
            script[start + 50] = ('cw', 1)
            script[start + 54] = ('hard', 1)
    elif flavour == 'plunge':
        pieces = [0] * 600
        script = {
            window * 60 + 30: ('hard', 1)
            for window in range((frames - 20) // 60)}
    elif flavour == 'tslot':
        pieces = [2, 0] * 300
        script = build_tslot_script(frames)
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

    gametype, timer_ms, start_lines = MODES.get(name, (0, 300000, 0))
    cleartype = CLEARTYPES.get(name, us.CLEAR_NAIVE)

    clock = Clock()
    G.time.perf_counter = clock
    tetris = G.init(Namespace(debug=False, forced_delay=forced, volume=0., sfx_volume=0.))
    user, core = tetris.user, tetris.game
    user.das, user.arr, user.dcd, user.sdf, user.are = das, arr, dcd, sdf, are
    user.enablekicks = kicks
    user.finesse = finesse
    user.spinrule = spinrule
    user.cleartype = cleartype
    user.state = 'game'
    user.gametype = GAMETYPE_NAMES[gametype]
    user.reset()

    # The engine reads real elapsed milliseconds off the frame clock; a trace
    # runs on the same twenty even milliseconds the sim counts.
    class SteadyClock:
        @staticmethod
        def get_time():
            return 20

        @staticmethod
        def tick(*ignored):
            return 20

        @staticmethod
        def get_fps():
            return 50.0

    G.env.clock = SteadyClock()

    # Arcade's garbage holes, dealt up front so the sim can be dealt the same
    # hand - the sim never rolls its own dice.
    hole_rng = random.Random('holes-' + name)
    holes = [hole_rng.randrange(10) for _ in range(40)]
    hole_feed = iter(holes)
    import engine.shapes as shp

    class DealtDice:
        @staticmethod
        def randrange(count):
            return next(hole_feed) % count

    shp.random = DealtDice()

    feed = {'at': 0}

    def deal():
        start = feed['at']
        feed['at'] = start + 7
        return [Shape(form) for form in pieces[start:start + 7]]

    core.gen_shapelist = deal
    core.set_data()
    if gametype == 1:
        user.timer = timer_ms
    if start_lines:
        user.lines_cleared = start_lines
    if seedname:
        seed_board(core, seedname)

    locks = []
    scores = []
    places = []
    cues = []
    in_forced = [False]
    real_fallen = core.eval_fallen
    real_forced = core.eval_forced_drop
    real_commit = core.recorder.commit
    real_hold = core.recorder.hold
    frame_at = [0]

    def noting_sound(sound):
        # Every cue the engine fires, by frame. The sim has to fire the same
        # ones in the same order, or its game sounds different from the game.
        cues.append((frame_at[0], sound))

    G.env.play_sound = noting_sound

    def noting_hold(**fields):
        # What the recorder is told about each placement's journey: the press
        # names, the trail of stops, where the piece came from and what the
        # player could see. All of it has to come out of the sim identically,
        # or a replay written by one engine lies when read by the other.
        places.append((
            fields.get('held'), fields.get('stored'),
            list(fields.get('queue') or []),
            1 if fields.get('judged') else 0,
            fields.get('best'),
            list(fields.get('presses') or []),
            [list(stop) for stop in (fields.get('trail') or [])],
        ))
        return real_hold(**fields)

    core.recorder.hold = noting_hold

    def noting_fallen(posdif):
        # The two flags the spin verdict reads, written down at the moment the
        # verdict reads them. Comparing them at every lock grades the flag
        # bookkeeping across every trace, not just where a spin lands: a port
        # that forgets to clear one on a shift, or to raise one on a refused
        # rotation, disagrees at the next lock whether anything scored or not.
        locks.append((
            frame_at[0], core.freeshape.form, core.freeshape.state,
            core.freeshape.pos[0], core.freeshape.pos[1], 1 if in_forced[0] else 0,
            1 if core.rotated_last else 0, 1 if core.user.twist_flag else 0))
        return real_fallen(posdif)

    def noting_forced():
        in_forced[0] = True
        try:
            return real_forced()
        finally:
            in_forced[0] = False

    def noting_commit(user_, grid, label, perfect, sent):
        # The moment Core.run scores a placement: the clearer has finished and
        # the chain counters are final. One of these per lock, in lock order.
        # The score is the game's own arithmetic, which the sim also carries.
        scores.append((
            atk.spin_kind(label), user_.b2b, user_.combo_ctr,
            1 if perfect else 0, sent, user_.score, user_.downstack))
        return real_commit(user_, grid, label, perfect, sent)

    core.eval_fallen = noting_fallen
    core.eval_forced_drop = noting_forced
    core.recorder.commit = noting_commit

    out.append('trace {}'.format(name))
    out.append('config {} {} {} {} {} {!r} {} {} 30 {} {} {} {} {}'.format(
        das, arr, dcd, sdf, are, float(forced), int(kicks), finesse, spinrule,
        gametype, timer_ms, start_lines, cleartype))
    if gametype == 2:
        out.append('holes ' + ' '.join(str(hole) for hole in holes))
    if seedname:
        for row in rp.board_to_rows(core.grid):
            out.append('seed ' + row)
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
        out.append('lock {} {} {} {} {} {} {} {}'.format(*lock))
    for index, place in enumerate(places):
        held, stored, queue, judged, best, presses, trail = place
        out.append('place {} {} {} {} {} {} {} {}'.format(
            index, 1 if held else 0, stored,
            ','.join(str(form) for form in queue) or '-',
            judged, -1 if best is None else best,
            ','.join(presses) or '-',
            ','.join('{}:{}:{}'.format(*stop) for stop in trail) or '-'))
    for index, score in enumerate(scores):
        out.append('score {} {} {} {} {} {} {} {}'.format(index, *score))
    for frame, name in cues:
        out.append('cue {} {}'.format(frame, name))
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
