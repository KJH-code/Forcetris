## pyTetris

#### PyGame implementation of a Tetris clone.

#### Original Version Features:
- Working keyboard-operated menus.
- Basic grid implemented that uses invisible block padding to emulate wall collision, and a simply (although buggy) line clear system in either naive or sticky methods.
- Collision detection between blocks in the grid, and the blocks in the active tetrimino.
- Rotation and tetrimino generation work according to the SRS guideline.
- The active tetrimino can be held and swapped.
- The active tetrimino has a ghost to aid player move prediction.
- Both soft drop and hard drop can be used to make the active tetrimino fall faster.
- The game can be paused (though not during line clearing).
- The game data can be reset and replayed.

#### Repository updates start here:

InDev 0.2.3: 
- Implemented rudimentary scoring system. 
- Fixed bug in cascade clearing where some block domains did not fall when they should.
- Fixed bug in line clearing that caused block domains to fall "through" each other when they should not.

InDev 0.2.4: 
- Fixed bug where rotating a piece allowed players to suspend it indefinitely.
- Fixed bug where I piece still caused crashes when wall-kicked from the right side due to block iteration order by padding the right side by one more.
- Made game speed consistent per frame, and reduced the number of unnecessary calls made.
- Fixed scoring system bugs.

InDev 0.2.5:
- The player can now lose properly when the block stack fills up all the way.
- Rebalanced the score values to more closely resemble Tetris standards.
- Implemented Delayed Auto-Shifting.
- Streamlined the running code, making it more readable and efficient.

InDev 0.2.6:
- Implemented tetrimino spawn delay.
- Allowed DAS to charge during spawn delay.
- Fixed bug that caused ghost to lag behind the active tetrimino by one frame.

InDev 0.2.7:
- Allowed swapping held piece during spawn delay.
- Fixed bug where the tetrimino did not evaluate properly after spawn delay.
- Soft locking time increased to normal locking time.
- Rebalanced tetrises to be more point-worthy and chains less point-worthy.

InDev 0.2.8:
- Fixed bug where pressing hard dropping during spawn delay caused a premature loss.
- Fixed bug where soft drop lock delay did not work as intended.
- Implemented rudimentary Timed mode.
- Fixed a bug where DAS would not charge if the key was held before the old piece was dropped.

InDev 0.2.9:
- Fixed display error during line clear due to outdated code.
- Optimized code for readability (again), especially with regards to score tracking.
- Timed mode is now faster than Free mode in pacing.
- Added up key as quick clockwise rotate shortcut.

# pyTetris Alpha

Alpha 1.0.0:
- Implemented Arcade mode. Levels and rising difficulty rely on the number of lines cleared by the player.
- Implemented Timed mode. Fixed a bug where DAS would cause a piece to be suspended while in contact with another block in this mode.
- The older mode has been renamed Free mode and can be played for casual Tetris.
- Removed redundant display code at shapes.py and replaced it with an optimized display function.
- Updated grid texture.
- Fixed bug where pausing would cause a falling block to not lock.
- Fixed bug where garbage lines would be subject to cascading gravity.

Alpha 1.1.0:
- Rebalanced score values and score calculation to avoid line clear score errors.
- Allowed no-kick T-spins to be rewarded with a bonus score multiplier.
- Rescaled levels in arcade mode to reduce the overall number of lines required to get to maximum level.
- Garbage lines now spawn periodically at level 128, and will spawn faster at level 256. (Previously, they only spawned at level 256)
- Wall kick and Arcade difficulty tables have been reworked for readability.
- Arcade difficulty now scales linearly with level up to its maximum.
- Enabled Left Control to rotate counter-clockwise.
- Optimized PositionedSurface and Grid code, laying groundwork for performance improvements.
- Game now automatically pauses when the window loses focus.
- Added a Tetris theme remix to be played in-game.
- Added a placeholder playing field texture to complement the rudimentary HUD design.

Alpha 1.1.2:
- Moved to python 3.x syntax.
- Optimized base class code, creating FreeSprite from PositionedSurface (inheriting from pygame.sprite.Sprite but with the same functionality), making it easier to add new features and implement animation via the AnimatedSprite class.
- Improved line-clear performance and made Grid class code more readable.
- Fixed a bug where the textures would mess up during line clearing.
- Fixed arcade mode level bugs.

Alpha 1.1.4:
- Fixed a bug where the line clearing would cause an infinite loop on sticky but not cascade clearing.
- Enabled pausing during line clear, turning Grid.clear_lines() into a generator, and optimizing its code in relation to the User object.
- Replaced obsolete syntax.
- Updated menu logic.

Alpha 1.1.5:
- Moved main program function into main game object in an attempt to implement the game in an object-oriented manner.
- Updated the menu-switch system such that it now uses eval() to simplify the running loop code tremendously.
- Fixed menu methods and attribute names for both efficiency and logical sense.
- Made render_text its own function, and changed all color references into hexstrings.
- Fixed ghost textures for increased visibility.

Alpha 1.1.6:
- Improved handling of scorefile reading/writing, fixed all (explored) bugs that caused crashes as a result of reading missing and invalid score files. This makes use of a context manager object.
- Fully updated syntax to Python 3.x as of the fixed file byte handling.
- Allowed numbers to be entered into score names.

Alpha 1.1.7:
- More OOP-style refactoring.
- Added a ClearSprite that performs the line clearing animation.
- Fixed the gravity timers being longer by one extra frame.
- Allowed freeshape to update after gravity ticks, fixing the longstanding DAS-related "sticky" bug.

Alpha 1.1.8:
- Increased score payout for twists and combos, and fixed an error in the math for Timed Mode's score multiplier.
- Fixed an error resulting from defining a rich comparison but no hash for Shape objects.
- Updated context managers to work more generally, and started on user-defined game settings/configuration.
- Updated repository layout to be easier to navigate.
- Added a -d option so that debug mode can be manually activated.

Alpha 1.1.9:
- Partially mplemented a logging feature.
- Made optimizations to some slow and difficult to read parts in the file handling code.

Alpha 1.2.0: (to be worked on)
- Add new textures for score, main, and game menus
- Add help menu and settings menu
