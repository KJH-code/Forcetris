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
	save(user)

def bind (user, action, code):
	# Give a key to one action, taking it off any other that held it. An action
	# stripped of its last key reads as Unbound and simply stops responding,
	# which is recoverable from the same screen.
	for other, codes in user.keys.items():
		if other != action and code in codes:
			user.keys[other] = tuple(c for c in codes if c != code)
	user.keys[action] = (code,)
	save(user)

def load (user):
	# Read saved bindings, falling back to the defaults for anything missing or
	# unreadable. Bindings are worth persisting even though the other settings
	# are not: nobody wants to redo them on every launch.
	user.keys = defaults()
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

def save (user):
	# Best effort. The browser build has no writable filesystem worth the name,
	# and failing to save a keybind is not worth taking the game down for.
	try:
		os.makedirs(os.path.dirname(CONFIG), exist_ok=True)
		with open(CONFIG, 'w') as config:
			json.dump({'keys': {action: list(codes) for action, codes in user.keys.items()}}, config, indent=1)
	except (IOError, OSError):
		pass
