"Contains class definitions for objects that track play state."

# Seconds a piece may stay in play before Forcetris hard drops it for the player.
# A value of 0 turns the forced drop off and leaves plain Tetris behind.
DEFAULT_FORCED_DELAY = 1.0

# Music volume as a 0.0 to 1.0 fraction. The command line takes it as a percentage,
# to match the way the settings menu shows it.
DEFAULT_VOLUME = 1.0

# Sound effect volume, same 0.0 to 1.0 fraction and same percentage on the command line.
DEFAULT_SFX_VOLUME = 1.0

class User:
	"""
	The User class tracks global game state values, such as
	plot flags, difficulty, game internal state, etc.

	In this case, it tracks tetris difficulty values and handles score data.
	"""
	__slots__ = (
		'state', 'gametype', 'resetgame', 'debug', 'forced_delay', 'volume', 'sfx_volume',
		'cleartype', 'enablekicks', 'showghost', 'linktiles',
		'hard_flag', 'twist_flag', 'tspin_flag',
		'score', 'last_score', 'lines_cleared', 'level', 'timer',
		'line_list', 'combo_ctr', 'current_combo'
	)
	# Score data.
	drop_score = 1. # The base score added when a block lands.
	dist_factor = 0.6 # The multiplier per unit distance dropped by a piece in a soft or hard drop.
	line_score = 500. # The base score added when a line is cleared.
	line_factor = 0.8 # The added percentage for each line cleared in one drop.
	cascade_factor = 1.3 # The multiplier for each set of lines cleared successively. Exponential.
	twist_factor = 2.7 # The multiplier if the starting piece was twisted in.
	tspin_factor = 1.8 # The multiplier if it was a successful T-spin.
	combo_factor = 1.6 # The multiplier for subsequent drop clears.
	clear_factor = 2. # The multiplier if the entire matrix was cleared by this piece.

	def __init__ (self):
		# Global state variables.
		self.state = 'main_menu'
		self.gametype = 'free'
		self.resetgame = False # True if the game needs to be reset.
		# Eventually will be modifiable in the Options Menu.
		# Default settings are good for Modern Tetris.
		# Retro Tetris would use cleartype 0, enablekicks, showghost, and linktiles False.
		self.cleartype = 2 # Determines line clear type, refer to Grid.clear_lines().
		self.enablekicks = True # Determines if wall kicks are allowed.
		self.showghost = True # Determines if the ghost tetrimino will be shown.
		self.linktiles = True # Determines if the blocks will use connected textures.

		self.hard_flag = False # True if the piece was hard-dropped.
		self.twist_flag = False # True if the tetrimino twisted into place.
		self.tspin_flag = False # True if a T-spin occured.
		self.eval_argv(None)
		self.reset()

	def __str__ (self):
		# Debug info dump.
		return (
			"User instance running <"+self.state+">.\n"
			"User Settings:\n"
			"Clear Type: "+(['Naive', 'Sticky Cascade', 'Linked Cascade'][self.cleartype])+"\n"
			"Wall Kicks: "+('Enabled' if self.enablekicks else 'Disabled')+"; "
			"Ghost Piece: "+('Enabled' if self.showghost else 'Disabled')+"; "
			"Linked Tile Textures: "+('Enabled' if self.linktiles else 'Disabled')+"\n"
			"Forced Drop: "+("{:.3f}s".format(self.forced_delay) if self.forced_delay > 0 else 'Disabled')+"; "
			"Music Volume: "+"{:.0%}".format(self.volume)+"; "
			"Sound Volume: "+"{:.0%}".format(self.sfx_volume)+"\n\n"
			"Scoring Values:\n"
			"Score: "+str(self.score)+"; Last Clear: "+str(self.last_score)+" Clearing Chain: "+str(self.line_list[:-1])+"\n"
			"Level: "+str(self.level)+"; Timer: "+"{}:{:02d}:{:02d}".format(self.timer // 60000, self.timer//1000 % 60, self.timer%1000 // 10)+"\n"
			"Combo Number: "+str(self.combo_ctr)+"; Combo Multiplier: "+str(self.current_combo)+"\n"
		)

	def eval_argv (self, argv):
		# argv is an argparse.Namespace object that defines special behavior for testing purposes.
		if argv is not None:
			self.debug = argv.debug # Debug mode: cheats on!
			# Seconds a piece is allowed to stay in play before it is dropped for the player.
			self.forced_delay = max(0., argv.forced_delay)
			# Taken as a percentage on the command line, kept as a fraction here.
			self.volume = min(1., max(0., argv.volume / 100.))
			self.sfx_volume = min(1., max(0., argv.sfx_volume / 100.))
		else:
			self.debug = False
			self.forced_delay = DEFAULT_FORCED_DELAY
			self.volume = DEFAULT_VOLUME
			self.sfx_volume = DEFAULT_SFX_VOLUME

	def reset (self):
		# Reset data when starting a new game.
		self.score = 0 # Score value for the current game.
		self.last_score = 0 # Score value for the last clear.
		self.line_list = [0] # Tracks how many lines are cleared in a single clearing chain.
		self.lines_cleared = 0 # Total number of lines cleared in the game.
		self.level = 1 # Current level in arcade mode.
		self.timer = 0 # How long the game has been playing.

		self.combo_ctr = 0 # Current combo number.
		self.current_combo = 1. # The current combo multiplier.

	def eval_drop_score (self, posdif=0):
		# Add score value when piece is dropped.
		if self.state != 'loss_menu':
			if self.hard_flag:
				self.score += int(round(self.drop_score + (self.dist_factor*posdif), 0))
				self.hard_flag = False
			else:
				self.score += int(round(self.drop_score + (self.dist_factor*posdif/3.), 0))

	def predict_score (self, clearflag):
		# Evaluate clear line combo score value, but don't add it yet.
		if self.line_list[-1] == 0:
			self.line_list.pop()
		temp_score = 0
		# In arcade mode, level boosts score earned by line clears.
		linescore = self.line_score
		if self.gametype == 'arcade':
			linescore += self.level*2.5
		# Calculate base score from number of cascades and number of lines cleared per cascade.
		for line in self.line_list:
			temp_score += linescore * line * (1 + (self.line_factor * (line-1)))
		temp_score *= (self.cascade_factor ** (len(self.line_list)-1)) * self.current_combo
		# Increase score for twists.
		if self.twist_flag:
			temp_score *= self.twist_factor
		# Increase score for T-spins.
		if self.tspin_flag:
			temp_score *= self.tspin_factor
		# Increase score for perfect clears.
		if clearflag:
			temp_score *= self.clear_factor
		# Increase score as timer goes down in timed mode.
		if self.gametype == 'timed':
			temp_score *= 1 + float(300 - (self.timer//1000))/100
		return int(round(temp_score / 50, 0) * 50)

	def eval_clear_score (self, clearflag):
		# Evaluates the score gain from the last line clear.
		if len(self.line_list) > 1 or self.line_list[0] > 0:
			# Add the clear line score.
			self.lines_cleared += sum(self.line_list)
			self.last_score = self.predict_score(clearflag)
			self.score += self.last_score
			# Increment combo counter.
			self.combo_ctr += 1
			self.current_combo = self.combo_factor ** self.combo_ctr
		else:
			# If no lines were cleared, break combo.
			self.combo_ctr = 0
			self.current_combo = 1.0

	def eval_level (self):
		# Evaluate current arcade level.
		if self.lines_cleared <= 640: # Up to level 64, increment every 10 lines.
			self.level = 1 + self.lines_cleared // 10
		elif self.lines_cleared <= 1920: # Up to level 128, increment every 20 lines.
			self.level = 64 + ((self.lines_cleared - 640) // 20)
		elif self.lines_cleared <= 3840: # Up to level 192, increment every 30 lines.
			self.level = 128 + ((self.lines_cleared - 1920) // 30)
		elif self.lines_cleared <= 6400: # Up to level 256, increment every 40 lines.
			self.level = 192 + ((self.lines_cleared - 3840) // 40)
		else: self.level = 256

	def eval_timer (self, time):
		# Evaluate timer.
		if self.gametype == 'timed':
			if self.timer > 0:
				self.timer -= time
			else:
				self.timer = 0
				self.state = 'loss_menu'
		else:
			self.timer += time
