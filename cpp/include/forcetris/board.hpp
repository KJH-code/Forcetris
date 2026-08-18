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
	// spin rules treat a wall as a block.
	int at (int x, int y) const;
	void set (int x, int y, int form);

	// True if any of the piece's cells is out of bounds or on top of a block.
	// Cells above the top of the matrix are ignored, as they are in Python:
	// pieces spawn partly above the visible field.
	bool collides (const Piece& piece) const;

	// The piece dropped straight down as far as it will go.
	Piece dropped (const Piece& piece) const;

	// Paste a piece in. Returns false if it would have overlapped, which the
	// caller should treat as a bug rather than a game event.
	bool paste (const Piece& piece);

	// Take out every full row and drop what was above it, the way the guideline
	// clears. Returns how many rows went.
	int clear_lines ();

	// True if nothing is left standing on the field.
	bool empty () const;

	// The board as replay row strings, trimmed of the empty rows on top.
	std::vector<std::string> rows () const;

private:
	std::array<std::array<int, kWidth>, kHeight> cells_{};
};

} // namespace forcetris
