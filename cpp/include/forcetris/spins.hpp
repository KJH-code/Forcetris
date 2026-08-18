// Spin detection: whether the placement just made counts as a spin, and how
// generously that is judged.
//
// A port of Core.eval_corners, Core.is_immobile and Core.eval_spin, graded
// against them over the same rubble boards the rotation sweep uses.
#pragma once

#include <optional>

#include "forcetris/board.hpp"

namespace forcetris {
namespace spins {

// The rules, in the order the settings menu lists them.
enum Rule : int { OFF = 0, TSPIN = 1, ALL = 2, ALL_MINI = 3 };

struct Verdict {
	int form = 0;
	bool full = false;   // A full spin rather than a mini.
};

// The three corner rule for a T: how many diagonals around its centre are
// filled, and whether both of the two it faces are among them. Cells outside
// the matrix count as filled - a wall wedges a piece as well as a block does.
struct Corners {
	int count = 0;
	bool fronts = false;
};
Corners corners (const Board& board, const Piece& piece);

// The test every all-spin rule turns on: a piece that cannot move up, left or
// right is wedged into the stack rather than resting on top of it.
bool immobile (const Board& board, const Piece& piece);

// The verdict on a piece where it locked. `rotated_last` is whether the last
// thing done to it was a rotation; `kicked` is whether that rotation needed a
// kick, which is what separates full from mini for non-T pieces under the
// all-spin-plus-mini rule.
std::optional<Verdict> judge (const Board& board, const Piece& piece,
                              Rule rule, bool rotated_last, bool kicked);

} // namespace spins
} // namespace forcetris
