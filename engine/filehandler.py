"Contains class definitions for custom context managers used in the game."
try:
	import os
	import sys
	import struct
	import itertools
	from traceback import print_exception
except ImportError:
	print("A module must've shat itself:")
	raise

# Same anchor as engine.environment.ROOT, recomputed here rather than imported so
# that reading save data doesn't drag in pygame and a display surface.
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

class ContextMan:
	"""
	Generic file manager.
	Note that the return value of __enter__ is the object, and not the file.
	"""
	__slots__ = ('sfile', 'bckup', 'eflag')

	def __init__ (self, name):
		# Anchored to the game folder rather than the working directory, and created
		# up front, so that saving a score doesn't depend on where the game was started.
		fname = os.path.join(ROOT, 'data', name)
		bname = os.path.join(ROOT, 'data', 'back', name[:-3] + 'bak')
		os.makedirs(os.path.dirname(bname), exist_ok=True)
		# The backup file is there to ensure that the data is preserved in some cases of fucketry.
		# Note: Does not work if the backup file itself is fucked with.
		try:
			self.bckup = open(bname, 'rb+')
			# Since the backup exists, check if the data exists too.
			try:
				self.sfile = open(fname, 'rb+')
			except IOError:
				# If the scorefile data is missing, but the backup still exists, use the backup to restore it.
				self.sfile = open(fname, 'wb+')
				self.load()
		except IOError:
			self.bckup = open(bname, 'wb+')
			# If the backup is missing, check if the original data exists.
			try:
				self.sfile = open(fname, 'rb+')
				self.backup()
			except IOError:
				self.sfile = open(fname, 'wb+')
				self.reset()
		self.bckup.seek(0)
		self.sfile.seek(0)
		# If this flag is False, then the backup file is assumed to be valid when a reading exception is thrown.
		self.eflag = False

	def __str__ (self):
		# Debug info.
		return "<Context manager with id "+str(id(self))+">"

	def __repr__ (self):
		# eval() usable expression.
		return "ContextMan('file.dat')"

	def __enter__ (self):
		# The value that will be returned to the as statement.
		return self

	def __exit__ (self, *exc_info):
		# Cleanup both files.
		self.bckup.close()
		self.sfile.close()

	def backup (self):
		# Update the backup.
		self.sfile.seek(0)
		self.bckup.seek(0)
		self.bckup.truncate()
		self.bckup.write(self.sfile.read())

	def load (self):
		# Refresh the file using the backup.
		self.sfile.seek(0)
		self.bckup.seek(0)
		self.sfile.truncate()
		self.sfile.write(self.bckup.read())

	def reset (self):
		pass

class SFH (ContextMan):
	"""
	SFH, or ScoreFileHandler is a class that is built to, as the name suggests,
	handle score file data.
	"""
	def __init__ (self):
		# Scorefile of the filename hiscore.dat
		super().__init__('hiscore.dat')

	def __str__ (self):
		# Debug info.
		return "<Score file context manager with id "+str(id(self))+">"

	def __repr__ (self):
		# eval() usable expression.
		return "SFH()"

	def reset (self):
		# Resets the given scorefile to the default data.
		self.sfile.seek(0)
		self.sfile.truncate()
		for _ in range(30):
			# Alexey Leonidovich Pajitnov is the developer of the original Tetris.
			score = [c.encode() for c in 'Pajitnov'] + [0, 0, 0]
			self.sfile.write(struct.pack('>ccccccccQLL', *score))
		self.backup()

	def validate (self):
		# Load from the backup file or reset both files based on the error state.
		if not self.eflag:
			# The backup hasn't been verified to be invalid yet, load from backup.
			self.load()
			self.eflag = True
		else:
			# The backup is also invalid, reset both files.
			self.reset()
			self.eflag = False

	def decode (self, splitname=False):
		# Read the scorefile, and return 3 lists of top ten score lists.
		self.sfile.seek(0, 2)
		if self.sfile.tell() != 720:
			# If the number of bytes are wrong, then the score file is probably wrong.
			# Load from the backup, but if that fails, remake the score file.
			self.validate()
			print('Scorefile length is invalid!')
			return self.decode()
		self.sfile.seek(0)
		scorelists = [ ]
		for _ in range(30):
			try:
				# Each score is a set of 24 bytes, the first eight of which stand for the name entered.
				rawname = struct.unpack('>cccccccc', self.sfile.read(8))
				if splitname:
					# The splitname argument is True when the data needs to be read raw, rather than formatted for easy display.
					score = [*rawname, *struct.unpack('>QLL', self.sfile.read(16))]
				else:
					name = ''.join(c.decode() for c in rawname)
					score = [name, *struct.unpack('>QLL', self.sfile.read(16))]
				scorelists.append(score)
			except (struct.error, TypeError):
				# If an exception is thrown during reading, first attempt to load from the backup.
				# If that still fails, reset the scores (erasing score data, whoops).
				self.validate()
				# Echo exception details to the console.
				print_exception(*sys.exc_info(), file=sys.stdout)
				return self.decode()
		return [scorelists[:10], scorelists[10:20], scorelists[20:]]

	def encode (self, gtype, entry):
		# Add a new score to the high scores.
		g = 0 if gtype == 'arcade' else 1 if gtype == 'timed' else 2
		# Grab the score data.
		slists = self.decode(True)
		# Add the new entry.
		slists[g].append(entry)
		# Sort the entries: Lines cleared third, time second, and score first.
		slists[g].sort(key=lambda s: (-s[-3], s[-1], s[-2]))
		# Remove the old last entry and re-arrange the score lists into a single list.
		slists[g].pop()
		slists = itertools.chain.from_iterable(slists)
		# Apply change to scorefile.
		self.sfile.seek(0)
		self.sfile.truncate()
		for score in slists:
			self.sfile.write(struct.pack('>ccccccccQLL', *score))
		# Backup the entered score.
		self.backup()

class Config (ContextMan):
	""
	def __init__ (self):
		# Config file is of the name config.dat.
		super().__init__('config.dat')

	def __str__ (self):
		# Debug info.
		return "<Config file context manager with id "+str(id(self))+">"

	def __repr__ (self):
		# eval() usable expression.
		return "Config()"

	def reset (self):
		raise NotImplementedError
