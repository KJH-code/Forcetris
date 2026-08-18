"""Recording a game so it can be watched back, and the analysis that comes with it.

A replay here is a list of placements, not a list of keystrokes. Keystrokes would
be smaller, but replaying them means re-simulating gravity, auto-shift and the
bag, and a replay that drifts from the game it recorded is worse than no replay.
Each entry carries what the piece was, where it ended up, what the player pressed
to get it there, where the piece stood after each of those presses, and a
snapshot of the board once the clear had resolved.

So playback shows boards rather than deriving them, and walks the piece over the
top of them along the trail that was recorded. The result is a re-enactment
rather than a simulation: what settled is what settled, and the piece is seen
getting there the way it actually did.

That is also what makes the corrected view honest. Correcting a player's finesse
swaps the trail for the finesse route - the same piece arriving in the same
column in the same orientation, stopping fewer times on the way. The placement
does not move, so the boards in a corrected replay are the same boards and the
score is the same score. Only the journey is different, which is the only thing
better finesse would have changed.

Files live in data/replays as JSON. They are written once when a game ends and
never edited, and the newest handful are kept.
"""
try:
	import os
	import json
	import time
	import datetime
	import engine.finesse as fin
	import engine.attack as atk
except ImportError:
	print("A module must've shat itself:")
	raise

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Overridable for the same reason the settings file is: a test run must not be
# able to fill the player's own replay folder.
FOLDER = os.environ.get('FORCETRIS_REPLAYS') or os.path.join(ROOT, 'data', 'replays')

# How many replays to keep. Old ones are pruned oldest first when a new one
# lands, because a trainer played daily would otherwise never stop growing.
KEEP = 30

# Games shorter than this are not worth a file. Mostly this catches a game
# started and immediately abandoned.
MIN_PLACEMENTS = 5

# Bumped when the shape of the file changes. Version 2 added the movement trail,
# without which a replay can be read but not re-enacted; version 3 added the
# queue and the held piece.
FORMAT = 3
# The oldest version still worth reading. Fields a older file does not carry come
# back as None and the screens do without them, which costs nothing and means an
# upgrade does not throw away the replays already on disk. A file from the future
# is declined outright, since there is no telling what its fields mean.
MIN_FORMAT = 2

# What a clear of each size is called, matching the banner the game puts up.
CLEAR_NAMES = {1: 'Single', 2: 'Double', 3: 'Triple', 4: 'Quad'}

# Rows in the matrix a snapshot covers, floor excluded. Read off a real Grid at
# import time would need a display, so it is written out and checked by the tests.
HEIGHT = 22

def board_to_rows (grid):
	"""The matrix as one string per row: a colour digit per block, a dot for none.

	Only from the highest occupied row down. A stack twelve deep is written as
	twelve rows rather than twenty-two, which is most of the size of a replay
	file, since most of a Tetris board is empty most of the time. The reader pads
	the top back out, so what is stored is shorter but not different.
	"""
	rows = [
		''.join('.' if cell is None else str(cell.color) for cell in row)
		for row in grid.cells[:len(grid.cells) - 1]
	]
	while rows and not rows[0].strip('.'):
		rows.pop(0)
	return rows

def padded (rows, height=HEIGHT):
	# A snapshot back at full height, empty rows on top.
	rows = rows or []
	wide = len(rows[0]) if rows else 10
	return ['.' * wide] * max(0, height - len(rows)) + list(rows)

class Placement:
	"""One piece going down, and everything worth saying about it afterwards."""
	__slots__ = (
		'form', 'state', 'x', 'y', 'held', 'presses', 'trail', 'best', 'judged',
		'forced', 'lines', 'spin', 'perfect', 'combo', 'b2b', 'score', 'elapsed',
		'rows', 'queue', 'stored', 'attack',
	)

	def __init__ (self, **fields):
		for name in self.__slots__:
			setattr(self, name, fields.get(name))

	@property
	def wasted (self):
		# Presses thrown away, or 0 for a placement finesse had no opinion about.
		if not self.judged or self.best is None:
			return 0
		return max(0, len(self.presses) - self.best)

	@property
	def fault (self):
		return self.wasted > 0

	def route (self):
		# What the presses should have been. None when the placement is off the
		# tables, which is the same set of placements that go unjudged.
		return fin.route(self.form, self.state, self.x) if self.judged else None

	def steps (self, fixed=False):
		"""Where the piece stands at each stage of being placed.

		A list of (state, x, y), beginning at spawn and ending where the piece
		locked, which is what the replay walks the piece through.

		With `fixed` on, a judged placement is re-enacted along the finesse route
		instead: the same piece arriving in the same column in the same
		orientation, having stopped fewer times on the way. A placement that was
		never judged - a tuck, a spin, one the timer took - has no route to follow,
		so it keeps the player's own path. Those are the placements finesse has no
		opinion about, and inventing one for them would be inventing a mistake.
		"""
		spawn = (fin.SPAWN_STATE, fin.SPAWN_X, fin.SPAWN_Y)
		landed = (self.state, self.x, self.y)
		best = self.route() if fixed else None
		if best is not None:
			# The route is walked over an empty field, which is where finesse is
			# measured, so every stop on it is at the spawn row. The piece then falls
			# the whole way, which is exactly what made this placement judgeable.
			path = [(state, x, fin.SPAWN_Y) for state, x in fin.follow(self.form, best)]
		else:
			path = [tuple(stop) for stop in (self.trail or []) if len(stop) == 3]
		stops = [spawn] + path
		if stops[-1] != landed:
			stops.append(landed)
		return stops

	def presses_shown (self, fixed=False):
		# The presses the screen names, matching whichever path it is animating.
		best = self.route() if fixed else None
		return list(best) if best is not None else list(self.presses or [])

	def to_dict (self):
		return {name: getattr(self, name) for name in self.__slots__}

	@classmethod
	def from_dict (cls, data):
		return cls(**data)

class Replay:
	"""A finished game: what it was played with, and every placement in it."""

	def __init__ (self, meta=None, placements=None, path=None):
		self.meta = meta or {}
		self.placements = placements or []
		self.path = path

	def __len__ (self):
		return len(self.placements)

	def before (self, index):
		# The board a placement was made onto: whatever the one before it left.
		return self.placements[index - 1].rows if 0 < index < len(self.placements) else []

	def title (self):
		# What the list screen shows for this file.
		stamp = self.meta.get('played', '')
		return '{}  {}  {} pts'.format(
			stamp[:16].replace('T', ' ') or 'unknown',
			self.meta.get('gametype', '?').capitalize(),
			self.meta.get('score', 0),
		)

	def summary (self, fixed=False):
		"""The numbers the analysis screen shows.

		With `fixed` on, the placements are counted as if every one had been made
		in the fewest presses it could have been - which is what the corrected
		replay is showing. Nothing else about the game changes, because nothing
		else about the game would have.
		"""
		judged = [p for p in self.placements if p.judged]
		faults = 0 if fixed else sum(1 for p in judged if p.fault)
		wasted = 0 if fixed else sum(p.wasted for p in judged)
		spent = sum(
			(p.best if fixed and p.best is not None else len(p.presses))
			for p in self.placements
		)
		seconds = self.meta.get('seconds', 0.) or 0.
		# Attack is what it is however the presses went: the corrected view changes
		# the journey, never the placements, so these do not take `fixed`.
		attack = sum(p.attack or 0 for p in self.placements)
		downstack = self.meta.get('downstack', 0) or 0
		clears = {}
		spins = 0
		perfects = 0
		for p in self.placements:
			if p.lines:
				clears[p.lines] = clears.get(p.lines, 0) + 1
			if p.spin:
				spins += 1
			if p.perfect:
				perfects += 1
		return {
			'placements': len(self.placements),
			'judged': len(judged),
			'faults': faults,
			'wasted': wasted,
			'presses': spent,
			'rate': 1. if not judged else float(len(judged) - faults) / len(judged),
			'ppp': 0. if not self.placements else float(spent) / len(self.placements),
			'pps': 0. if seconds <= 0 else len(self.placements) / seconds,
			'lines': sum(p.lines or 0 for p in self.placements),
			'score': self.meta.get('score', 0),
			'seconds': seconds,
			'clears': clears,
			'spins': spins,
			'perfects': perfects,
			'best_b2b': max([p.b2b or 0 for p in self.placements] or [0]),
			'best_combo': max([p.combo or 0 for p in self.placements] or [0]),
			'attack': attack,
			'apm': atk.apm(attack, seconds),
			'vs': atk.vs_score(attack, downstack, seconds),
		}

	def to_dict (self):
		return {
			'format': FORMAT,
			'meta': self.meta,
			'placements': [p.to_dict() for p in self.placements],
		}

	@classmethod
	def from_dict (cls, data, path=None):
		if not isinstance(data, dict):
			return None
		version = data.get('format')
		if not isinstance(version, int) or not MIN_FORMAT <= version <= FORMAT:
			return None
		rows = data.get('placements')
		if not isinstance(rows, list):
			return None
		try:
			placements = [Placement.from_dict(row) for row in rows if isinstance(row, dict)]
		except (TypeError, ValueError):
			return None
		return cls(data.get('meta') or {}, placements, path)

class Recorder:
	"""Collects placements while a game is being played."""

	def __init__ (self):
		self.clear()

	def clear (self):
		self.meta = {}
		self.placements = []
		self.pending = None
		self.started = None

	def begin (self, user):
		# Called when a game starts, so the settings recorded are the ones it was
		# actually played under rather than whatever they were changed to later.
		self.clear()
		self.started = time.perf_counter()
		self.meta = {
			'played': datetime.datetime.now().replace(microsecond=0).isoformat(),
			'gametype': user.gametype,
			'forced_delay': user.forced_delay,
			'finesse': user.finesse,
			'spinrule': user.spinrule,
			'cleartype': user.cleartype,
			'das': user.das, 'arr': user.arr, 'dcd': user.dcd,
			'sdf': user.sdf, 'are': user.are,
		}

	def hold (self, **fields):
		# Stashed when the piece locks, because what it went on to clear is not
		# known for several frames after that.
		fields['elapsed'] = round(
			0. if self.started is None else time.perf_counter() - self.started, 2)
		self.pending = fields

	def commit (self, user, grid, spin='', perfect=False, attack=0):
		# Called once the line clearer has finished and the board has settled.
		if self.pending is None:
			return
		fields = self.pending
		self.pending = None
		self.placements.append(Placement(
			lines=sum(user.line_list) if user.line_list else 0,
			spin=spin,
			attack=attack,
			perfect=bool(perfect),
			combo=max(0, user.combo_ctr - 1),
			b2b=max(0, user.b2b - 1),
			score=user.score,
			rows=board_to_rows(grid),
			**fields
		))

	def finish (self, user):
		# Stamp the totals and hand back a Replay, or None if there is nothing
		# here worth keeping.
		self.pending = None
		if len(self.placements) < MIN_PLACEMENTS:
			return None
		self.meta['score'] = user.score
		self.meta['lines'] = user.lines_cleared
		self.meta['downstack'] = user.downstack
		self.meta['seconds'] = round(
			0. if self.started is None else time.perf_counter() - self.started, 2)
		return Replay(dict(self.meta), list(self.placements))

def filename (replay):
	# Sortable, and free of anything Windows refuses in a path.
	stamp = (replay.meta.get('played') or '').replace(':', '').replace('-', '').replace('T', '-')
	return '{}-{}.json'.format(stamp or 'replay', replay.meta.get('gametype', 'free'))

def save (replay):
	# Best effort, like every other write in the game: a replay that cannot be
	# stored is not a reason to take the game down at the moment it ends.
	try:
		os.makedirs(FOLDER, exist_ok=True)
		path = os.path.join(FOLDER, filename(replay))
		with open(path, 'w') as out:
			json.dump(replay.to_dict(), out, separators=(',', ':'))
		replay.path = path
		prune()
		return path
	except (IOError, OSError):
		return None

def prune ():
	# Keep the newest KEEP files and drop the rest.
	try:
		files = sorted(
			name for name in os.listdir(FOLDER) if name.endswith('.json'))
		for name in files[:-KEEP] if len(files) > KEEP else []:
			os.remove(os.path.join(FOLDER, name))
	except (IOError, OSError):
		pass

def listing ():
	# Saved replays, newest first. A file that will not parse is left out rather
	# than allowed to break the screen that lists them.
	found = []
	try:
		names = sorted(os.listdir(FOLDER), reverse=True)
	except (IOError, OSError):
		return found
	for name in names:
		if not name.endswith('.json'):
			continue
		replay = load(os.path.join(FOLDER, name))
		if replay is not None:
			found.append(replay)
	return found

def load (path):
	try:
		with open(path, 'r') as source:
			return Replay.from_dict(json.load(source), path)
	except (IOError, ValueError):
		return None
