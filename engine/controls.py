"""Gameplay key bindings, and the file they persist in.

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
except ImportError:
	print("A module must've shat itself:")
	raise

# Same anchor as engine.environment.ROOT, recomputed here to keep this module
# free of anything that needs a display surface.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG = os.path.join(ROOT, 'data', 'controls.json')

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
)

HANDLING_DEFAULTS = {'das': 140, 'arr': 40, 'dcd': 0, 'sdf': 6}

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
	return '{}{}'.format(int(value), 'ms' if name != 'sdf' else 'x')

def set_handling (user, name, value):
	for key, label, unit, low, high, step in HANDLING:
		if key == name:
			setattr(user, name, min(high, max(low, value)))
			save(user)
			return

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
	# Read saved bindings, falling back to the defaults for anything missing or
	# unreadable. Bindings are worth persisting even though the other settings
	# are not: nobody wants to redo them on every launch.
	user.keys = defaults()
	for name, value in HANDLING_DEFAULTS.items():
		setattr(user, name, value)
	try:
		with open(CONFIG, 'r') as config:
			saved = json.load(config)
	except (IOError, ValueError):
		return
	if not isinstance(saved, dict):
		return
	for action, codes in (saved.get('keys') or {}).items():
		if action in DEFAULTS and isinstance(codes, list):
			user.keys[action] = tuple(code for code in codes if isinstance(code, int))
	for name, value in (saved.get('handling') or {}).items():
		if name in HANDLING_DEFAULTS and isinstance(value, (int, float)):
			set_handling(user, name, value)

def save (user):
	# Best effort. The browser build has no writable filesystem worth the name,
	# and failing to save a keybind is not worth taking the game down for.
	try:
		os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
		with open(CONFIG, 'w') as config:
			json.dump({
				'keys': {action: list(codes) for action, codes in user.keys.items()},
				'handling': {name: getattr(user, name) for name in HANDLING_DEFAULTS},
			}, config, indent=1)
	except (IOError, OSError):
		pass
