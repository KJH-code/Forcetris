// Tetriminoes: their cells, how they turn, and where they start.
//
// This is a port of engine/shapes.py, not a fresh implementation. The cell
// offsets and the rotation maths are the ones the Python engine uses, down to
// the half-cell centre the I piece rotates about, because the two have to agree
// exactly - tools/check_cpp_core.py compares them placement by placement.
#pragma once

#include <array>
#include <cstdint>

namespace forcetris {

// Pieces in the order the engine numbers them. Seven playable, then garbage.
enum Form : int { I = 0, O = 1, T = 2, S = 3, Z = 4, J = 5, L = 6, GARBAGE = 7 };

inline constexpr int kForms = 7;
inline constexpr int kStates = 4;
inline constexpr int kCells = 4;

// The playfield, and where a piece appears on it. Matches Grid.set_cells and
// Shape's default position.
inline constexpr int kWidth = 10;
inline constexpr int kHeight = 22;   // Playable rows; the floor sits below them.
inline constexpr int kSpawnX = 4;
inline constexpr int kSpawnY = 1;

struct Offset {
	int x = 0;
	int y = 0;

	friend constexpr bool operator== (Offset a, Offset b) { return a.x == b.x && a.y == b.y; }
	friend constexpr bool operator!= (Offset a, Offset b) { return !(a == b); }
	// Sorted the way Python's sorted() orders (x, y) tuples, so the two agree on
	// what "the same four cells" looks like when they are compared as sequences.
	friend constexpr bool operator< (Offset a, Offset b) {
		return a.x != b.x ? a.x < b.x : a.y < b.y;
	}
};

using Cells = std::array<Offset, kCells>;

// Where a piece's four cells sit relative to its centre of rotation, in each
// orientation. Sorted. Rotation state 0 is spawn, 1 is one turn clockwise.
const Cells& offsets (int form, int state);

// Which state a rotation lands in. Turns are counted clockwise: 1 for CW, 3 for
// CCW, 2 for a 180.
constexpr int turned (int state, int turns) { return (state + turns) % kStates; }

} // namespace forcetris
