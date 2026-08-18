// Wall kicks: SRS, with Arika's symmetric I and the SRS+ 180 tables.
//
// A port of Core.wall_kick and Core.test_kicks. The tables are the engine's own,
// already converted to its downward y axis, so a negative y offset lifts the
// piece. Getting one entry wrong is invisible until a piece lands somewhere it
// should not have, which is why the whole table is compared against Python.
#pragma once

#include "forcetris/board.hpp"

namespace forcetris {

// What became of a rotation.
struct Rotation {
	Piece piece;            // Where the piece ended up. Unchanged if refused.
	bool turned = false;    // False if the rotation was refused outright.
	bool kicked = false;    // True if it only fitted after being nudged.
	bool floor_kick = true; // The floor kick allowance, passed back out.
};

// Turn a piece and settle where it lands.
//
// `turns` is clockwise: 1 for CW, 3 for CCW, 2 for a 180. `floor_kick` is the
// per-piece allowance - a piece may be lifted by a kick once, after which the
// candidates that would raise it are skipped.
//
// With `kicks` off a rotation that collides is refused rather than forced
// through, which is what the Python side does since the piece would otherwise
// be left standing inside the stack.
//
// An O is never turned at all: it has one orientation, so a rotation of it is
// discarded rather than recorded as a state change.
Rotation rotate (const Board& board, const Piece& piece, int turns,
                 bool kicks = true, bool floor_kick = true);

} // namespace forcetris
