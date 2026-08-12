#!/usr/bin/env python
"A Tetris trainer that hard drops your piece for you once the clock runs out."
__url__ = "https://github.com/KJH-code/Forcetris"
__author__ = "virtuNat, KJH-code"
__license__ = "GPL"
__version__ = "1.0.0"
__basedon__ = "pyTetris 1.1.9"

if __name__ == '__main__':
	import argparse
	# Safe to import ahead of parsing: userstate pulls in nothing, unlike engine.game.
	from engine.userstate import DEFAULT_FORCED_DELAY, DEFAULT_VOLUME
	# Parse the optional arguments for the command line interface.
	parser = argparse.ArgumentParser(description="Tetris clone with a forced hard drop timer, implemented using Pygame.")
	parser.add_argument('-d', '--debug', action='store_true', help="enables debug mode")
	parser.add_argument(
		'-f', '--forced-delay', type=float, default=DEFAULT_FORCED_DELAY, metavar='SECONDS',
		help="seconds a piece may stay in play before it is hard dropped for you (default %(default)s, 0 disables)"
	)
	parser.add_argument(
		'-v', '--volume', type=float, default=DEFAULT_VOLUME * 100, metavar='PERCENT',
		help="music volume from 0 to 100 (default %(default)g, 0 mutes)"
	)
	args = parser.parse_args()
	# Imported after parsing so that --help doesn't need a display or a sound card.
	import engine.game
	# Run the game.
	tetris = engine.game.init(args)
	tetris.run()
