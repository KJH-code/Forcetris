"""Finesse: the fewest key presses a placement could have been made in.

Finesse is the standard measure of input economy. Every placement reachable by
dropping a piece straight down has a minimum number of presses that gets it
there - rotate the piece, move it across, drop it - and using more than that
minimum is a fault. Tapping left four times where one held key would have
walked the piece to the wall is the classic example: same placement, four times
the work.

The minimum is computed here rather than hard-coded as the usual table, by
searching every route from the spawn position over an empty field. That is what
the published tables are, and deriving it has the advantage of staying correct
if the playfield or the spawn position ever change.

The moves the search is allowed are the ones a single press can make:

    tap left / tap right        one column
    hold left / hold right      all the way to the wall, since auto-shift
                                covers the distance off one press
    rotate cw / ccw / 180       one press each

Soft drop and hard drop are not counted. Soft drop changes no placement the
search can reach, and the hard drop at the end is one press on every placement
alike, so neither tells the two routes apart.

Rotations that would put the piece through a wall are refused rather than
kicked. Wall kicks exist to rescue rotations against the stack, and the tables
every guideline game measures finesse against are built without them; allowing
them here would credit routes no finesse table calls optimal.
"""
try:
	from collections import deque
	from engine.shapes import Shape
except ImportError:
	print("A module must've shat itself:")
	raise

# The playfield the search runs over, and the column a piece starts in. The
# spawn column is read off a real Shape rather than written out again; the width
# matches Grid.set_cells, and the test suite checks the two still agree, since a
# table built against a wider board than the one being played would be quietly
# wrong rather than obviously broken.
WIDTH = 10
SPAWN_X, SPAWN_Y = Shape().pos
SPAWN_STATE = 0

# One entry per piece, filled in on first use. Building all seven costs a couple
# of hundred tiny searches, which is not worth doing at import time for a player
# who never turns finesse on.
_TABLES = {}
# Offsets per form and rotation state, read out of Shape so that the two cannot
# drift apart.
_OFFSETS = {}

def offsets (form, state):
	# Where a piece's four cells sit relative to its centre, in one orientation.
	if (form, state) not in _OFFSETS:
		_OFFSETS[(form, state)] = tuple(sorted(
			(int(block.relpos[0]), int(block.relpos[1])) for block in Shape(form, state)
		))
	return _OFFSETS[(form, state)]

def fits (form, state, x):
	# True if the piece sits inside the walls at this column. Only the walls are
	# tested: finesse is measured on an empty field by definition, so the stack
	# the player has actually built has no say in it.
	return all(0 <= x + dx < WIDTH for dx, dy in offsets(form, state))

def placement (form, state, x):
	"""What the placement *is*, as opposed to how the piece is being held.

	Rotation state is not the answer on its own. An S turned once and an S turned
	three times occupy the same cells, and a vertical I reached clockwise sits
	where the one reached counter-clockwise does, so a search that told those
	apart would be measuring the route rather than the result. Naming a placement
	by the cells it covers folds the duplicates together.
	"""
	cells = offsets(form, state)
	floor = min(dy for dx, dy in cells)
	return frozenset((x + dx, dy - floor) for dx, dy in cells)

# What each move is called, in the order the search should prefer them when two
# routes cost the same. A held key before a tap, and a single turn before a 180,
# because that is the route a player reading the advice would rather be given.
MOVE_NAMES = {
	'das_left': 'Hold Left', 'das_right': 'Hold Right',
	'left': 'Tap Left', 'right': 'Tap Right',
	'ccw': 'Rotate CCW', 'cw': 'Rotate CW', 'flip': 'Rotate 180',
}

def moves (form, state, x):
	# Every state one press can reach from this one, and the name of the press.
	found = []
	for step, tap, das in ((-1, 'left', 'das_left'), (1, 'right', 'das_right')):
		# A held key, which auto-shift walks into the wall off the one press.
		wall = x
		while fits(form, state, wall + step):
			wall += step
		if wall != x:
			found.append((das, state, wall))
		if fits(form, state, x + step) and x + step != wall:
			# A tap. Skipped when one column IS the whole way to the wall, since the
			# two presses would be the same move under two names.
			found.append((tap, state, x + step))
	for turn, name in ((1, 'cw'), (3, 'ccw'), (2, 'flip')):
		spun = (state + turn) % 4
		if fits(form, spun, x):
			found.append((name, spun, x))
	return found

def table (form):
	"""Cheapest route from spawn to every placement this piece can be dropped in.

	Maps a placement to the tuple of presses that gets there. The length of that
	tuple is the minimum the player is held to; the tuple itself is what the
	replay shows them instead of what they actually did.
	"""
	if form in _TABLES:
		return _TABLES[form]
	start = (SPAWN_STATE, SPAWN_X)
	routes = {start: ()}
	queue = deque([start])
	while queue:
		state, x = queue.popleft()
		route = routes[(state, x)]
		for name, spun, moved in moves(form, state, x):
			if (spun, moved) not in routes:
				routes[(spun, moved)] = route + (name,)
				queue.append((spun, moved))
	# Several ways of holding the piece land on the same placement, and the
	# cheapest of them is the one the player is held to. Breadth first hands them
	# over shortest first, so the first route to reach a placement is the one kept.
	best = {}
	for (state, x), route in sorted(routes.items(), key=lambda item: len(item[1])):
		key = placement(form, state, x)
		if key not in best:
			best[key] = route
	_TABLES[form] = best
	return best

def route (form, state, x):
	"""The presses this placement should have taken, or None if it is off the table.

	None means the search never reached it, which on an empty field should not
	happen for any placement a piece can legally be in. A caller that gets one
	should decline to judge rather than invent a verdict.
	"""
	if form > 6:
		return None
	return table(form).get(placement(form, state, x))

def optimal (form, state, x):
	# Fewest presses this placement could have been made in, or None.
	best = route(form, state, x)
	return None if best is None else len(best)

# How much each rotation turns the piece, by press name.
TURNS = {'cw': 1, 'ccw': 3, 'flip': 2}

def follow (form, presses):
	"""Walk a list of presses from spawn, and say where the piece is after each.

	Returns one (state, x) per press, which is what the replay animates: a held
	key is a single step to the wall, a tap is a single column, and a rotation
	turns on the spot. A press the walls refuse leaves the piece where it was,
	so the sequence is always as long as the presses that produced it.
	"""
	state, x = SPAWN_STATE, SPAWN_X
	stops = []
	for name in presses:
		if name in TURNS:
			spun = (state + TURNS[name]) % 4
			if fits(form, spun, x):
				state = spun
		elif name in ('left', 'right'):
			step = -1 if name == 'left' else 1
			if fits(form, state, x + step):
				x += step
		elif name in ('das_left', 'das_right'):
			step = -1 if name == 'das_left' else 1
			while fits(form, state, x + step):
				x += step
		stops.append((state, x))
	return stops

# The same moves in as few characters as they can be told apart in, for the line
# that has to show a whole placement's worth of them side by side.
SHORT_NAMES = {
	'das_left': 'Hold L', 'das_right': 'Hold R',
	'left': 'L', 'right': 'R',
	'ccw': 'CCW', 'cw': 'CW', 'flip': '180',
}

def describe (presses):
	# A route, or a player's actual presses, written out for the replay screen.
	return ' + '.join(MOVE_NAMES.get(name, name) for name in presses) if presses else 'nothing'

def brief (presses):
	# The same, compact enough to sit on one line next to a label.
	return ', '.join(SHORT_NAMES.get(name, name) for name in presses) if presses else 'nothing'
