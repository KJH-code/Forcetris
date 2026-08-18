"Contains subclass definitions for all menus used in the game."
try:
	import pygame as pg
	import engine.environment as env
	import engine.filehandler as fh
	import engine.controls as ctl
	import engine.userstate as us
	import engine.finesse as fin
	import engine.replay as rp
except ImportError:
	print("Something fucking jammed in here:")
	raise

# The pieces by letter, in Shape.form order.
SHAPE_LETTERS = 'IOTSZJLG'
# What each of those looks like, sampled from textures/tileset.png so the replay
# is drawn in the same colours as the game it is replaying. The last is garbage.
PIECE_COLOURS = (
	0x1BDDDF, 0xDDDF1B, 0xAF1BDF, 0x49DF1B, 0xDF1B1B, 0x1B1BDF, 0xDF7C1B, 0x7D7D7D,
)

class MainMenu (env.Menu):
	"""
	The MainMenu object represents the main menu of the game.

	The player is capable of starting the game selection, viewing the high score tables, and
	changing the options.
	"""

	rows = (
		('play', 'Start Game'),
		('help', 'How to Play'),
		('hiscore', 'High Scores'),
		('replays', 'Replays'),
		('settings', 'Game Settings'),
		('quit', 'Quit'),
	)

	def __init__ (self, user, score_menu, help_menu, settings_menu, replay_menu):
		hmargin = 15 # horizontal margin in pixels
		tmargin = 20 # top margin in pixels
		spacing = 5 # space between selections in pixels
		height = 48 # height of selections in pixels
		# Sized from the row list, so adding an entry grows the panel rather than
		# squeezing the rows into a shorter one.
		bg = pg.Surface((210, 2 * tmargin + len(self.rows) * height + (len(self.rows) - 1) * spacing))
		bg.fill(0x00FF00)
		super().__init__(user, bg, center=(env.screct.width / 2, env.screct.height / 2 + 60))
		self.score_menu = score_menu
		self.help_menu = help_menu
		self.settings_menu = settings_menu
		self.replay_menu = replay_menu

		self.selections = [[
			env.MenuOption(
				self, action, label, (hmargin, tmargin + i * (spacing + height)),
				(self.rect.w - 2 * hmargin, height))
			for i, (action, label) in enumerate(self.rows)]]

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
				elif self.selected.action == 'replays':
					# Re-read the folder on the way in, since a game played since this
					# was last opened will have added to it.
					self.replay_menu.return_state = 'main_menu'
					self.replay_menu.refresh()
					self.user.state = 'replay_menu'
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

	The forced drop timer sits on this screen as well as in the settings, because
	whether it is running is the difference between practice and a plain game, and
	that is a decision made on the way in rather than one buried two screens deep.
	It only toggles here - the length of the budget is the settings menu's job.
	"""
	# The three modes, then the switch that decides what kind of run this is.
	modes = ('arcade', 'timed', 'free')
	rows = modes + ('timer',)

	labels = {
		'arcade': 'Arcade Mode',
		'timed': 'Timed Mode',
		'free': 'Free Mode',
	}

	def __init__(self, user):
		hmargin = 20
		tmargin = 56
		spacing = 12
		height = 60
		# Sized from the row list, so a row added here cannot push the hint line off
		# the bottom of the panel.
		bg = pg.Surface((620, tmargin + len(self.rows) * (spacing + height) + 22))
		bg.fill(0x00FF40)
		super().__init__(user, bg, center=env.screct.center)

		width = self.rect.w - 2 * hmargin
		self.selections = [[
			env.MenuOption(self, action, '', (hmargin, tmargin + i * (spacing + height)), (width, height))
			for i, action in enumerate(self.rows)]]
		self.set_labels()

	def label (self, action):
		# The text drawn on a row, value included.
		if action == 'timer':
			return 'Forced Drop:  ' + (
				'{:.2f}s'.format(self.user.forced_delay) if self.user.forced_delay > 0. else 'Off')
		return self.labels[action]

	def set_labels (self):
		for option in self.selections[0]:
			option.text = self.font.render(self.label(option.action), 0, pg.Color(255, 255, 255))
			option.text_rect = option.text.get_rect(center=option.rect.center)
		# What the timer row currently reads, so run() can tell when it has gone stale.
		self.shown_delay = self.user.forced_delay

	def eval_move (self, coord, movedir):
		# Sideways on the timer row switches it rather than moving the cursor, which
		# is how every other screen in the game spells "change this value".
		if coord == 0 and self.selected.action == 'timer':
			if not self.moved:
				self.user.toggle_forced()
				ctl.save(self.user)
				self.set_labels()
				self.moved = True
			return
		if coord == 0:
			# Nothing to the left or right of a mode button. Left as a no-op rather
			# than wrapping, since one column wrapping onto itself just flickers.
			return
		super().eval_move(coord, movedir)

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'timer':
					# Confirm works here too, so the row can be used without knowing
					# that the arrow keys are what change a value.
					self.user.toggle_forced()
					ctl.save(self.user)
					self.set_labels()
					return
				self.user.gametype = self.selected.action
				self.user.state = 'game'
				self.user.resetgame = True
				env.restart_music()
				self.reset()
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = 'main_menu'

	@env.Menu.render
	def display_hint (self, surf):
		self.render_text('Start Game', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 15))
		hint = ('LEFT / RIGHT to switch, X to go back' if self.selected.action == 'timer'
			else 'Z to start, X to go back')
		self.render_text(hint, 0xE0FFE0, surf, midbottom=(self.rect.w / 2, self.rect.h - 8))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		# The settings menu can change the delay while this screen is not looking, so
		# the row is re-rendered when the number behind it has moved on.
		if self.shown_delay != self.user.forced_delay:
			self.set_labels()
		super().run()
		self.display_hint()
		pg.display.flip()

class HelpMenu (env.Menu):
	"""
	The Help Menu lists the controls and explains what the forced drop timer does,
	since that part isn't in any Tetris the player has met before.

	It has one selection, which returns to whichever menu opened it.
	"""
	# Laid out from the number of actions rather than from measured pixels, so that
	# binding a new one pushes the rest down instead of landing on top of them.
	row_top = 60
	row_step = 25
	note_lines = 4
	note_step = 22

	def __init__ (self, user):
		self.rows_end = self.row_top + len(ctl.ACTIONS) * self.row_step
		self.notes_top = self.rows_end + 40
		back_top = self.notes_top + self.note_lines * self.note_step + 16
		bg = pg.Surface((620, back_top + 60))
		bg.fill(0x2F6F8F)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'main_menu'

		self.selections = [[env.MenuOption(self, 'back', 'Back', (self.rect.w / 2 - 90, back_top), (180, 40))]]

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
			top = self.row_top + i * self.row_step
			self.render_text(ctl.describe(self.user, action), 0xFFE080, surf, topright=(250, top))
			self.render_text(name, 0xFFFFFF, surf, topleft=(275, top))
		# The part that isn't standard Tetris.
		self.render_text('Forced Drop', 0xFFFFFF, surf, midtop=(self.rect.w / 2, self.rows_end + 10))
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
			self.render_text(line, 0xFFFFFF, surf, midtop=(self.rect.w / 2, self.notes_top + i * self.note_step))

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

	# Every row the screen carries, in the order it lists them.
	rows = (
		'delay', 'ghost', 'kicks', 'tiles', 'clears', 'spins', 'finesse',
		'music', 'sound', 'controls', 'handling', 'back',
	)

	def __init__ (self, user, controls_menu, handling_menu):
		hmargin = 20
		tmargin = 50
		spacing = 5
		height = 34
		# Sized from the row list rather than by hand, so adding a setting cannot
		# quietly push the last row or the hint line off the bottom of the panel.
		bg = pg.Surface((460, tmargin + len(self.rows) * (spacing + height) + 33))
		bg.fill(0x00A060)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'main_menu'
		self.controls_menu = controls_menu
		self.handling_menu = handling_menu

		width = self.rect.w - 2 * hmargin
		self.selections = [[
			env.MenuOption(self, action, '', (hmargin, tmargin + i * (spacing + height)), (width, height))
			for i, action in enumerate(self.rows)]]
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
		elif action == 'spins':
			return 'Spins:  ' + us.SPIN_RULES[self.user.spinrule]
		elif action == 'finesse':
			return 'Finesse:  ' + us.FINESSE_RULES[self.user.finesse]
		elif action == 'music':
			return 'Music:  ' + ('{:.0%}'.format(self.user.volume) if self.user.volume > 0. else 'Off')
		elif action == 'sound':
			return 'Sound:  ' + ('{:.0%}'.format(self.user.sfx_volume) if self.user.sfx_volume > 0. else 'Off')
		elif action == 'controls':
			return 'Controls...'
		elif action == 'handling':
			return 'Handling...'
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
			if self.user.forced_delay > 0.:
				# What the toggle on the mode screen switches back on.
				self.user.forced_hold = self.user.forced_delay
		elif action == 'ghost':
			self.user.showghost = not self.user.showghost
		elif action == 'kicks':
			self.user.enablekicks = not self.user.enablekicks
		elif action == 'tiles':
			self.user.linktiles = not self.user.linktiles
		elif action == 'clears':
			self.user.cleartype = (self.user.cleartype + movedir) % len(self.clear_names)
		elif action == 'spins':
			self.user.spinrule = (self.user.spinrule + movedir) % len(us.SPIN_RULES)
		elif action == 'finesse':
			self.user.finesse = (self.user.finesse + movedir) % len(us.FINESSE_RULES)
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
		# Kept, so a value set once does not have to be set again next launch.
		ctl.save(self.user)
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
				elif self.selected.action == 'handling':
					self.handling_menu.return_state = 'settings_menu'
					self.user.state = 'handling_menu'
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
		# One row per action, plus Reset and Back, and room for the hint underneath.
		bg = pg.Surface((470, 516))
		bg.fill(0x0E7C7B)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'settings_menu'
		# The action currently waiting for a key press, or None.
		self.listening = None
		# Whether that key will replace the binding or join it.
		self.listening_adds = False

		hmargin = 20
		tmargin = 50
		spacing = 5
		height = 34
		width = self.rect.w - 2 * hmargin
		# The two trailing rows are commands, not actions, so they take no keys.
		self.bindable = frozenset(action for action, label in ctl.ACTIONS)
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
			return name + (':  Press a key to add...' if self.listening_adds else ':  Press a key...')
		return name + ':  ' + ctl.describe(self.user, action)

	def fit (self, action, width):
		# The cap on keys per action bounds how many names a row carries, but not how
		# long they are: four of the wordier ones overrun the row. Drop names off the
		# end until it fits, and say how many went.
		text = self.label(action)
		if action not in self.bindable or self.listening == action:
			return text
		codes = list(self.user.keys.get(action, ()))
		if not codes:
			return text
		name = dict(ctl.ACTIONS)[action]
		for shown in range(len(codes), 0, -1):
			keys = ', '.join(ctl.key_name(code) for code in codes[:shown])
			if shown < len(codes):
				keys += ' +{}'.format(len(codes) - shown)
			text = '{}:  {}'.format(name, keys)
			if self.font.size(text)[0] <= width:
				break
		return text

	def set_labels (self):
		for option in self.selections[0]:
			text = self.fit(option.action, option.rect.w - 12)
			option.text = self.font.render(text, 0, pg.Color(255, 255, 255))
			option.text_rect = option.text.get_rect(center=option.rect.center)

	def listen (self, adds):
		# Wait for the next key press. Adding is refused once the action is full,
		# rather than opening a prompt that would quietly discard the key.
		# setdir stops the cursor drifting on the key that opened the prompt.
		self.setdir()
		if adds and len(self.user.keys.get(self.selected.action, ())) >= ctl.MAX_KEYS:
			return
		self.listening = self.selected.action
		self.listening_adds = adds
		self.set_labels()

	def eval_input (self):
		if self.listening is not None:
			# Swallow input while waiting for a key, so that arrows and Enter can be
			# bound rather than moving the cursor or re-triggering the row.
			event = pg.event.poll()
			if event.type == pg.QUIT:
				self.user.state = 'quit'
			elif event.type == pg.KEYDOWN:
				if event.key != pg.K_ESCAPE:
					ctl.bind(self.user, self.listening, event.key, not self.listening_adds)
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
					self.listen(False)
			elif event.key == pg.K_RIGHT and self.selected.action in self.bindable:
				# Left and Right move nothing on a single-column menu, so they are free
				# to grow and trim the binding instead.
				self.listen(True)
			elif event.key == pg.K_LEFT and self.selected.action in self.bindable:
				ctl.unbind_last(self.user, self.selected.action)
				self.setdir()
				self.set_labels()
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = self.return_state
				self.reset()

	@env.Menu.render
	def display_hint (self, surf):
		self.render_text('Controls', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 15))
		if self.listening is not None:
			hint = 'Press the key to bind, or Escape to cancel'
		elif (self.selected.action in self.bindable
			and len(self.user.keys.get(self.selected.action, ())) >= ctl.MAX_KEYS):
			hint = 'Full at {} keys - LEFT removes one, Z starts over'.format(ctl.MAX_KEYS)
		else:
			hint = 'Z rebind, RIGHT add a key, LEFT remove one, X back'
		self.render_text(hint, 0xD0FFFF, surf, midbottom=(self.rect.w / 2, self.rect.h - 12))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		self.display_hint()
		pg.display.flip()

class HandlingMenu (env.Menu):
	"""
	TETR.IO's handling knobs: DAS, ARR, DCD and the soft drop factor.

	Left and Right change the highlighted value, exactly as in the settings menu.
	The millisecond settings land on a 20ms grid because the game runs at a fixed
	50 frames per second and auto-shift can only act on a frame boundary.
	"""
	def __init__ (self, user):
		# One row per handling setting, plus Reset and Back, and room for the hint.
		bg = pg.Surface((470, 382))
		bg.fill(0x1F5F9F)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'settings_menu'

		hmargin = 20
		tmargin = 50
		spacing = 6
		height = 36
		width = self.rect.w - 2 * hmargin
		rows = [name for name, label, unit, low, high, step in ctl.HANDLING] + ['reset', 'back']

		self.selections = [[
			env.MenuOption(self, name, '', (hmargin, tmargin + i * (spacing + height)), (width, height))
			for i, name in enumerate(rows)]]
		self.set_labels()

	def label (self, name):
		if name == 'reset':
			return 'Reset to Defaults'
		elif name == 'back':
			return 'Back'
		title = dict((n, l) for n, l, u, lo, hi, st in ctl.HANDLING)[name]
		return '{}:  {}'.format(title, ctl.describe_handling(self.user, name))

	def set_labels (self):
		for option in self.selections[0]:
			option.text = self.font.render(self.label(option.action), 0, pg.Color(255, 255, 255))
			option.text_rect = option.text.get_rect(center=option.rect.center)

	def adjust (self, movedir):
		name = self.selected.action
		for key, title, unit, low, high, step in ctl.HANDLING:
			if key == name:
				ctl.set_handling(self.user, name, getattr(self.user, name) + movedir * step)
				self.set_labels()
				return

	def eval_move (self, coord, movedir):
		# Horizontal movement edits the highlighted value rather than moving the
		# cursor, reusing the auto-repeat timing the base class already runs.
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
				elif self.selected.action == 'reset':
					for name, value in ctl.HANDLING_DEFAULTS.items():
						ctl.set_handling(self.user, name, value)
					self.set_labels()
				else:
					self.adjust(1)
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = self.return_state
				self.reset()

	@env.Menu.render
	def display_hint (self, surf):
		self.render_text('Handling', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 15))
		self.render_text('LEFT / RIGHT to change, X to go back', 0xD0E8FF, surf, midbottom=(self.rect.w / 2, self.rect.h - 12))

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
	rows = (
		('restart', 'Try Again?'),
		('analysis', 'Analysis'),
		('settings', 'Game Settings'),
		('quit', 'Return to Menu'),
	)

	def __init__ (self, user, settings_menu, analysis_menu):
		tmargin = 20
		hmargin = 15
		spacing = 5
		height = 52
		self.loss_bg = pg.Surface(env.screct.size)
		# One row taller than the base game's, so sized from the list.
		bg = pg.Surface((250, tmargin + (len(self.rows) + 1) * (spacing + height) + tmargin))
		bg.fill(0x7F7F00)
		super().__init__(user, bg, center=env.screct.center)
		self.settings_menu = settings_menu
		self.analysis_menu = analysis_menu

		self.selections = [[
			env.MenuOption(
				self, action, label, (hmargin, tmargin + (i + 1) * (spacing + height)),
				(self.rect.w - 2 * hmargin, height))
			for i, (action, label) in enumerate(self.rows)]]

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
				elif self.selected.action == 'analysis':
					self.analysis_menu.return_state = 'loss_menu'
					self.user.state = 'analysis_menu'
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

class AnalysisMenu (env.Menu):
	"""
	What the run was actually made of, once it is over.

	The finesse counter on the HUD says how you are doing; this says what you did.
	The two figures either side of it are the point: what the run cost in presses,
	and what it would have cost had every placement been made in the fewest.
	"""
	def __init__ (self, user, replay_menu):
		bg = pg.Surface((470, 470))
		bg.fill(0x203040)
		super().__init__(user, bg, center=env.screct.center)
		self.replay_menu = replay_menu
		self.return_state = 'loss_menu'
		self.replay = None
		self.stats = None
		self.fixed = None
		self.smallfont = pg.font.SysFont(None, 21)

		hmargin = 20
		height = 34
		width = self.rect.w - 2 * hmargin
		self.selections = [[
			env.MenuOption(self, 'watch', 'Watch Replay', (hmargin, 386), (width, height)),
			env.MenuOption(self, 'back', 'Back', (hmargin, 386 + height + 6), (width, height))]]

	def show (self, replay):
		# Handed the finished recording when a game ends. A game too short to be
		# worth a file leaves the screen with nothing, which it says rather than
		# pretending to have numbers.
		self.replay = replay
		self.stats = None if replay is None else replay.summary()
		self.fixed = None if replay is None else replay.summary(fixed=True)
		self.reset()

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				if self.selected.action == 'watch' and self.replay is not None:
					self.replay_menu.watch(self.replay, self.user.state)
				else:
					self.user.state = self.return_state
					self.reset()
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = self.return_state
				self.reset()

	def rows (self):
		# Left column, right column. Built here rather than drawn inline so the
		# layout check can count them.
		if self.stats is None:
			return []
		s, f = self.stats, self.fixed
		clears = s['clears']
		return [
			('Score', '{}'.format(s['score'])),
			('Pieces', '{}  ({:.2f}/s)'.format(s['placements'], s['pps'])),
			('Lines', '{}'.format(s['lines'])),
			('Time', '{:.0f}:{:04.1f}'.format(s['seconds'] // 60, s['seconds'] % 60)),
			('', ''),
			('Finesse', '{:.1%}'.format(s['rate'])),
			('Faults', '{} over {} judged'.format(s['faults'], s['judged'])),
			('Key presses', '{}  ({:.2f}/piece)'.format(s['presses'], s['ppp'])),
			('Without faults', '{}  ({:.2f}/piece)'.format(f['presses'], f['ppp'])),
			('Wasted', '{} press{}'.format(s['wasted'], '' if s['wasted'] == 1 else 'es')),
			('', ''),
			('Clears', ' '.join(
				'{}x{}'.format(count, rp.CLEAR_NAMES.get(size, '{}L'.format(size)))
				for size, count in sorted(clears.items())) or 'none'),
			('Spins', '{}'.format(s['spins'])),
			('Perfect clears', '{}'.format(s['perfects'])),
			('Best B2B / combo', '{} / {}'.format(s['best_b2b'], s['best_combo'])),
		]

	@env.Menu.render
	def display_body (self, surf):
		self.render_text('Analysis', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 12))
		if self.stats is None:
			self.render_text(
				'That run was too short to record.', 0xC0C0C0, surf,
				midtop=(self.rect.w / 2, 60))
			return
		top = 44
		for i, (name, value) in enumerate(self.rows()):
			if not name:
				continue
			y = top + i * 23
			self.render_text(name, 0xA8C0D8, surf, topleft=(24, y))
			self.render_text(value, 0xFFFFFF, surf, topright=(self.rect.w - 24, y))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		self.display_body()
		pg.display.flip()

class ReplayMenu (env.Menu):
	"""
	The saved replays, newest first, and the screen that plays one back.

	Both live here because the list is only ever a way into the viewer, and a
	separate screen for four lines of file names would be four lines of file names
	on its own screen.
	"""
	# Placements advanced per second when playing rather than stepping.
	speeds = (2., 4., 8.)
	rows_shown = 8

	def __init__ (self, user):
		bg = pg.Surface((560, 420))
		bg.fill(0x203040)
		super().__init__(user, bg, center=env.screct.center)
		self.return_state = 'main_menu'
		self.smallfont = pg.font.SysFont(None, 21)
		self.replays = []
		self.top = 0
		self.viewer = ReplayViewer(user, self)
		self.selections = [[env.MenuOption(self, 'none', '', (20, 44), (self.rect.w - 40, 30))]]
		self.refresh()

	def refresh (self):
		# Re-read the folder every time the screen is opened, since a game played
		# since it was last looked at will have added to it.
		self.replays = rp.listing()
		hmargin = 20
		height = 34
		spacing = 4
		width = self.rect.w - 2 * hmargin
		count = max(1, min(len(self.replays), self.rows_shown))
		self.selections = [[
			env.MenuOption(self, 'row{}'.format(i), '', (hmargin, 52 + i * (height + spacing)), (width, height))
			for i in range(count)]]
		self.top = 0
		self.reset()
		self.set_labels()

	def set_labels (self):
		for i, option in enumerate(self.selections[0]):
			index = self.top + i
			text = self.replays[index].title() if index < len(self.replays) else ''
			option.text = self.smallfont.render(text or 'No replays yet', 0, pg.Color(255, 255, 255))
			option.text_rect = option.text.get_rect(center=option.rect.center)

	def chosen (self):
		index = self.top + self.selection[1]
		return self.replays[index] if index < len(self.replays) else None

	def watch (self, replay, return_state):
		# Open the viewer on a replay, remembering where to go back to. The
		# analysis screen uses this too, which is why it is not private.
		self.viewer.load(replay, return_state)
		self.user.state = 'replay_viewer'

	def eval_move (self, coord, movedir):
		if coord == 0:
			return
		super().eval_move(coord, movedir)
		# Scroll the window when the cursor runs off either end of it.
		shown = len(self.selections[0])
		if self.selection[1] == shown - 1 and self.top + shown < len(self.replays) and movedir > 0:
			self.top += 1
			self.selection[1] = shown - 1
		elif self.selection[1] == 0 and self.top > 0 and movedir < 0:
			self.top -= 1
		self.set_labels()

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				replay = self.chosen()
				if replay is not None:
					self.watch(replay, 'replay_menu')
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.user.state = self.return_state
				self.reset()

	@env.Menu.render
	def display_hint (self, surf):
		self.render_text('Replays', 0xFFFFFF, surf, midtop=(self.rect.w / 2, 12))
		if len(self.replays) > len(self.selections[0]):
			self.render_text(
				'{} of {}'.format(self.top + 1, len(self.replays)), 0xA8C0D8, surf,
				topright=(self.rect.w - 20, 16))
		self.render_text(
			'Z to watch, X to go back', 0xC8D8E8, surf,
			midbottom=(self.rect.w / 2, self.rect.h - 10))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		super().run()
		self.display_hint()
		pg.display.flip()

class ReplayViewer (env.Menu):
	"""
	Re-enacts a replay: the piece is walked to where it went, stop by stop.

	The boards come from the snapshots each placement carries rather than being
	re-simulated, so what settles is what settled. The piece on top of them is
	animated from the movement trail recorded alongside - where it stood after
	each press - so watching it back shows the placement being made, not just
	its result.

	Turning the fix on swaps that trail for the finesse route. The piece is the
	same piece, it arrives in the same column in the same orientation, and the
	board it leaves behind is the same board; it simply stops fewer times on the
	way. That is the whole of what better finesse would have changed, so it is
	the whole of what the corrected replay changes.

	Placements finesse has no opinion about - tucks, spins, and any the timer
	took - keep the player's own path in both views. There is no route to hold
	them to, and inventing one would be inventing a mistake.
	"""
	cell = 18
	# The queue and the held piece are drawn small, in their own column between
	# the board and the readout.
	mini = 10
	speeds = (1., 2., 4., 8.)

	def __init__ (self, user, replay_menu):
		bg = pg.Surface((760, 540))
		bg.fill(0x101820)
		super().__init__(user, bg, center=env.screct.center)
		self.replay_menu = replay_menu
		self.return_state = 'replay_menu'
		self.smallfont = pg.font.SysFont(None, 21)
		self.replay = None
		self.index = 0
		self.step = 0
		self.playing = False
		self.speed = 1
		self.fixed = False
		self.carry = 0.
		self.selections = [[env.MenuOption(self, 'view', '', (0, 0), (1, 1))]]

	def load (self, replay, return_state='replay_menu'):
		self.replay = replay
		self.return_state = return_state
		self.index = 0
		self.step = 0
		self.playing = False
		self.carry = 0.

	def here (self):
		if self.replay is None or not len(self.replay):
			return None
		return self.replay.placements[min(self.index, len(self.replay) - 1)]

	def stops (self, index=None):
		# Where the piece stands at each stage of the placement being watched.
		place = self.replay.placements[self.index if index is None else index]
		return place.steps(self.fixed)

	def step_count (self, index=None):
		# One stage per stop, plus one for the board it settles into afterwards.
		return len(self.stops(index)) + 1

	def clamp (self):
		# Keep the cursor on a stage that exists, which matters after the fix is
		# toggled: the corrected path usually has fewer stops than the real one.
		if self.replay is None or not len(self.replay):
			self.index = self.step = 0
			return
		self.index = max(0, min(len(self.replay) - 1, self.index))
		self.step = max(0, min(self.step_count() - 1, self.step))

	def advance_step (self, by):
		# One stage forward or back, running on into the next placement or back
		# into the last one rather than stopping at the seam.
		if self.replay is None or not len(self.replay):
			return False
		self.clamp()
		self.step += by
		if self.step >= self.step_count():
			if self.index >= len(self.replay) - 1:
				self.step = self.step_count() - 1
				return False
			self.index += 1
			self.step = 0
		elif self.step < 0:
			if self.index <= 0:
				self.step = 0
				return False
			self.index -= 1
			self.step = self.step_count() - 1
		return True

	def jump_piece (self, by):
		if self.replay is None or not len(self.replay):
			return
		self.index = max(0, min(len(self.replay) - 1, self.index + by))
		self.step = 0

	def eval_move (self, coord, movedir):
		# Sideways walks the placement stage by stage and repeats when held; up and
		# down jump a whole piece. The cursor never moves - there is one thing on
		# this screen and it is the board.
		if coord == 0:
			if not self.moved:
				self.advance_step(movedir)
				self.moved = True
			else:
				self.movetime -= 1
				if self.movetime < 1:
					self.advance_step(movedir)
					self.movetime = 3
			return
		if not self.moved:
			self.jump_piece(movedir)
			self.moved = True

	def eval_input (self):
		event = super().eval_input()
		if event.type == pg.KEYDOWN:
			if event.key == pg.K_z or event.key == pg.K_RETURN:
				self.playing = not self.playing
				self.carry = 0.
				if self.playing and self.at_end():
					# Play from the top rather than sitting on the last frame doing
					# nothing, which is what pressing play on a finished replay means.
					self.index = self.step = 0
			elif event.key == pg.K_f:
				self.fixed = not self.fixed
				self.clamp()
			elif event.key == pg.K_s:
				self.speed = (self.speed + 1) % len(self.speeds)
			elif event.key == pg.K_x or event.key == pg.K_ESCAPE:
				self.playing = False
				self.user.state = self.return_state
				if self.return_state == 'replay_menu':
					self.replay_menu.reset()

	def at_end (self):
		return (
			self.replay is not None and len(self.replay)
			and self.index >= len(self.replay) - 1
			and self.step >= self.step_count() - 1)

	def advance (self):
		# Called once a frame while playing. Stages are the unit of time here, so a
		# piece that took more presses genuinely takes longer to watch, which is the
		# difference the corrected view is there to show.
		if not self.playing or self.replay is None or not len(self.replay):
			return
		self.carry += self.speeds[self.speed] * 4. / 50.
		while self.carry >= 1.:
			self.carry -= 1.
			if not self.advance_step(1):
				self.playing = False
				break

	def draw_cells (self, surf, left, top, rows, form=None, cells=()):
		# The settled board, and the piece standing on top of it if there is one.
		wide = len(rows[0]) if rows else 10
		pg.draw.rect(
			surf, pg.Color(0x18, 0x20, 0x2C),
			(left - 2, top - 2, wide * self.cell + 4, len(rows) * self.cell + 4))
		for y, row in enumerate(rows):
			for x, mark in enumerate(row):
				if mark == '.':
					continue
				pg.draw.rect(
					surf, env.convert_hexcolor(PIECE_COLOURS[int(mark) % len(PIECE_COLOURS)]),
					(left + x * self.cell + 1, top + y * self.cell + 1, self.cell - 2, self.cell - 2))
		if form is None:
			return
		colour = env.convert_hexcolor(PIECE_COLOURS[form % len(PIECE_COLOURS)])
		for x, y in cells:
			if not (0 <= x < wide and 0 <= y < len(rows)):
				continue
			box = (left + x * self.cell + 1, top + y * self.cell + 1, self.cell - 2, self.cell - 2)
			pg.draw.rect(surf, colour, box)
			# Outlined, so the piece still in motion reads as separate from the
			# stack it is about to join.
			pg.draw.rect(surf, pg.Color(0xFF, 0xFF, 0xFF), box, 2)

	def fit (self, font, presses, width):
		# The press list, shortened from the end until it sits inside the panel.
		# A placement can take a lot of presses - that is rather the point of the
		# screen - and a line that runs off the edge tells the player nothing.
		text = fin.brief(presses)
		if font.size(text)[0] <= width:
			return text
		for keep in range(len(presses) - 1, 0, -1):
			text = '{} +{}'.format(fin.brief(presses[:keep]), len(presses) - keep)
			if font.size(text)[0] <= width:
				return text
		return '{} presses'.format(len(presses))

	def piece_cells (self, place, stop):
		state, x, y = stop
		return [(x + dx, y + dy) for dx, dy in fin.offsets(place.form, state)]

	def draw_mini (self, surf, form, left, top, width):
		# One piece drawn small, centred in a box of the given width, in its spawn
		# orientation - which is how the game's own queue shows it.
		if form is None or not 0 <= form < 7:
			return
		cells = fin.offsets(form, 0)
		lox = min(dx for dx, dy in cells)
		loy = min(dy for dx, dy in cells)
		span = (max(dx for dx, dy in cells) - lox + 1) * self.mini
		pad = max(0, (width - span) // 2)
		colour = env.convert_hexcolor(PIECE_COLOURS[form % len(PIECE_COLOURS)])
		for dx, dy in cells:
			pg.draw.rect(surf, colour, (
				left + pad + (dx - lox) * self.mini + 1,
				top + (dy - loy) * self.mini + 1,
				self.mini - 2, self.mini - 2))

	def draw_queue (self, surf, place, left, top, width):
		"""What the player could see coming while they made this placement.

		Taken from the queue recorded with the placement rather than from the
		placements that follow it. A hold reorders those, so the pieces played next
		are not the pieces that were shown next, and showing the wrong three would
		quietly misrepresent the decision being watched.
		"""
		y = top
		self.render_text('Hold', 0xA8C0D8, surf, topleft=(left, y))
		y += 20
		if place.stored is not None and place.stored < 7:
			self.draw_mini(surf, place.stored, left, y, width)
		else:
			self.render_text('-', 0x60707C, surf, topleft=(left + width // 2 - 4, y))
		y += 46
		queue = place.queue
		if not queue:
			# A replay recorded before the queue was kept. Saying so beats drawing
			# three empty boxes that look like an empty bag.
			self.render_text('Next', 0xA8C0D8, surf, topleft=(left, y))
			self.render_text('n/a', 0x60707C, surf, topleft=(left, y + 20))
			return
		self.render_text('Next', 0xA8C0D8, surf, topleft=(left, y))
		y += 20
		for form in queue[:3]:
			self.draw_mini(surf, form, left, y, width)
			y += 46

	@env.Menu.render
	def display_body (self, surf):
		place = self.here()
		self.render_text('Replay', 0xFFFFFF, surf, topleft=(24, 14))
		if self.replay is None or place is None:
			self.render_text('Nothing to play back.', 0xC0C0C0, surf, topleft=(24, 48))
			return
		self.clamp()
		stops = self.stops()
		settled = self.step >= len(stops)
		self.render_text(
			'piece {} / {}'.format(self.index + 1, len(self.replay)), 0xA8C0D8, surf,
			topright=(self.rect.w - 24, 14))

		if settled:
			self.draw_cells(surf, 24, 44, rp.padded(place.rows))
		else:
			rows = rp.padded(self.replay.before(self.index))
			self.draw_cells(
				surf, 24, 44, rows, place.form, self.piece_cells(place, stops[self.step]))

		queue_left = 24 + 10 * self.cell + 16
		queue_width = 52
		self.draw_queue(surf, place, queue_left, 44, queue_width)

		panel = queue_left + queue_width + 22
		presses = place.presses_shown(self.fixed)
		# What is left for a value once the label column has had its share.
		room = self.rect.w - 24 - (panel + 96)
		lines = [
			('Piece', '{}{}'.format(SHAPE_LETTERS[place.form], ' (held)' if place.held else ''), 0xFFFFFF),
			('Column', '{}'.format(place.x), 0xFFFFFF),
			('Presses', self.fit(self.smallfont, presses, room), 0xFFFFFF),
			('Count', '{}'.format(len(presses)), 0xFFFFFF),
		]
		if settled:
			doing = 'settled'
		elif self.step == 0:
			doing = 'spawned'
		else:
			doing = fin.MOVE_NAMES.get(
				presses[self.step - 1] if self.step - 1 < len(presses) else '', 'dropping')
		lines.append(('Doing', doing, 0x7CFF8A if self.fixed else 0xFFFFFF))
		if place.forced:
			lines.append(('Verdict', 'timer took it, left as played', 0xFFC040))
		elif not place.judged:
			lines.append(('Verdict', 'tuck or spin, left as played', 0xA8C0D8))
		elif self.fixed:
			lines.append(('Verdict', 'played by the book', 0x7CFF8A))
		elif place.fault:
			lines.append(('Verdict', '{} press{} wasted'.format(
				place.wasted, '' if place.wasted == 1 else 'es'), 0xFF7B7B))
		else:
			lines.append(('Verdict', 'clean', 0x7CFF8A))
		if place.lines:
			lines.append(('Cleared', '{}{}'.format(
				rp.CLEAR_NAMES.get(place.lines, '{} lines'.format(place.lines)),
				' + PC' if place.perfect else ''), 0xFFD24A))
		if place.spin:
			lines.append(('Spin', place.spin, 0xFFD24A))
		lines.append(('Score', '{}'.format(place.score), 0xFFFFFF))

		for i, (name, value, colour) in enumerate(lines):
			y = 48 + i * 24
			self.render_text(name, 0xA8C0D8, surf, topleft=(panel, y))
			if name == 'Presses':
				# The one line that can be long enough to need the smaller face.
				line = self.smallfont.render(value, 0, env.convert_hexcolor(colour))
				surf.blit(line, line.get_rect(topleft=(panel + 96, y + 3)))
			else:
				self.render_text(value, colour, surf, topleft=(panel + 96, y))

		summary = self.replay.summary(self.fixed)
		y = 48 + (len(lines) + 1) * 24
		self.render_text(
			'Whole run, as {}'.format('corrected' if self.fixed else 'played'),
			0xFFFFFF, surf, topleft=(panel, y))
		for i, (name, value) in enumerate((
			('Finesse', '{:.1%}'.format(summary['rate'])),
			('Presses', '{}'.format(summary['presses'])),
			('Per piece', '{:.2f}'.format(summary['ppp'])),
		)):
			self.render_text(name, 0xA8C0D8, surf, topleft=(panel, y + 24 + i * 22))
			self.render_text(value, 0xFFFFFF, surf, topleft=(panel + 96, y + 24 + i * 22))

		self.render_text(
			'{}  |  {:g}x'.format('Playing' if self.playing else 'Paused', self.speeds[self.speed]),
			0xC8D8E8, surf, bottomleft=(24, self.rect.h - 32))
		self.render_text(
			'F: play it by the book  [{}]'.format('on' if self.fixed else 'off'),
			0x7CFF8A if self.fixed else 0xC8D8E8, surf, bottomright=(self.rect.w - 24, self.rect.h - 32))
		self.render_text(
			'LEFT / RIGHT a move, UP / DOWN a piece, Z play, S speed, F fix, X back',
			0xC8D8E8, surf, midbottom=(self.rect.w / 2, self.rect.h - 10))

	def run (self):
		self.menu_bg.draw(env.screen)
		self.draw(env.screen)
		self.advance()
		super().run()
		self.display_body()
		pg.display.flip()
