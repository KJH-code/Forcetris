"""The replay cross check: both engines must read a replay the same way.

Two directions, both against real files:

  C++ -> Python   the replaycheck binary plays a scripted game through the
                  sim, saves the replay, and dumps what it saved. This script
                  loads that file with engine/replay.py and dumps it the same
                  way. The dumps must agree, so a file written by the C++
                  game means exactly the same thing to the Python screens.

  Python -> C++   a scripted game is played through the Python engine and its
                  replay saved as the game itself would save it. replaycheck
                  loads that file and dumps it; this script dumps its own
                  reading. Agreement means the C++ viewer re-enacts a Python
                  game move for move, corrected finesse included.

The dump names every field of every placement, every step of the
re-enactment with the correction off and on, and both summaries, so nothing
about a replay is taken on faith in either direction.

Run with: python tools/replay_cross.py <replaycheck-binary>
"""
import math
import os
import subprocess
import sys
import tempfile

os.environ.setdefault('SDL_VIDEODRIVER', 'dummy')
os.environ.setdefault('SDL_AUDIODRIVER', 'dummy')
os.environ.setdefault('FORCETRIS_CONFIG', os.path.join(tempfile.mkdtemp(), 'settings.json'))
PY_REPLAYS = os.path.join(tempfile.mkdtemp(), 'replays')
os.environ['FORCETRIS_REPLAYS'] = PY_REPLAYS

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)
sys.path.insert(0, os.path.join(ROOT, 'tools'))

# Resolved before dump_trace's import moves the working directory.
BINARY = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None

import random
from argparse import Namespace

import pygame as pg
import dump_trace as dt
import engine.game as G
import engine.replay as rp
import engine.userstate as us

pg.key.get_focused = lambda: True

failures = []


def check (name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name,
                           ' -- ' + detail if detail and not ok else ''))
    if not ok:
        failures.append(name)


def joined (parts):
    parts = list(parts)
    return ','.join(str(part) for part in parts) if parts else '-'


def number (value):
    return '{:.9g}'.format(float(value))


def stops_joined (stops):
    return joined('{}:{}:{}'.format(*stop) for stop in stops)


_screens = {}


def analysis_menu ():
    """The game's own analysis screen, built once and re-shown per replay."""
    if 'menu' not in _screens:
        tetris = G.init(Namespace(
            debug=False, forced_delay=0., volume=0., sfx_volume=0.))
        _screens['menu'] = tetris.game.loss_menu.analysis_menu
    return _screens['menu']


def dump_summary (out, summary, fixed):
    clears = sorted(summary['clears'].items())
    out.append('summary {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}'.format(
        1 if fixed else 0,
        summary['placements'], summary['judged'], summary['faults'],
        summary['wasted'], summary['presses'], summary['lines'],
        summary['score'], summary['spins'], summary['perfects'],
        summary['best_b2b'], summary['best_combo'], summary['attack'],
        number(summary['rate']), number(summary['ppp']), number(summary['pps']),
        number(summary['apm']), number(summary['vs']), number(summary['seconds']),
        ) + ' ' + joined('{}:{}'.format(size, count) for size, count in clears))


def dump_replay (replay):
    """The canonical dump, mirrored line for line by replaycheck.cpp."""
    meta = replay.meta
    out = []
    out.append('meta {} {} {} {} {} {} {} {} {} {} {} {} {} {}'.format(
        meta.get('gametype', 'free'), meta.get('score', 0), meta.get('lines', 0),
        meta.get('downstack', 0), number(meta.get('seconds', 0.) or 0.),
        meta.get('das', 0), meta.get('arr', 0), meta.get('dcd', 0),
        meta.get('sdf', 0), meta.get('are', 0), meta.get('finesse', 0),
        meta.get('spinrule', 0), meta.get('cleartype', 0),
        number(meta.get('forced_delay', 0.) or 0.)))
    for i, place in enumerate(replay.placements):
        spin = (place.spin or '').replace(' ', '_')
        out.append('place {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {} {}'.format(
            i, place.form, place.state, place.x, place.y,
            1 if place.held else 0, 1 if place.forced else 0,
            1 if place.judged else 0,
            -1 if place.best is None else place.best,
            place.lines or 0, spin or '-', 1 if place.perfect else 0,
            place.combo or 0, place.b2b or 0, place.score or 0,
            place.attack or 0,
            7 if place.stored is None else place.stored,
            joined(place.queue or []),
            number(place.elapsed or 0.), place.wasted))
        out.append('presses {} {}'.format(i, joined(place.presses or [])))
        out.append('rows {} {}'.format(i, joined(place.rows or [])))
        out.append('steps {} {}'.format(i, stops_joined(place.steps(False))))
        out.append('fixedsteps {} {}'.format(i, stops_joined(place.steps(True))))
        out.append('shown {} {}'.format(i, joined(place.presses_shown(False))))
        out.append('fixedshown {} {}'.format(i, joined(place.presses_shown(True))))
    dump_summary(out, replay.summary(False), False)
    dump_summary(out, replay.summary(True), True)
    # The analysis screen's rows, off the real screen rather than off a
    # second rendering of them: what the player reads is what is graded.
    menu = analysis_menu()
    menu.show(replay)
    for i, (name, value) in enumerate(menu.rows()):
        out.append('analysis {} {}|{}'.format(i, name, value))
    return out


def read_dump (path):
    with open(path) as source:
        return [line.rstrip('\n') for line in source if line.strip()]


def compare (name, py_lines, cpp_lines):
    """Token-by-token, numbers within tolerance, everything else exactly."""
    if len(py_lines) != len(cpp_lines):
        check(name, False, 'python dumped {} lines, C++ {}'.format(
            len(py_lines), len(cpp_lines)))
        return
    for py_line, cpp_line in zip(py_lines, cpp_lines):
        if py_line.startswith('analysis') or cpp_line.startswith('analysis'):
            # Rendered text, not numbers: '2.5' and '2.50' are the same
            # figure and a different screen, so these lines match exactly or
            # not at all.
            if py_line != cpp_line:
                check(name, False, 'analysis row differs:\n  py:  {}\n  cpp: {}'
                      .format(py_line, cpp_line))
                return
            continue
        py_parts = py_line.split()
        cpp_parts = cpp_line.split()
        if len(py_parts) != len(cpp_parts):
            check(name, False, 'token count differs:\n  py: {}\n  cpp: {}'.format(
                py_line, cpp_line))
            return
        for py_token, cpp_token in zip(py_parts, cpp_parts):
            if py_token == cpp_token:
                continue
            try:
                close = math.isclose(float(py_token), float(cpp_token),
                                     rel_tol=1e-9, abs_tol=1e-9)
            except ValueError:
                close = False
            if not close:
                check(name, False, 'differs:\n  py:  {}\n  cpp: {}'.format(
                    py_line, cpp_line))
                return
    check(name, True)


def play_python_replica ():
    """The exact game replaycheck's write mode plays, through the real engine.

    Same seed, same feed, same frame-stamped events. The file the Python
    recorder writes for it must match the file the sim's recorder wrote,
    field for field - which is what proves the C++ recorder writes down what
    the engine would have, not merely something well-formed.
    """
    clock = dt.Clock()
    G.time.perf_counter = clock
    tetris = G.init(Namespace(debug=False, forced_delay=0., volume=0., sfx_volume=0.))
    user, core = tetris.user, tetris.game
    user.das, user.arr, user.dcd, user.sdf, user.are = 140, 40, 0, 40, 0
    user.enablekicks = True
    user.finesse = 1
    user.spinrule = us.SPIN_ALL_MINI
    user.cleartype = us.CLEAR_NAIVE
    user.state = 'game'
    user.gametype = 'free'
    user.reset()

    pieces = [0] * 60
    pieces[3] = 1
    pieces[16] = 2
    feed = {'at': 0}

    def deal():
        start = feed['at']
        feed['at'] = start + 7
        return [G.Shape(form) for form in pieces[start:start + 7]]

    core.gen_shapelist = deal
    core.set_data()
    core.recorder.meta['played'] = '2026-08-18T12:00:00'
    for y in range(10, 22):
        for x in range(10):
            if not 3 <= x <= 6:
                core.grid.cells[y][x] = dt.Block([x, y], 7, fallen=True)
    core.grid.update()

    events = {}
    for window in range(12):
        start = window * 60
        if window in (1, 2):
            events[start + 20] = ('hold', 1)
        if window == 5:
            events[start + 24] = ('left', 1)
            events[start + 26] = ('left', 0)
            events[start + 28] = ('right', 1)
            events[start + 30] = ('right', 0)
        events[start + 40] = ('hard', 1)
    events[720 + 20] = ('right', 1)
    events[720 + 40] = ('right', 0)
    events[720 + 44] = ('hard', 1)
    events[780 + 20] = ('left', 1)
    events[780 + 22] = ('left', 0)
    events[780 + 24] = ('left', 1)
    events[780 + 26] = ('left', 0)
    events[780 + 40] = ('hard', 1)
    events[840 + 20] = ('cw', 1)
    events[840 + 40] = ('hard', 1)
    events[900 + 14] = ('left', 1)
    events[900 + 30] = ('left', 0)
    events[900 + 34] = ('soft', 1)
    events[900 + 36] = ('soft', 0)
    events[900 + 40] = ('cw', 1)
    events[900 + 44] = ('hard', 1)

    for frame in range(1000):
        clock.now = frame * 0.02
        pg.event.clear()
        if frame in events:
            key, down = events[frame]
            pg.event.post(pg.event.Event(
                pg.KEYDOWN if down else pg.KEYUP, key=dt.KEYMAP[key]))
        core.run()
        if user.state in ('loss_menu', 'save_menu'):
            break
    clock.now = 1000 * 0.02
    replay = core.recorder.finish(user)
    if replay is None:
        return None
    return rp.save(replay)


def play_python_game ():
    """A seeded mixed game through the real engine, saved as the game saves it."""
    rng = random.Random('forcetris-cross')
    pieces = []
    while len(pieces) < 600:
        bag = list(range(7))
        rng.shuffle(bag)
        pieces.extend(bag)
    script = dt.build_script(rng, 1500, 'mixed')

    clock = dt.Clock()
    G.time.perf_counter = clock
    tetris = G.init(Namespace(debug=False, forced_delay=0., volume=0., sfx_volume=0.))
    user, core = tetris.user, tetris.game
    user.das, user.arr, user.dcd, user.sdf, user.are = 140, 40, 0, 6, 0
    user.enablekicks = True
    user.finesse = 1
    user.spinrule = us.SPIN_ALL
    user.cleartype = us.CLEAR_NAIVE
    user.state = 'game'
    user.gametype = 'free'
    user.reset()

    feed = {'at': 0}

    def deal():
        start = feed['at']
        feed['at'] = start + 7
        return [G.Shape(form) for form in pieces[start:start + 7]]

    core.gen_shapelist = deal
    core.set_data()

    for frame in range(1500):
        clock.now = frame * 0.02
        pg.event.clear()
        if frame in script:
            key, down = script[frame]
            pg.event.post(pg.event.Event(
                pg.KEYDOWN if down else pg.KEYUP, key=dt.KEYMAP[key]))
        core.run()
        if user.state in ('loss_menu', 'save_menu'):
            # eval_loss has already finished and saved the recording.
            break
    else:
        replay = core.recorder.finish(user)
        if replay is not None:
            rp.save(replay)
    saved = rp.listing()
    return saved[0].path if saved else None


def main (binary):
    workdir = tempfile.mkdtemp()

    # C++ -> Python.
    cpp_folder = os.path.join(workdir, 'cpp-replays')
    cpp_dump = os.path.join(workdir, 'cpp-write.dump')
    result = subprocess.run(
        [binary, 'write', cpp_folder, cpp_dump],
        capture_output=True, text=True)
    check('the C++ side writes a replay', result.returncode == 0,
          result.stderr.strip())
    if result.returncode == 0:
        path = result.stdout.strip().splitlines()[-1]
        loaded = rp.load(path)
        check('the Python engine reads the C++ file', loaded is not None, path)
        if loaded is not None:
            check('it is worth reading',
                  len(loaded) >= rp.MIN_PLACEMENTS
                  and any(p.lines for p in loaded.placements)
                  and any(p.held for p in loaded.placements)
                  and any(p.fault for p in loaded.placements)
                  and any(p.perfect for p in loaded.placements),
                  '{} placements'.format(len(loaded)))
            compare('the two engines agree on the C++ file',
                    dump_replay(loaded), read_dump(cpp_dump))
            # The strongest direction: the Python engine plays the very same
            # scripted game, and its recorder's file must match the sim's.
            replica_path = play_python_replica()
            check('the Python engine plays the same game', replica_path is not None)
            if replica_path is not None:
                replica = rp.load(replica_path)
                check('and its recording loads', replica is not None)
                if replica is not None:
                    compare('the two recorders wrote the same game down',
                            dump_replay(replica), dump_replay(loaded))

    # Python -> C++.
    py_path = play_python_game()
    check('the Python engine saves a replay', py_path is not None)
    if py_path is not None:
        py_loaded = rp.load(py_path)
        check('and can read it back', py_loaded is not None)
        rich = (py_loaded is not None and len(py_loaded) >= rp.MIN_PLACEMENTS
                and any(p.judged for p in py_loaded.placements)
                and any(p.presses for p in py_loaded.placements))
        check('and it is worth reading', rich,
              '{} placements'.format(0 if py_loaded is None else len(py_loaded)))
        cpp_read = os.path.join(workdir, 'cpp-read.dump')
        result = subprocess.run(
            [binary, 'read', py_path, cpp_read], capture_output=True, text=True)
        check('the C++ side reads the Python file', result.returncode == 0,
              result.stderr.strip())
        if py_loaded is not None and result.returncode == 0:
            compare('the two engines agree on the Python file',
                    dump_replay(py_loaded), read_dump(cpp_read))

    # Synthetic: the corners of the format a played game may not visit -
    # spin labels of both kinds, a judgement stripped from a placement that
    # has a route (so the corrected view must keep its player path), a best
    # left behind on an unjudged one, a missing queue and hold the way a
    # version 2 file lacks them, a forced held placement - written into one
    # file both sides must still agree on.
    if py_loaded is not None and len(py_loaded) >= 5:
        lab = py_loaded
        lab.meta = dict(lab.meta)
        lab.meta['played'] = '2026-08-18T13:00:00'
        lab.meta['downstack'] = 5
        lab.placements[0].spin = 'MINI T-SPIN'
        lab.placements[0].perfect = True
        lab.placements[1].spin = 'Z-SPIN'
        lab.placements[1].b2b = 3
        lab.placements[1].attack = 7
        # A placement that was judged - so its position is on the finesse
        # table - with the judgement stripped: steps(fixed) must fall back to
        # the recorded trail rather than inventing the route.
        judged_at = [i for i, p in enumerate(lab.placements) if p.judged]
        check('the game left something judged to corrupt', bool(judged_at))
        if judged_at:
            stripped = lab.placements[judged_at[0]]
            stripped.judged = False
            stripped.best = None
        # And the reverse corruption: unjudged but with a best left in, on a
        # placement whose best differs from its press count, so the corrected
        # totals can tell which one a summary reads.
        confused = next(
            (p for i, p in enumerate(lab.placements)
             if i != judged_at[0] and p.judged and p.wasted > 0), None)
        check('the game left a fault to corrupt', confused is not None)
        if confused is not None:
            confused.judged = False
        lab.placements[3].queue = None
        lab.placements[3].stored = None
        lab.placements[4].held = True
        lab.placements[4].forced = True
        lab_path = rp.save(lab)
        check('the synthetic file saves', lab_path is not None)
        if lab_path is not None:
            lab_loaded = rp.load(lab_path)
            check('and reads back', lab_loaded is not None)
            lab_read = os.path.join(workdir, 'cpp-lab.dump')
            result = subprocess.run(
                [binary, 'read', lab_path, lab_read],
                capture_output=True, text=True)
            check('the C++ side reads the synthetic file',
                  result.returncode == 0, result.stderr.strip())
            if lab_loaded is not None and result.returncode == 0:
                compare('the two engines agree on the synthetic file',
                        dump_replay(lab_loaded), read_dump(lab_read))

    print()
    if failures:
        print('{} check(s) failed.'.format(len(failures)))
        raise SystemExit(1)
    print('All checks passed.')


if __name__ == '__main__':
    if BINARY is None or len(sys.argv) != 2:
        print('usage: python tools/replay_cross.py <replaycheck-binary>')
        raise SystemExit(2)
    main(BINARY)
