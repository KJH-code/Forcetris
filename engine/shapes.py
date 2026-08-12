"Contains class definitions for objects that handle pyTetris block logic."
try:
	import random
	import operator
	import itertools
	import pygame as pg
	import engine.environment as env
except ImportError:
	print("Are the blocks fucking made of soap?:")
	raise

class Block (env.FreeSprite):
	"""
	Blocks are the individual pieces that make up tetriminos and the matrix grid.

	The block object supports copying of its data to other blocks.
	"""
	block_src = env.load_image('tileset.png')

	def __init__(self, relpos, color, links=[], ghost=False, fallen=False):
		super().__init__(self.block_src, (0, 0, 25, 25))
		self.relpos = relpos # Position of the block relative to a predefined point on the grid, or the "center" of the shape.
		self.color = color # Block color.
		self.links = links # Which direction a block is linked to. 0, 1, 2, 3, for U, R, D, L respectively.
		self.fallen = fallen # Used by the line clear function.
		self.update(ghost)

	def __getattr__ (self, name):
		if name == 'linkhash':
			# Returns a single number representing all the links.
			return hex(sum(map(lambda x: 2 ** x, self.links)))[2]

	def __repr__ (self):
		# eval() usable expression to create a block similar to this.
		return "Block("+repr(self.relpos)+", "+str(self.color)+", "+repr(self.links)+")"

	def __hash__ (self):
		return object.__hash__(self)

	def __eq__ (self, other):
		# If two blocks would use the same texture they're equal regardless of other internal values.
		if isinstance(other, Block):
			return self.cliprect == other.cliprect

	def copy (self, ghost=False, fallen=False):
		# Create new Block object that is the same as this one.
		return Block(self.relpos[:], self.color, self.links[:], ghost, fallen)

	def update (self, ghost=False):
		# Evaluate the graphic to be used. Not much else to update in Block Sprites.
		self.cliprect.topleft = ((self.color*50) + (25*int(ghost)), (int(self.linkhash, 16) if env.user.linktiles else 0) * 25)

class Shape (env.FreeGroup):
	"""
	Shape objects are groups of four blocks each treated as one independent structure.
	During non-naive line clears, temporary aggregate shapes are created to handle falling of the separate block domains.

	The position will always start at the left middle column, at the second invisible row from the top.
	This position is also the center of the shape's rotation if the shape is T, S, Z, J, or L.

	If I, the position represents the topleft center block of its bounding square. If O, the position
	represents the bottomleft corner.

	If this is a temporary aggregate of blocks during clears, the position is used as a reference point for translation.
	"""
	def __init__ (self, form=7, state=0, pos=[4, 1], ghost=False):
		super().__init__()
		self.pos = pos # Center of rotation.
		self.form = form # Tetrimino shape.
		self.state = state # Current rotation relative to spawn rotation.
		self.ghost = ghost # Does this shape use ghost textures?
		if form == 0: # I
			self.add([
				Block([-1, 0], form, [1]),
				Block([ 0, 0], form, [1, 3]),
				Block([ 1, 0], form, [1, 3]),
				Block([ 2, 0], form, [3])
			])
		elif form == 1: # O
			self.add([
				Block([ 0,-1], form, [1, 2]),
				Block([ 1,-1], form, [2, 3]),
				Block([ 1, 0], form, [3, 0]),
				Block([ 0, 0], form, [0, 1])
			])
		elif form == 2: # T
			self.add([
				Block([-1, 0], form, [1]),
				Block([ 0,-1], form, [2]),
				Block([ 1, 0], form, [3]),
				Block([ 0, 0], form, [0, 1, 3])
			])
		elif form == 3: # S
			self.add([
				Block([-1, 0], form, [1]),
				Block([ 0, 0], form, [0, 3]),
				Block([ 0,-1], form, [1, 2]),
				Block([ 1,-1], form, [3])
			])
		elif form == 4: # Z
			self.add([
				Block([-1,-1], form, [1]),
				Block([ 0,-1], form, [2, 3]),
				Block([ 0, 0], form, [0, 1]),
				Block([ 1, 0], form, [3])
			])
		elif form == 5: # J
			self.add([
				Block([-1,-1], form, [2]),
				Block([-1, 0], form, [0, 1]),
				Block([ 0, 0], form, [1, 3]),
				Block([ 1, 0], form, [3])
			])
		elif form == 6: # L
			self.add([
				Block([-1, 0], form, [1]),
				Block([ 0, 0], form, [1, 3]),
				Block([ 1, 0], form, [0, 3]),
				Block([ 1,-1], form, [2])
			])
		# Pre-rotation for non-spawn state values.
		if state > 0:
			if state < 3:
				self.rotate(True)
				if state == 2:
					self.rotate(True)
			else:
				self.rotate(False)
			self.state = state

	def __getattr__ (self, name):
		if name == 'blocks':
			# Explicit reference to the list of blocks contained in this group.
			return [block for block in self]
		elif name == 'poslist':
			# List of grid coordinates for the blocks' true positions.
			return [[self.pos[i]+block.relpos[i] for i in range(2)] for block in self]

	def __str__ (self):
		# Full debug information.
		return (
			("Ghost " if self.ghost else '')+"Shape "+("IOTSZJLX"[self.form])+":\n"
			"Rotational centre at "+repr(self.pos)+" with "+(["up", "right", "down", "left"][self.state])+"ward orientation.\n"
			"Block Grid Positions: "+repr(self.poslist)+"\n"
		)

	def copy (self, ghost=False):
		# Copy this shape, creating a new Shape object in the process.
		if self.form < 7:
			return Shape(self.form, self.state, self.pos, ghost)
		else:
			dest = Shape(self.form, self.state, self.pos, ghost)
			dest.empty()
			for block in self: dest.add(block)
			return dest

	def rotate (self, clockwise):
		# SRS implementation of Tetrimino rotation. It is done relative to shape.pos unless it's an I.
		# If clockwise parameter is True, the rotation is as named. Otherwise it's counter-clockwise.
		# Shape.state tracks current orientation for the wall kick implementation.
		if clockwise:
			if self.state < 3: self.state += 1
			else: self.state = 0
		else:
			if self.state > 0: self.state -= 1
			else: self.state = 3
		# Actually rotating the shape.
		if self.form != 1: # O shapes don't need to be rotated.
			for block in self:
				# Center of rotation is different for I shapes.
				block.relpos = [block.relpos[i] - 0.5 if self.form < 1 else block.relpos[i] for i in range(2)]
				# Apply transformation using precalculated rotation matrices.
				if clockwise:
					tmp = -1 * block.relpos[1], block.relpos[0]
					block.links = [link + 1 if link < 3 else 0 for link in block.links]
				else:
					tmp = block.relpos[1], -1 * block.relpos[0]
					block.links = [link - 1 if link > 0 else 3 for link in block.links]
				block.update(self.ghost)
				block.relpos = [int(tmp[i] + 0.5) if self.form < 1 else tmp[i] for i in range(2)]

	def translate (self, disp):
		# Move the tetrimino given a displacement.
		self.pos = [self.pos[0]+disp[0], self.pos[1]+disp[1]]
			
	def draw (self, anchor=None, forced=False):
		# The anchor is the coordinate of the topleft pixel of the tile represented in self.pos.
		# Draw the tetrimino to the screen.
		if anchor is None:
			anchor = [275 + self.pos[0]*25, 10 + self.pos[1]*25]
		elif forced:
			if self.form < 1:
				anchor[1] -= 12
			elif self.form > 1:
				anchor[0] += 12
		for pos, block in zip(self.poslist, self.blocks):
			if pos[1] > 1 or forced:
				block.set(topleft=(anchor[0] + block.relpos[0]*block.rect.w, anchor[1] + block.relpos[1]*block.rect.h))
				block.update(self.ghost)
				block.draw()

	def update (self):
		raise NotImplementedError("Updates to shapes are handled by the free/new distinction.")

class ClearSprite (env.AnimatedSprite):
	"Sprite that performs the clearing animation."
	src = env.load_image('clear.png', colorkey=0xFF00FF)

	def __init__(self, **anchors):
		super().__init__(pg.Surface((250, 25)), cliprect=(0, 0, 25, 25), **anchors)
		self.image.set_colorkey(0xFF00FF)
		# Due to the way env.AnimatedSprite.animate() works, change frames when the total sheet frame count changes.
		self.frames = 6

	def draw (self, surf=env.screen):
		# Somewhat cheaty due to the rudimentary graphics, will probably fix later.
		self.image.fill(0xFF00FF)
		for i in range(10): self.image.blit(self.src, (i * 25, 0), self.cliprect)
		surf.blit(self.image, self.rect)

class Grid (env.AnimatedSprite):
	"""
	The Matrix upon which the game is played. When shapes fall and can no longer be moved,
	their blocks are stored here. Fallen block colors and links are kept.

	The Grid object extends beyond the visible playing field 2 blocks upward and 1 downward,
	where the upward rows are empty and the downward row is full. This minimizes the number of checks required.
	"""
	image = env.load_image('display.png', colorkey=0x000000)
	rect = image.get_rect(midbottom=(env.screct.centerx, env.screct.bottom - 20))

	def __init__(self, user):
		super().__init__(self.image, self.rect)
		self.user = user
		self.csprts = [ ]
		self.set_cells()

	def __str__ (self):
		# Dump information.
		return (
			"Grid Colormap and Linkmap:\n"
			""+''.join(
				[
					''.join([str(block.color) if isinstance(block, Block) else ' ' for block in row]+ #Colormap
					['|']+
					[str(block.linkhash) if isinstance(block, Block) else ' ' for block in row])+'\n' #Linkmap
				for row in self]
			)
		)

	def __getitem__ (self, key):
		# Allows the object to be directly sliced and indexed, instead of having to reference the cells.
		return self.cells.__getitem__(key)

	def __iter__ (self):
		# Allows the object to be directly used in a for loop, instead of having to reference the cells.
		return iter(self.cells)

	def __len__ (self):
		# Returns the grid height.
		return len(self.cells)

	def __contains__ (self, block):
		# Allows in container tests for blocks.
		return block in itertools.chain.from_iterable(self)

	def set_cells (self):
		# Reset Matrix.
		self.cells = [[None if j<=21 else Block([i, j], 7, fallen=True) for i in range(10)] for j in range(23)]

	def add_garbage (self):
		# Adds a garbage row.
		hole = random.randrange(10)
		garbage = [None if i == hole else Block([i, 22], 7, fallen=True) for i in range(10)]
		# Link up the garbage blocks.
		for i, block in enumerate(garbage):
			# For some reason this will not work with a list comprehension.
			links = [ ]
			if i != hole:
				if i != 0 and i != hole + 1:
					links.append(3)
				if i != 9 and i != hole - 1:
					links.append(1)
				block.links = links
				block.update()
		self.cells.insert(22, garbage)
		self.cells.pop(0)
		self.update()

	def paste_shape (self, shape, fallen=False):
		# Paste a shape to this grid.
		for pos, block in zip(shape.poslist, shape):
			assert self[pos[1]][pos[0]] is None, "Collision check failure! You done fucked up, bro!"
			self[pos[1]][pos[0]] = block
			block.relpos = pos
			if fallen: block.fallen = True

	def flood_fill (self, t_shape, index):
		# Recursive blind flood fill function.
		if self[index[0]][index[1]] is not None and index[0] < 22:
			# Reset the block's position.
			self[index[0]][index[1]].relpos = [index[1] - 4, index[0] - 1]
			# Add the block to the temporary shape.
			t_shape.add(self[index[0]][index[1]])
			# Remove the block from the grid.
			self[index[0]][index[1]] = None
			# Look at every nearby cell and perform again if valid.
			self.flood_fill(t_shape, (index[0] - 1, index[1])) # Up
			self.flood_fill(t_shape, (index[0], index[1] + 1)) # Right
			self.flood_fill(t_shape, (index[0] + 1, index[1])) # Down
			self.flood_fill(t_shape, (index[0], index[1] - 1)) # Left

	def link_fill (self, t_shape, index):
		# Recursive flood fill function using the links.
		if self[index[0]][index[1]] is not None and index[0] < 22:
			# Reset the block's position.
			self[index[0]][index[1]].relpos = [index[1] - 4, index[0] - 1]
			# Add the block to the temporary shape.
			t_shape.add(self[index[0]][index[1]])
			# Save links to temporary variable before deleting the block.
			oldlinks = self[index[0]][index[1]].links
			# Remove the block from the grid.
			self[index[0]][index[1]] = None
			# Check adjacent blocks as indicated by the links.
			if 0 in oldlinks: # Up
				self.link_fill(t_shape, (index[0] - 1, index[1]))
			if 1 in oldlinks: # Right
				self.link_fill(t_shape, (index[0], index[1] + 1))
			if 2 in oldlinks: # Down
				self.link_fill(t_shape, (index[0] + 1, index[1]))
			if 3 in oldlinks: # Left
				self.link_fill(t_shape, (index[0], index[1] - 1))

	def cascade (self, base_row):
		# Drop floating blocks during non-naive line clear methods.
		# Set the fallen flag of all blocks above and one row below to False, which means they're 'floating'.
		for row in self[:base_row + 2]:
			for block in row:
				if block is not None and block.color != 7:
					block.fallen = False
		# For each row, cut a set of temporary shapes from it to allow to fall.
		locked_shapes = True
		while locked_shapes:
			for row in self[::-1]:
				# locked_shapes is True when one of the shapes in the row is blocked from falling by another shape.
				locked_shapes = False
				tempshapes = [ ]
				for block in row:
					# Add blocks that both exist and have not fallen yet.
					if block is None or block.fallen:
						continue
					# Create new blank temporary shape.
					tempshape = Shape()
					# Cut connected blocks from grid to the shape.
					if self.user.cleartype == 1:
						# Perform a blind flood fill if the method is sticky.
						self.flood_fill(tempshape, block.relpos[::-1])
					elif self.user.cleartype == 2:
						# Perform a flood fill considering which blocks are linked if the method is cascade.
						self.link_fill(tempshape, block.relpos[::-1])
					for block in tempshape:
						# If the block being tested isn't connected to the block below it, then the shape it's a part of is blocked.
						if (self.user.cleartype == 1
							or (2 not in block.links and self[block.relpos[1] + 2][block.relpos[0] + 4] is not None)):
							locked_shapes = True
							# Cut the shape back to the matrix.
							self.paste_shape(tempshape)
							break
					# Add this temprary shape to the list if it's not blocked.
					else: tempshapes.append(tempshape)
				# If there is at least one temporary shape created, move them all down one space.
				if not len(tempshapes):
					continue
				for shape in tempshapes:
					shape.translate(( 0, 1))	
					# Test if moving down one space causes a collision.
					for pos, block in zip(shape.poslist, shape):
						if self[pos[1]][pos[0]] is None:
							continue
						# Move it back up one space when a collision has been detected.
						shape.translate(( 0,-1))
						# If it collided with a fallen block, then the shape has fallen. Copy it back to the matrix.
						if self[pos[1]][pos[0]].fallen:
							self.paste_shape(shape, True)
							break
					else:
						# If it either did not collide, or collided with another floating shape, copy it to the tempgrid instead.
						self.tempgrid.append(shape)			

	def clear_lines (self):
		"""
		Clears lines when the free tetrimino is pasted to the grid.
		self.user.cleartype describes the 3 different styles of clearing available:
		0 refers to naive clearing, where floating blocks are left alone. Used in old Tetris games.
		1 refers to sticky clearing, where the floating blocks are grouped by those that share sides.
		2 refers to cascade clearing, where the original block groupings are preserved.

		In both sticky and cascade clearing, the groups will fall as if they were still active, then
		the lines will be re-evaluated to see if another clear happened.
		"""
		# Track how many lines are cleared per chain.
		self.user.line_list = [0]
		cleared = True
		while cleared:
			# Set the cleared flag to False to represent no lines being cleared yet.
			cleared = False	
			# Set base_row, which is the lowest row a line clear occurs in, to the top row.
			base_row = 0
			# Test if there is a full row, checking upwards, then clear all of them.
			for i in range(21, -1, -1):
				# If at least one block is empty, the row is not full.
				if all(self[i]):
					# Add a ClearSprite to show the row being cleared.
					self.csprts.append(ClearSprite(bottomleft=(self.rect.centerx - 125, self.rect.bottom - 20 + 25*(i-21))))
					# If at least one block is grey between the first two visible blocks, it must be a garbage row.
					garbagerow = env.cond_any(self[i][:2], lambda b:b.color == 7)
					# Increment the cleared lines counter.
					self.user.line_list[-1] += 1
					cleared = True
					# If a line is full below the indicated base_row, set base_row to that row.
					if i > base_row: base_row = i
					# Remove the links that point to blocks that are to be cleared.
					for j in range(10):
						# Remove upward links from blocks below.
						if self[i + 1][j] is not None and 2 in self[i][j].links:
							self[i + 1][j].links.remove(0)
							self[i + 1][j].update()
						# Remove downward links from blocks above.
						if self[i - 1][j] is not None and 0 in self[i][j].links:
							self[i - 1][j].links.remove(2)
							self[i - 1][j].update()
						# Just leave the row empty if the method is sticky or cascade.
						if self.user.cleartype > 0: self[i][j] = None
					# Splice the old row out and create a new blank row on top if method is naive or when clearing a garbage row.
					if self.user.cleartype < 1 or garbagerow:
						# Delete the old row.
						self.cells.pop(i)
						# Create new blank row at the top.
						self.cells.insert(0, [None for i in range(10)])
						# Force the game to look through all the rows again to avoid skipping lines.
						break
			# If a line has been cleared, let the floating blocks fall until they collide with other blocks.
			if not cleared:
				continue
			yield True
			# If the method is naive, then don't proceed.
			if self.user.cleartype < 1:
				continue
			while True:
				# Tempgrid is the list of all shapes that have not fallen all the way down yet.
				self.tempgrid = [ ]
				# Drop all floating pieces via the tempgrid.
				self.cascade(base_row)
				# Copy all of the tempgrid's shapes back to the matrix.
				if len(self.tempgrid) > 0:
					for shape in self.tempgrid:
						self.paste_shape(shape)
					# Display intermediate drops so the user can see the combo, via doing this as a coroutine.
					yield True
				else: break
			self.user.line_list.append(0)
			# If the tempgrid list is empty, that means that all the blocks have fallen. Check if the fallen blocks caused another line clear.
		# Evaluate score from the last clear.
		# If at least one block remains on the bottom, then the grid has not completely cleared.
		self.user.eval_clear_score(not any(self[21]))
		# Once done clearing lines, set the clearing flag to False to prevent the StopIteration exception from being raised.
		yield False

	def animate_clears (self):
		# Animate and draw the ClearSprite objects.
		for sprite in self.csprts:
			sprite.draw()
			try:
				sprite.animate()
			except StopIteration:
				self.csprts = [ ]
				break

	def update (self):
		# Display the grid background and constituent blocks.
		self.draw()
		for i, row in enumerate(self[2:22], 2):
			for j, block in enumerate(row):
				if block is None:
					continue
				block.set(bottomleft=(self.rect.centerx + block.rect.w*(j-5), self.rect.bottom - 20 + block.rect.h*(i-21)))
				block.draw()
