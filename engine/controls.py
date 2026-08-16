"""The saved profile: key bindings, handling, and the game settings.

Only gameplay keys are reboundable. Menu navigation stays fixed on the arrow
keys, Z, X, Enter and Escape, because it is the only route back to the screen
that does the rebinding - a player who bound every menu key to something else
would have no way to undo it.

Bindings live on the User object as `keys`, mapping an action name to a tuple of
key codes. An action may hold several codes, which is how the defaults keep both
halves of the Z/Left Ctrl and X/Up pairs the base game shipped with. Rebinding
replaces the whole tuple with the single key that was pressed.
"""
try:
	import os
	import json
	import pygame as pg
	import engine.userstate as us
except ImportError:
	print("A module must've shat itself:")
	raise

# Same anchor as engine.environment.ROOT, recomputed here to keep this module
# free of anything that needs a display surface.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# Overridable so that a test run cannot read or overwrite the player's own profile,
# and so anyone who wants two profiles can have them.
CONFIG = os.environ.get('FORCETRIS_CONFIG') or os.path.join(ROOT, 'data', 'settings.json')
# What the file was called back when it only held bindings. Read if the current one
# is missing, then left alone, so an upgrade keeps the bindings already set. Skipped
# entirely when a profile was named explicitly - that names the only file to read.
LEGACY = None if os.environ.get('FORCETRIS_CONFIG') else os.path.join(ROOT, 'data', 'controls.json')

# The settings the menu can change, each with the rule that decides whether a value
# read back off disk is usable. A hand-edited or outdated file must not be able to
# put the game into a state its own menus cannot express.
SETTINGS = (
	('forced_delay', lambda v: max(0., min(60., float(v)))),
	('volume', lambda v: max(0., min(1., float(v)))),
	('sfx_volume', lambda v: max(0., min(1., float(v)))),
	('cleartype', lambda v: max(0, min(2, int(v)))),
	('spinrule', lambda v: max(0, min(len(us.SPIN_RULES) - 1, int(v)))),
	('finesse', lambda v: max(0, min(len(us.FINESSE_RULES) - 1, int(v)))),
	('enablekicks', bool),
	('showghost', bool),
	('linktiles', bool),
)

# Action order is the order the rebinding screen lists them in.
ACTIONS = (
	('left', 'Shift Left'),
	('right', 'Shift Right'),
	('softdrop', 'Soft Drop'),
	('harddrop', 'Hard Drop'),
	('rotate_ccw', 'Rotate CCW'),
	('rotate_cw', 'Rotate CW'),
	('rotate_180', 'Rotate 180'),
	('hold', 'Hold'),
	('pause', 'Pause'),
)

DEFAULTS = {
	'left': (pg.K_LEFT,),
	'right': (pg.K_RIGHT,),
	'softdrop': (pg.K_DOWN,),
	'harddrop': (pg.K_SPACE,),
	'rotate_ccw': (pg.K_z, pg.K_LCTRL),
	'rotate_cw': (pg.K_x, pg.K_UP),
	'rotate_180': (pg.K_a,),
	'hold': (pg.K_LSHIFT,),
	'pause': (pg.K_ESCAPE,),
}

# pygame's own names for these read as bare words like 'left', which is unhelpful
# next to a row already labelled 'Shift Left'.
KEY_NAMES = {
	pg.K_LEFT: 'Left Arrow',
	pg.K_RIGHT: 'Right Arrow',
	pg.K_UP: 'Up Arrow',
	pg.K_DOWN: 'Down Arrow',
}

# How many keys one action will hold. Four already fills the width of a row on
# the rebinding screen, and nobody needs a fifth.
MAX_KEYS = 4

# TETR.IO's handling knobs, in the units that game shows them in. The engine runs
# at 50 frames per second, so anything in milliseconds lands on a 20ms grid.
HANDLING = (
	('das', 'DAS', 'ms', 0, 500, 20),
	('arr', 'ARR', 'ms', 0, 200, 20),
	('dcd', 'DCD', 'ms', 0, 200, 20),
	('sdf', 'SDF', 'x', 5, 40, 1),
	('are', 'ARE', 'ms', 0, 500, 20),
)

# ARE defaults to nothing: the base game held the board for 400ms after every
# placement, which is dead time in a trainer built around reaction speed.
HANDLING_DEFAULTS = {'das': 140, 'arr': 40, 'dcd': 0, 'sdf': 6, 'are': 0}

# Above this, soft drop stops being a speed and becomes a slam to the floor.
SDF_INSTANT = 40

def handling_defaults ():
	return dict(HANDLING_DEFAULTS)

def describe_handling (user, name):
	# The value as the player set it, or the word the extreme end of the range means.
	value = getattr(user, name)
	if name == 'arr' and value <= 0:
		return 'Instant'
	if name == 'dcd' and value <= 0:
		return 'Off'
	if name == 'sdf' and value >= SDF_INSTANT:
		return 'Instant'
	if name == 'are' and value <= 0:
		return 'None'
	return '{}{}'.format(int(value), 'ms' if name != 'sdf' else 'x')

def clamp_handling (user, name, value):
	# The setting on its own, without touching the file. Load needs this: writing a
	# half-applied profile back over the one being read is a good way to lose it.
	for key, label, unit, low, high, step in HANDLING:
		if key == name:
			setattr(user, name, min(high, max(low, value)))
			return

def set_handling (user, name, value):
	clamp_handling(user, name, value)
	save(user)

def key_name (code):
	# A label for one key code, as close to what is printed on the key as we can get.
	if code in KEY_NAMES:
		return KEY_NAMES[code]
	try:
		name = pg.key.name(code)
	except (ValueError, TypeError):
		return 'Key {}'.format(code)
	# pygame returns lower case words like 'left shift' and 'page up'.
	return ' '.join(word.capitalize() for word in name.split()) or 'Key {}'.format(code)

def describe (user, action):
	# Every key bound to an action, or a note that it has none.
	codes = user.keys.get(action, ())
	return ', '.join(key_name(code) for code in codes) if codes else 'Unbound'

def matches (user, action, code):
	# True if the pressed key is bound to this action.
	return code in user.keys.get(action, ())

def defaults ():
	return {action: tuple(codes) for action, codes in DEFAULTS.items()}

def reset (user):
	user.keys = defaults()
	for name, value in HANDLING_DEFAULTS.items():
		setattr(user, name, value)
	save(user)

def bind (user, action, code, replace=True):
	# Give a key to one action, taking it off any other that held it. An action
	# stripped of its last key reads as Unbound and simply stops responding,
	# which is recoverable from the same screen.
	#
	# With replace off the key joins whatever the action already answers to, so one
	# action can be driven from several keys - which is how the defaults ship
	# rotation on both Z and Left Ctrl.
	if not replace and len(user.keys.get(action, ())) >= MAX_KEYS:
		return False
	for other, codes in user.keys.items():
		if other != action and code in codes:
			user.keys[other] = tuple(c for c in codes if c != code)
	if replace:
		user.keys[action] = (code,)
	elif code not in user.keys.get(action, ()):
		user.keys[action] = tuple(user.keys.get(action, ())) + (code,)
	save(user)
	return True

def unbind_last (user, action):
	# Drop the most recently added key, so a mistaken addition is undoable without
	# retyping the whole binding.
	codes = user.keys.get(action, ())
	if not codes:
		return False
	user.keys[action] = codes[:-1]
	save(user)
	return True

def load (user):
	# Read the saved profile, falling back to the defaults for anything missing or
	# unreadable. Everything the settings menu can change is in here, so a session
	# spent tuning the delay and the handling is not thrown away on exit.
	user.keys = defaults()
	for name, value in HANDLING_DEFAULTS.items():
		setattr(user, name, value)
	saved = None
	for path in (CONFIG, LEGACY):
		if path is None:
			continue
		try:
			with open(path, 'r') as config:
				saved = json.load(config)
			break
		except (IOError, ValueError):
			continue
	if not isinstance(saved, dict):
		return
	for action, codes in (saved.get('keys') or {}).items():
		if action in DEFAULTS and isinstance(codes, list):
			user.keys[action] = tuple(code for code in codes if isinstance(code, int))
	for name, value in (saved.get('handling') or {}).items():
		if name in HANDLING_DEFAULTS and isinstance(value, (int, float)):
			clamp_handling(user, name, value)
	for name, clean in SETTINGS:
		if name in (saved.get('settings') or {}):
			try:
				setattr(user, name, clean(saved['settings'][name]))
			except (TypeError, ValueError):
				# One unusable value is no reason to discard the rest of the file.
				pass

def save (user):
	# Best effort. The browser build has no writable filesystem worth the name,
	# and failing to save a keybind is not worth taking the game down for.
	try:
		os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
		with open(CONFIG, 'w') as config:
			json.dump({
				'keys': {action: list(codes) for action, codes in user.keys.items()},
				'handling': {name: getattr(user, name) for name in HANDLING_DEFAULTS},
				'settings': {name: getattr(user, name) for name, clean in SETTINGS},
			}, config, indent=1)
	except (IOError, OSError):
		pass
