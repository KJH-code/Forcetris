"""The high score cross check: one file, one meaning, byte for byte.

The C++ side submits a scripted sequence of scores - ties, misses and all -
into a fresh folder; this script replays the very same sequence through the
engine's own SFH.encode into another. The two hiscore.dat files must come
out byte-identical, and the C++ side's announced places must match what
SortedCollection would have announced. Then the direction turns around: the
C++ reader prints the Python-written table and it must say what the Python
decoder says.

Run with: python tools/hiscore_cross.py <hiscorecheck-binary>
"""
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT)

BINARY = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else None

failures = []


def check (name, ok, detail=''):
    print('{} {}{}'.format('PASS' if ok else 'FAIL', name,
                           ' -- ' + detail if detail and not ok else ''))
    if not ok:
        failures.append(name)


# The same scripted submissions hiscorecheck.cpp carries, in the same order.
TURNS = [
    ('free', 'AAAAAAAA', 5000, 20, 12345),
    ('free', 'BBBBBBBB', 7000, 30, 11111),
    ('free', 'CCCCCCCC', 5000, 20, 12345),
    ('free', 'DDDDDDDD', 5000, 20, 12000),
    ('free', 'EEEEEEEE', 5000, 25, 12000),
    ('free', 'ZZZZZZZZ', 0, 0, 0),
    ('free', 'F1      ', 100, 1, 50000),
    ('free', 'F2      ', 200, 2, 40000),
    ('free', 'F3      ', 300, 3, 30000),
    ('free', 'F4      ', 400, 4, 20000),
    ('free', 'F5      ', 500, 5, 10000),
    ('free', 'MISSES  ', 0, 99, 4294967295),
    ('timed', 'TIMEDONE', 9000, 40, 29500),
    ('timed', 'TIMEDTWO', 8000, 35, 30000),
    ('arcade', 'ARCADEON', 12000, 64, 60000),
    ('arcade', 'ARCADETW', 11000, 60, 61000),
    ('arcade', 'ARCADETH', 12000, 64, 60000),
]


def python_places_and_write (folder):
    """Replay TURNS through the engine's own codec, collecting places."""
    os.environ['FORCETRIS_HISCORE'] = folder
    import engine.filehandler as fh
    from engine.sortedcollections import SortedCollection as SC

    places = []
    for gametype, name, score, lines, timer in TURNS:
        with fh.SFH() as sfh:
            g = 0 if gametype == 'arcade' else 1 if gametype == 'timed' else 2
            table = SC(sfh.decode()[g], key=lambda s: (-s[-3], s[-1], s[-2]))
            probe = [name, score, lines, timer]
            table.insert(probe)
            places.append(table.index(probe))
        with fh.SFH() as sfh:
            entry = [c.encode() for c in name] + [score, lines, timer]
            sfh.encode(gametype, entry)
    return places


def python_dump (folder):
    os.environ['FORCETRIS_HISCORE'] = folder
    import engine.filehandler as fh
    lines = []
    with fh.SFH() as sfh:
        tables = sfh.decode()
    for tname, table in zip(('arcade', 'timed', 'free'), tables):
        for name, score, cleared, timer in table:
            shown = '{}:{:02d}:{:02d}'.format(timer // 6000, timer // 100 % 60, timer % 100)
            lines.append('row {} [{}] {} {} {} {}'.format(
                tname, name, score, cleared, timer, shown))
    return lines


def main (binary):
    work = tempfile.mkdtemp()
    cpp_folder = os.path.join(work, 'cpp')
    py_folder = os.path.join(work, 'py')
    os.makedirs(cpp_folder)
    os.makedirs(py_folder)

    result = subprocess.run([binary, 'write', cpp_folder],
                            capture_output=True, text=True)
    check('the C++ side writes its table', result.returncode == 0,
          result.stderr.strip())
    cpp_places = {}
    for line in result.stdout.splitlines():
        parts = line.split()
        if parts and parts[0] == 'place':
            cpp_places[len(cpp_places)] = int(parts[-1])

    py_places = python_places_and_write(py_folder)
    check('the Python side writes its table', True)

    same = all(cpp_places.get(i) == at for i, at in enumerate(py_places))
    check('the announced places agree', same and len(cpp_places) == len(py_places),
          'cpp {} vs py {}'.format(dict(cpp_places), py_places))

    with open(os.path.join(cpp_folder, 'hiscore.dat'), 'rb') as handle:
        cpp_bytes = handle.read()
    with open(os.path.join(py_folder, 'hiscore.dat'), 'rb') as handle:
        py_bytes = handle.read()
    check('the two files are byte-identical',
          cpp_bytes == py_bytes and len(cpp_bytes) == 720,
          'first difference at byte {}'.format(
              next((i for i, (a, b) in enumerate(zip(cpp_bytes, py_bytes))
                    if a != b), min(len(cpp_bytes), len(py_bytes)))))

    with open(os.path.join(cpp_folder, 'back', 'hiscore.bak'), 'rb') as handle:
        check('and so is the backup', handle.read() == py_bytes)

    result = subprocess.run([binary, 'read', py_folder],
                            capture_output=True, text=True)
    check('the C++ side reads the Python file', result.returncode == 0)
    cpp_rows = [line for line in result.stdout.splitlines() if line.startswith('row')]
    check('and prints the table the Python decoder prints',
          cpp_rows == python_dump(py_folder),
          '\n'.join(cpp_rows[:3]))

    # The repair ladder: a truncated data file falls back to the backup.
    with open(os.path.join(py_folder, 'hiscore.dat'), 'wb') as handle:
        handle.write(b'garbage')
    result = subprocess.run([binary, 'read', py_folder],
                            capture_output=True, text=True)
    rows = [line for line in result.stdout.splitlines() if line.startswith('row')]
    check('a mangled file falls back to the backup', rows == cpp_rows)

    print()
    if failures:
        print('{} check(s) failed.'.format(len(failures)))
        raise SystemExit(1)
    print('All checks passed.')


if __name__ == '__main__':
    if BINARY is None or len(sys.argv) != 2:
        print('usage: python tools/hiscore_cross.py <hiscorecheck-binary>')
        raise SystemExit(2)
    main(BINARY)
