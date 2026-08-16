#!/usr/bin/env python
"A Tetris trainer that hard drops your piece for you once the clock runs out."
__url__ = "https://github.com/KJH-code/Forcetris"
__author__ = "virtuNat, KJH-code"
__license__ = "GPL"
__version__ = "1.0.0"
__basedon__ = "pyTetris 1.1.9"

if __name__ == '__main__':
	import argparse
	# Parse the optional arguments for the command line interface. Anything left off
	# the command line keeps whatever the saved profile holds, so a value set once in
	# the settings menu survives without being retyped.
	parser = argparse.ArgumentParser(description="Tetris clone with a forced hard drop timer, implemented using Pygame.")
	parser.add_argument('-d', '--debug', action='store_true', help="enables debug mode")
	parser.add_argument(
		'-f', '--forced-delay', type=float, default=None, metavar='SECONDS',
		help="seconds a piece may stay in play before it is hard dropped for you (0 disables)"
	)
	parser.add_argument(
		'-v', '--volume', type=float, default=None, metavar='PERCENT',
		help="music volume from 0 to 100 (0 mutes)"
	)
	parser.add_argument(
		'-s', '--sfx-volume', type=float, default=None, metavar='PERCENT',
		help="sound effect volume from 0 to 100 (0 mutes)"
	)
	# The browser build has no command line, so it runs on the defaults. Everything
	# these flags set can also be changed in the game's own settings menu. Unknown
	# arguments are ignored there rather than exiting, since a usage error would take
	# the whole page down instead of printing to a terminal nobody is watching.
	import sys
	args = parser.parse_known_args()[0] if sys.platform == 'emscripten' else parser.parse_args()
	# Imported after parsing so that --help doesn't need a display or a sound card.
	import asyncio
	import engine.game
	# Run the game. The loop is a coroutine so that the same entry point works both
	# as a desktop process and inside a browser tab, which owns its own event loop.
	asyncio.run(engine.game.init(args).run())
