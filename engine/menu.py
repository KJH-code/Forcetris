"Contains subclass definitions for all menus used in the game."
try:
	import pygame as pg
	import engine.environment as env
	import engine.filehandler as fh
	import engine.controls as ctl
except ImportError:
	print("Something fucking jammed in here:")
	raise

class MainMenu (env.Menu):
	"""
	The MainMenu object represents the main menu of the game.

	The player is capable of starting the game selection, viewing the high score tables, and
	changing the options.
	"""

	def __init__ (self, user, score_menu, help_menu, settings_menu):
		bg = pg.Surface((210, 300))
		bg.fill(0x00FF00)
		super().__init__(user, bg, midtop=(env.screct.width / 2, 250))
		self.score_menu = score_menu
		self.help_menu = help_menu
		self.settings_menu = settings_menu

		hmargin = 15 # horizontal margin in pixels
		tmargin = 20 # top margin in pixels
		spacing = 5 # space between selections in pixels
		height = (self.rect.h - 2 * tmargin - 4 * spacing) / 5 # height of selections in pixels

		self.selections = [[
			env.MenuOption(self, 'play', 'Start Game', (hmargin, tmargin), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'help', 'How to Play', (hmargin, tmargin + (spacing + height)), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'hiscore', 'High Scores', (hmargin, tmargin + 2 * (spacing + height)), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'settings', 'Game Settings', (hmargin, tmargin + 3 * (spacing + height)), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'quit', 'Quit', (hmargin, tmargin + 4 * (spacing + height)), (self.rect.w - 2 * hmargin, height))]]

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'play':
					self.user.state = 'play_menu'
				elif self.selected.action == 'help':
					self.help_menu.return_state = 'main_menu'
					self.user.state = 'help_menu'
				elif self.selected.action == 'hiscore':
					self.user.state = 'score_menu'
					# Load the scores into the score menu every time it is selected so the scores are up to date.
					with fh.SFH() as sfh:
						self.score_menu.scorelist = sfh.decode()
				elif self.selected.action == 'settings':
					self.settings_menu.return_state = 'main_menu'
					self.user.state = 'settings_menu'
				elif self.selected.action == 'quit':
					self.user.state = 'quit'

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		pg.display.flip()

class PlayMenu (env.Menu):
	"""
	PlayMenu is the selection menu after "Start Game" is selected, it provides the player
	with the available modes of play.

	Arcade Mode is standard Tetris, with increasing levels dependent on cleared lines. The
	speed of the tetrominos dropping increase with the levels, up to a maximum. Score earned
	per line cleared increases with level.

	Timed Mode is based on a timer that runs down. Dropping pieces and clearing lines cause
	delays that are counted by the timer, so it's up to the player to find the most efficient
	way to get the highest score in five minutes!

	Free Mode is a casual mode of play that doesn't stress the player. Good for newbies!
	"""

	def __init__(self, user):
		bg = pg.Surface((620, 300))
		bg.fill(0x00FF40)
		super().__init__(user, bg, midtop=(env.screct.width / 2, 250))

		hmargin = 20
		spacing = 14
		tmargin = 20
		height = 80

		self.selections = [
			[env.MenuOption(self, 'arcade', 'Arcade Mode', (hmargin, tmargin), ((self.rect.w - (2 * (spacing + hmargin))) / 3, height))],
			[env.MenuOption(self, 'timed', 'Timed Mode', (hmargin + spacing + (self.rect.w - (2 * (spacing + hmargin))) / 3, tmargin), ((self.rect.w - (2 * (spacing + hmargin))) / 3, height))],
			[env.MenuOption(self, 'free', 'Free Mode', (hmargin + 2 * spacing + (2 * (self.rect.w - (2 * (spacing + hmargin))) / 3), tmargin), ((self.rect.w - (2 * (spacing + hmargin))) / 3, height))]]

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'arcade':
					self.user.gametype = 'arcade'
				elif self.selected.action == 'timed':
					self.user.gametype = 'timed'
				elif self.selected.action == 'free':
					self.user.gametype = 'free'

				self.user.state = 'game'
				self.user.resetgame = True
				env.restart_music()
				self.reset()
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = 'main_menu'

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		pg.display.flip()

class HelpMenu (env.Menu):
	"""
	The Help Menu lists the controls and explains what the forced drop timer does,
	since that part isn't in any Tetris the player has met before.

	It has one selection, which returns to whichever menu opened it.
	"""
	def __init__ (self, user):
		bg = pg.Surface((620, 440))
		bg.fill(0x2F6F8F)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'main_menu'

		self.selections = [[env.MenuOption(self, 'back', 'Back', (self.rect.w / 2 - 90, 380), (180, 40))]]

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if (event.key == pg.K_z or event.key == pg.K_RETURN
				or event.key == pg.K_x or event.key == pg.K_ESCAPE):
				self.user.state = self.return_state
				self.reset()

	@env.Menu.render
	def display_help (self, surf):
		self.render_text('How to Play', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 20))
		# Read from the live bindings, so a rebound key is reflected here.
		for i, (action, name) in enumerate(ctl.ACTIONS):
			self.render_text(ctl.describe(self.user, action), 0xFFE080, surf, topright=(250, 60 + i * 25))
			self.render_text(name, 0xFFFFFF, surf, topleft=(275, 60 + i * 25))
		# The part that isn't standard Tetris.
		self.render_text('Forced Drop', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 265))
		if self.user.forced_delay > 0.:
			lines = [
				'Every piece is hard dropped for you {:.2f}s after it spawns.'.format(self.user.forced_delay),
				'Holding gives the incoming piece a fresh {:.2f}s, once per piece.'.format(self.user.forced_delay),
				'Soft dropping and wall kicks do not stop the clock.',
				'Change the time under Game Settings.',
			]
		else:
			lines = [
				'Currently off, so this is plain Tetris.',
				'Turn it on under Game Settings to train placement speed.',
			]
		for i, line in enumerate(lines):
			self.render_text(line, 0xFFFFFF, surf, midtop=(self.rect.w / 2, 295 + i * 22))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		self.display_help()
		pg.display.flip()

class HiScoreMenu (env.Menu):
	"""
	The High Score Menu allows the player to view the recorded high scores in the file
	hiscore.dat, segregated by game time and arranged by score. Time played and number
	of lines cleared are also displayed, along with the name of the score setter.

	Its selections are just switching between the scoreboards of the three different game modes.
	"""
	def __init__ (self, user):
		bg = pg.Surface((700, 480))
		bg.fill(0x40C080)
		super().__init__(user, bg, midbottom=(env.screct.centerx, env.screct.bottom - 25))
		self.font = pg.font.SysFont(None, 30)

		self.selections = [
			[env.MenuOption(self, 'arcade', None, env.screct.topright)],
			[env.MenuOption(self, 'timed', None, env.screct.topright)],
			[env.MenuOption(self, 'free', None, env.screct.topright)]]

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = 'main_menu'

	@env.Menu.render
	def display_scores (self, surf):
		self.render_text(self.selected.action.capitalize() + ' Mode', 0x000000, surf, midtop=(self.rect.w / 2, 30))
		self.render_text('Name:', 0x000000, surf, topleft=(25, 70))
		self.render_text('Score:', 0x000000, surf, topleft=(185, 70))
		self.render_text('Lines:', 0x000000, surf, topleft=(390, 70))
		self.render_text('Time Taken:', 0x000000, surf, topleft=(560, 70))
		d_scores = self.scorelist[self.selection[0]]
		for i in range(10):
			self.render_text('{}'.format(d_scores[i][0]), 0x000000, surf, topleft=(30, 120 + i * 35))
			self.render_text('{}'.format(d_scores[i][1]), 0x000000, surf, topright=(self.rect.w / 2 - 30, 120 + i * 35))
			self.render_text('{}'.format(d_scores[i][2]), 0x000000, surf, topright=(self.rect.w - 210, 120 + i * 35))
			self.render_text('{}:{:02d}:{:02d}'.format(d_scores[i][3]//6000, d_scores[i][3]//100%60, d_scores[i][3]%100), 0x000000, surf, topright=(self.rect.w - 30, 120 + i * 35))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		self.display_scores()
		pg.display.flip()

class SettingsMenu (env.Menu):
	"""
	The Settings Menu allows the user to edit game settings for more convenient play.

	The selections of this menu are special, and change the states of global variables.
	Up and Down pick a setting, Left and Right change it. Everything here is read live
	by the game, so a setting changed from the pause menu applies to the piece in play.
	"""
	# Bounds on the forced drop delay, in seconds. The step is what one keypress moves.
	delay_step = 0.05
	delay_max = 5.0
	# Music volume runs 0.0 to 1.0, moved in twentieths so a full sweep is 20 presses.
	volume_step = 0.05

	clear_names = ('Naive', 'Sticky Cascade', 'Linked Cascade')

	def __init__ (self, user, controls_menu):
		# Tall enough for nine rows under the title plus the hint line beneath them.
		bg = pg.Surface((460, 474))
		bg.fill(0x00A060)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'main_menu'
		self.controls_menu = controls_menu

		hmargin = 20
		tmargin = 50
		spacing = 6
		height = 38
		width = self.rect.w - 2 * hmargin

		self.selections = [[
			env.MenuOption(self, action, '', (hmargin, tmargin + i * (spacing + height)), (width, height))
			for i, action in enumerate(('delay', 'ghost', 'kicks', 'tiles', 'clears', 'music', 'sound', 'controls', 'back'))]]
		self.set_labels()

	def label (self, action):
		# The text drawn on a row, value included.
		if action == 'delay':
			return 'Forced Drop:  ' + (
				'{:.2f}s'.format(self.user.forced_delay) if self.user.forced_delay > 0. else 'Off')
		elif action == 'ghost':
			return 'Ghost Piece:  ' + ('On' if self.user.showghost else 'Off')
		elif action == 'kicks':
			return 'Wall Kicks:  ' + ('On' if self.user.enablekicks else 'Off')
		elif action == 'tiles':
			return 'Linked Tiles:  ' + ('On' if self.user.linktiles else 'Off')
		elif action == 'clears':
			return 'Line Clears:  ' + self.clear_names[self.user.cleartype]
		elif action == 'music':
			return 'Music:  ' + ('{:.0%}'.format(self.user.volume) if self.user.volume > 0. else 'Off')
		elif action == 'sound':
			return 'Sound:  ' + ('{:.0%}'.format(self.user.sfx_volume) if self.user.sfx_volume > 0. else 'Off')
		elif action == 'controls':
			return 'Controls...'
		return 'Back'

	def set_labels (self):
		# Re-render every row, since changing one setting can only be seen by redrawing it.
		for option in self.selections[0]:
			option.text = self.font.render(self.label(option.action), 0, pg.Color(255, 255, 255))
			option.text_rect = option.text.get_rect(center=option.rect.center)

	def adjust (self, movedir):
		# Apply one step of change to the highlighted setting.
		action = self.selected.action
		if action == 'delay':
			# Rounded because repeatedly adding 0.05 drifts off the step grid.
			delay = round(self.user.forced_delay + movedir * self.delay_step, 2)
			self.user.forced_delay = min(self.delay_max, max(0., delay))
		elif action == 'ghost':
			self.user.showghost = not self.user.showghost
		elif action == 'kicks':
			self.user.enablekicks = not self.user.enablekicks
		elif action == 'tiles':
			self.user.linktiles = not self.user.linktiles
		elif action == 'clears':
			self.user.cleartype = (self.user.cleartype + movedir) % len(self.clear_names)
		elif action == 'music':
			volume = round(self.user.volume + movedir * self.volume_step, 2)
			self.user.volume = min(1., max(0., volume))
			# Straight to the stream, so the player hears the level they picked
			# while the track is still playing behind the pause menu.
			env.set_volume(self.user.volume)
		elif action == 'sound':
			volume = round(self.user.sfx_volume + movedir * self.volume_step, 2)
			self.user.sfx_volume = min(1., max(0., volume))
			env.set_sfx_volume(self.user.sfx_volume)
			# Play a cue at the new level, so the number can be set by ear.
			env.play_sound('rotate')
		else:
			return
		self.set_labels()

	def eval_move (self, coord, movedir):
		# Horizontal movement edits the highlighted setting rather than moving the
		# cursor, which reuses the auto-repeat timing the base class already runs.
		if coord == 0:
			if not self.moved:
				self.adjust(movedir)
				self.moved = True
			else:
				self.movetime -= 1
				if self.movetime < 1:
					self.adjust(movedir)
					self.movetime = self.shortime
		else:
			super().eval_move(coord, movedir)

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'back':
					self.user.state = self.return_state
					self.reset()
				elif self.selected.action == 'controls':
					self.controls_menu.return_state = 'settings_menu'
					self.user.state = 'controls_menu'
				else:
					# Confirming a toggle flips it, which is what pressing it looks like it should do.
					self.adjust(1)
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = self.return_state
				self.reset()

	@env.Menu.render
	def display_hint (self, surf):
		self.render_text('Game Settings', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 15))
		self.render_text('LEFT / RIGHT to change, X to go back', 0xE0FFE0, surf, midbottom=(self.rect.w / 2, self.rect.h - 12))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		self.display_hint()
		pg.display.flip()

class ControlsMenu (env.Menu):
	"""
	Rebinds the gameplay keys.

	Menu navigation is deliberately absent from this list. The arrow keys, Z, X,
	Enter and Escape always drive the menus, so there is no combination of bindings
	that can strand a player outside the screen that would undo them.

	Selecting a row waits for the next key press. Escape cancels that wait, which is
	why Escape itself cannot be bound - Reset to Defaults puts it back on Pause.
	"""
	def __init__ (self, user):
		# Ten rows: one per action, plus Reset and Back.
		bg = pg.Surface((470, 478))
		bg.fill(0x0E7C7B)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'settings_menu'
		# The action currently waiting for a key press, or None.
		self.listening = None

		hmargin = 20
		tmargin = 50
		spacing = 5
		height = 34
		width = self.rect.w - 2 * hmargin
		rows = [action for action, label in ctl.ACTIONS] + ['reset', 'back']

		self.selections = [[
			env.MenuOption(self, action, '', (hmargin, tmargin + i * (spacing + height)), (width, height))
			for i, action in enumerate(rows)]]
		self.set_labels()

	def label (self, action):
		# The text drawn on a row, current binding included.
		if action == 'reset':
			return 'Reset to Defaults'
		elif action == 'back':
			return 'Back'
		name = dict(ctl.ACTIONS)[action]
		if self.listening == action:
			return name + ':  Press a key...'
		return name + ':  ' + ctl.describe(self.user, action)

	def set_labels (self):
		for option in self.selections[0]:
			option.text = self.font.render(self.label(option.action), 0, pg.Color(255, 255, 255))
			option.text_rect = option.text.get_rect(center=option.rect.center)

	def eval_input (self):
		if self.listening is not None:
			# Swallow input while waiting for a key, so that arrows and Enter can be
			# bound rather than moving the cursor or re-triggering the row.
			event = pg.event.poll()
			if event.type == pg.QUIT:
				self.user.state = 'quit'
			elif event.type == pg.KEYDOWN:
				if event.key != pg.K_ESCAPE:
					ctl.bind(self.user, self.listening, event.key)
				self.listening = None
				self.set_labels()
			return event

		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'back':
					self.user.state = self.return_state
					self.reset()
				elif self.selected.action == 'reset':
					ctl.reset(self.user)
					self.set_labels()
				else:
					# Stop the cursor drifting on the key that opened the prompt.
					self.setdir()
					self.listening = self.selected.action
					self.set_labels()
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = self.return_state
				self.reset()

	@env.Menu.render
	def display_hint (self, surf):
		self.render_text('Controls', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 15))
		hint = ('Press the key to bind, or Escape to cancel' if self.listening is not None
			else 'Z to rebind, X to go back')
		self.render_text(hint, 0xD0FFFF, surf, midbottom=(self.rect.w / 2, self.rect.h - 12))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		self.display_hint()
		pg.display.flip()

class PauseMenu (env.Menu):
	"""
	Pauses the game.
	The timer doesn't run while paused.
	"""

	def __init__(self, user, settings_menu):
		self.pause_bg = pg.Surface(env.screct.size)
		bg = pg.Surface((250, 300))
		bg.fill(0x00FF00)
		super().__init__(user, bg, center=env.screct.center)
		self.settings_menu = settings_menu
		
		tmargin = 20
		hmargin = 15
		spacing = 5
		height = 60

		self.selections = [[
			env.MenuOption(self, 'resume', 'Resume Game', (hmargin, tmargin), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'restart', 'Restart Game', (hmargin, tmargin + spacing + height), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'options', 'Options', (hmargin, tmargin + 2 * (spacing + height)), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'quit', 'Return to Menu', (hmargin, tmargin + 3 * (spacing + height)), (self.rect.w - 2 * hmargin, height))]]

	def set_bg (self, bg):
		# Set the background of the Pause Menu to the state of the game when the user paused it.
		self.pause_bg.blit(bg, (0, 0))

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'resume':
					self.user.state = 'game'
					pg.mixer.music.unpause()
					self.reset()
				if self.selected.action == 'restart':
					self.user.state = 'game'
					self.user.reset()
					self.user.resetgame = True
					env.restart_music()
					self.reset()
				elif self.selected.action == 'options':
					self.settings_menu.return_state = 'pause_menu'
					self.user.state = 'settings_menu'
				elif self.selected.action == 'quit':
					self.user.state = 'main_menu'
					self.user.reset()
					self.user.resetgame = True
					self.reset()
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = 'game'

	def run (self):
		env.screen.blit(self.pause_bg, (0, 0))
		self.draw(env.screen)
		super().run()
		pg.display.flip()


class SaveMenu (env.Menu):
	"""
	SaveMenu prompts the player to save a name to be attached to a high score,
	if they would have a high score that beats one on the record.
	"""
	def __init__(self, user):
		self.loss_bg = pg.Surface(env.screct.size)
		bg = pg.Surface((500, 150))
		bg.fill(0x0C87CD)
		super().__init__(user, bg, center=env.screct.center)
		self.name = u''
		self.placestring = '10th'

	def render_place (self, i):
		# Turns place number into a string.
		if i == 0:
			self.placestring = '1st'
		elif i == 1:
			self.placestring = '2nd'
		elif i == 2:
			self.placestring = '3rd'
		else:
			self.placestring = str(i+1) + 'th'

	def eval_timer (self):
		# Evaluates timer value based on game type.
		return (300000 - self.user.timer) // 10 if self.user.gametype == 'timed' else self.user.timer // 10

	def eval_input (self):
		event = pg.event.poll()
		if event.type == pg.QUIT:
			self.user.state = 'quit'
		if event.type == pg.KEYDOWN:
			if ((pg.K_a <= event.key <= pg.K_z) or (event.key == pg.K_SPACE) or (pg.K_0 <= event.key <= pg.K_9)) and len(self.name) < 8:
				self.name += event.unicode
			elif event.key == pg.K_BACKSPACE:
				self.name = self.name[:-1]
			elif event.key == pg.K_RETURN:
				# When the name is entered:
				# If the name length is shorter than eight characters, pad it to eight.
				if len(self.name) < 8:
					self.name += ' ' * (8 - len(self.name))
				with fh.SFH() as sfh:
					# Save the score to the score file.
					score = [c.encode() for c in str(self.name)] + [self.user.score, self.user.lines_cleared, self.eval_timer()]
					sfh.encode(self.user.gametype, score)
				# Reset the menu object and refer the user to the loss menu.
				self.name = u''
				self.user.reset()
				self.user.state = 'loss_menu'
			elif event.key == pg.K_ESCAPE:
				self.name = u''
				self.user.reset()
				self.user.state = 'loss_menu'

	@env.Menu.render
	def display_score (self, surf):
		self.render_text('You got the '+self.placestring+' place high score!', 0x000000, surf, midtop=(self.rect.w / 2, 15))
		self.render_text('Enter Name:', 0x000000, surf, topleft=(15, 40))
		self.render_text('Score:', 0x000000, surf, topright=(self.rect.w / 2 - 40, 40))
		self.render_text('Lines:', 0x000000, surf, topleft=(self.rect.w / 2 + 40, 40))
		self.render_text('Time Taken:', 0x000000, surf, topright=(self.rect.w - 25, 40))

		self.render_text(self.name, 0x000000, surf, topleft=(20, 65))
		self.render_text(str(self.user.score), 0x000000, surf, topright=(self.rect.w / 2 - 20, 65))
		self.render_text(str(self.user.lines_cleared), 0x000000, surf, topleft=(self.rect.w / 2 + 100, 65))
		_time = self.eval_timer()
		self.render_text('{}:{:02d}:{:02d}'.format(_time // 6000, _time // 100 % 60, _time % 100), 0x000000, surf, topright=(self.rect.w - 20, 65))

	def run (self):
		env.screen.blit(self.loss_bg, (0, 0))
		self.draw(env.screen)
		self.eval_input()
		self.display_score()
		pg.display.flip()

class LossMenu (env.Menu):
	"""
	When you lose the game, this menu pops up to show your score and let you try for a higher one.
	"""
	def __init__ (self, user, settings_menu):
		self.loss_bg = pg.Surface(env.screct.size)
		bg = pg.Surface((250, 300))
		bg.fill(0x7F7F00)
		super().__init__(user, bg, center=env.screct.center)
		self.settings_menu = settings_menu

		tmargin = 20
		hmargin = 15
		spacing = 5
		height = 60

		self.selections = [[
			env.MenuOption(self, 'restart', 'Try Again?', (hmargin, tmargin + spacing + height), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'settings', 'Game Settings', (hmargin, tmargin + 2 * (spacing + height)), (self.rect.w - 2 * hmargin, height)),
			env.MenuOption(self, 'quit', 'Return to Menu', (hmargin, tmargin + 3 * (spacing + height)), (self.rect.w - 2 * hmargin, height))]]

	def render_loss (self, bg):
		# To be called inside the game engine, saving relevant game data to be used.
		self.loss_score = self.user.score
		self.loss_bg.blit(bg, (0, 0))

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'restart':
					self.user.state = 'game'
					self.user.reset()
					self.user.resetgame = True
					env.restart_music()
					self.reset()
				elif self.selected.action == 'settings':
					self.settings_menu.return_state = 'loss_menu'
					self.user.state = 'settings_menu'
				elif self.selected.action == 'quit':
					self.user.state = 'main_menu'
					self.user.reset()
					self.user.resetgame = True
					self.reset()

	@env.Menu.render
	def rendered_text (self, surf):
		self.render_text("Game Over!", 0xFFFFFF, surf, midtop=(self.rect.w / 2, 15))
		self.render_text("Your score was: " + str(self.loss_score), 0xFFFFFF, surf, midtop=(self.rect.w / 2, 40))

	def run (self):
		env.screen.blit(self.loss_bg, (0, 0))
		self.draw(env.screen)
		self.rendered_text()
		super().run()
		pg.display.flip()
