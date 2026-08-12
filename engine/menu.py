"Contains subclass definitions for all menus used in the game."
try:
	import pygame as pg
	import engine.environment as env
	import engine.filehandler as fh
except ImportError:
	print("Something fucking jammed in here:")
	raise

class MainMenu (env.Menu):
	"""
	The MainMenu object represents the main menu of the game.

	The player is capable of starting the game selection, viewing the high score tables, and
	changing the options.
	"""

	def __init__ (self, user, score_menu):
		bg = pg.Surface((210, 300))
		bg.fill(0x00FF00)
		super().__init__(user, bg, midtop=(env.screct.width / 2, 250))
		self.score_menu = score_menu

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
					pass
				elif self.selected.action == 'hiscore':
					self.user.state = 'score_menu'
					# Load the scores into the score menu every time it is selected so the scores are up to date.
					with fh.SFH() as sfh:
						self.score_menu.scorelist = sfh.decode()
				elif self.selected.action == 'options':
					pass
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
				pg.mixer.music.play()
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
	LEFT and RIGHT arrow keys to shift tetrimino left and right.
	DOWN arrow key to speed up falling tetrimino.
	Z or LCTRL keys to rotate tetrimino counter-clockwise.
	X or UP keys to rotate tetrimino clockwise.
	SPACE key to drop tetrimino, and LSHIFT to hold tetrimino.
	ESCAPE key to pause.
	"""
	def __init__(self, user):
		pass		

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
	"""
	def __init__(self, user):
		pass

class PauseMenu (env.Menu):
	"""
	Pauses the game.
	The timer doesn't run while paused.
	"""

	def __init__(self, user):
		self.pause_bg = pg.Surface(env.screct.size)
		bg = pg.Surface((250, 300))
		bg.fill(0x00FF00)
		super().__init__(user, bg, center=env.screct.center)
		
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
					pass
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
	def __init__ (self, user):
		self.loss_bg = pg.Surface(env.screct.size)
		bg = pg.Surface((250, 300))
		bg.fill(0x7F7F00)
		super().__init__(user, bg, center=env.screct.center)

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
					pass
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
