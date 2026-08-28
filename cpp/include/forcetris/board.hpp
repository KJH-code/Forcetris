// The matrix, and the two questions asked of it: does a piece fit, and what
// happens when one lands.
//
// A port of the parts of engine/shapes.py Grid that the game logic depends on.
// Cells hold the form of the piece that filled them, so a board reads back the
// same colours the Python side records into a replay.
#pragma once

#include <array>
#include <string>
#include <vector>

#include "forcetris/piece.hpp"

namespace forcetris {

// A piece in play: which one, how it is turned, and where its centre sits.
struct Piece {
	int form = I;
	int state = 0;
	int x = kSpawnX;
	int y = kSpawnY;

	friend bool operator== (const Piece& a, const Piece& b) {
		return a.form == b.form && a.state == b.state && a.x == b.x && a.y == b.y;
	}
	friend bool operator!= (const Piece& a, const Piece& b) { return !(a == b); }
};

// Where a piece's four cells actually are on the board.
std::array<Offset, kCells> cells_of (const Piece& piece);

class Board {
public:
	// Empty, with the floor already in place.
	Board ();

	// Read a board out of the row strings a replay stores: a dot for an empty
	// cell, a digit for the form that filled it. Short input is padded at the
	// top, which is how replay.padded fills a trimmed snapshot back out.
	static Board from_rows (const std::vector<std::string>& rows);

	void clear ();

	// -1 for an empty cell, otherwise the form that fills it. Anything outside
	// the walls or below the floor reads as filled, which is what makes the
	// spin rules treat a wall as a block. Inline: the bot's search and its
	// evaluation read cells by the million, and the call was the cost.
	int at (int x, int y) const {
		if (x < 0 || x >= kWidth || y >= kHeight) {
			// The walls and the floor. Solid, so a piece wedged against one
			// counts as wedged in for the spin rules.
			return GARBAGE;
		}
		if (y < 0) {
			// Above the matrix, where a spawning piece legitimately sits.
			return -1;
		}
		// A sealed column is wall all the way down the visible field.
		if (sealed_ >> x & 1) {
			return GARBAGE;
		}
		return cells_[y][x];
	}
	void set (int x, int y, int form);

	// True if any of the piece's cells is out of bounds or on top of a block.
	// Cells above the top of the matrix are ignored, as they are in Python:
	// pieces spawn partly above the visible field.
	bool collides (const Piece& piece) const {
		for (const Offset cell : cells_of(piece)) {
			if (cell.y < 0) {
				continue;
			}
			if (cell.x < 0 || cell.x >= kWidth || cell.y >= kHeight) {
				return true;
			}
			if (sealed_ >> cell.x & 1) {
				return true;
			}
			if (cells_[cell.y][cell.x] >= 0) {
				return true;
			}
		}
		return false;
	}

	// Sealed Columns: bit x of the mask walls column x off for the whole
	// visible field - pieces cannot enter it, the spin rules read it as
	// wall, and a row completes without it. The mask is terrain, not cells:
	// clears and cascades never touch it, and clear() leaves it standing.
	// Zero - the default everywhere outside the stage that sets it - is
	// bit-for-bit the board as it always was.
	void set_sealed (int mask) { sealed_ = mask; }
	int sealed () const { return sealed_; }

	// Cold Iron: a row that froze solid instead of clearing. Marked by
	// freeze_full_rows at the end of a clearing pass, cleared - with the
	// line credit it was denied at the freeze - by the first later pass
	// that runs with iron_only. The flag rides with its row through every
	// splice and shift.
	bool iron_row (int y) const {
		return y >= 0 && y < kHeight && iron_[y];
	}
	// Mark every full row that is not yet iron. Returns how many froze.
	int freeze_full_rows ();

	// The piece dropped straight down as far as it will go.
	Piece dropped (const Piece& piece) const;

	// Paste a piece in. Returns false if it would have overlapped, which the
	// caller should treat as a bug rather than a game event.
	bool paste (const Piece& piece);

	// Take out every full row and drop what was above it, the way the guideline
	// clears. Returns how many rows went.
	int clear_lines ();

	// A garbage row pushed in under everything, as arcade mode does it: every
	// row shifts up one - the top row falls off the world - and the new bottom
	// row is garbage with a single hole, its blocks chained left and right the
	// way add_garbage chains them.
	void push_garbage (int hole);
	// The same push with any set of holes: bit x of `mask` empties column x.
	// The cheese knobs deal these; arcade keeps its single hole.
	void push_garbage_mask (int mask);
	// Rows still carrying garbage cells, for the cheese modes' bookkeeping:
	// how much of the stack is cheese, and whether the race has dug it all.
	int garbage_rows () const;
	// Overdrive's backdraft: splice the bottom row out if it holds garbage.
	// True when a row actually burned.
	bool burn_bottom_garbage ();

	// The cascade bookkeeping riding along with every cell: the intra-piece
	// links (bit 0 up, 1 right, 2 down, 3 left) and whether the cell has
	// settled. Cells hand-placed through set() and rows read through
	// from_rows() count as settled rubble; cells pasted as a locking piece do
	// not, until a cascade lands them.
	unsigned char links_at (int x, int y) const;
	bool fallen_at (int x, int y) const;
	void set_cell (int x, int y, int form, unsigned char links, bool fallen);

	// One pass of the line clearer's row scan, per clear style (0 naive,
	// 1 sticky, 2 linked): naive takes the lowest full row alone and splices
	// it out; the cascade styles blank every full row bottom-up in one pass
	// but stop at - and splice out - a garbage row, which ends the pass. The
	// link surgery on the neighbours happens here too. Returns the rows taken;
	// `base_row` is the lowest row cleared and `downstacked` counts the
	// garbage rows dug out. Under Cold Iron the pass runs with `iron_only`:
	// only rows already frozen clear, and the fresh ones are left for
	// freeze_full_rows to mark once the pass is done.
	int clear_pass (int cleartype, int& base_row, int& downstacked,
		bool iron_only = false);

	// One settle step of a cascade: every floating group - side-connected
	// under sticky, link-connected under linked - falls one row, or lands on
	// something settled, or waits behind another floating group. Returns true
	// if anything moved; the clearer yields a frame for it and calls again.
	bool cascade_step (int cleartype, int base_row);

	// True if nothing is left standing on the field.
	bool empty () const;

	// The board as replay row strings, trimmed of the empty rows on top.
	std::vector<std::string> rows () const;

private:
	// A block lifted out of the grid while its group decides where to fall.
	struct Loose {
		Offset at;
		int form = 0;
		unsigned char links = 0;
	};
	void fill_group (int cleartype, int x, int y, std::vector<Loose>& group);
	// Full under the sealed mask: every unsealed cell is occupied.
	bool row_full (int y) const;

	std::array<std::array<int, kWidth>, kHeight> cells_{};
	std::array<std::array<unsigned char, kWidth>, kHeight> links_{};
	std::array<std::array<bool, kWidth>, kHeight> fallen_{};
	int sealed_ = 0;
	std::array<bool, kHeight> iron_{};
};

} // namespace forcetris
