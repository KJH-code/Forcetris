#include "forcetris/spins.hpp"

namespace forcetris {
namespace spins {

Corners corners (const Board& board, const Piece& piece) {
	const auto filled = [&board] (int x, int y) { return board.at(x, y) >= 0; };
	const bool nw = filled(piece.x - 1, piece.y - 1);
	const bool ne = filled(piece.x + 1, piece.y - 1);
	const bool sw = filled(piece.x - 1, piece.y + 1);
	const bool se = filled(piece.x + 1, piece.y + 1);
	Corners found;
	found.count = int(nw) + int(ne) + int(sw) + int(se);
	// The two corners the T faces, by orientation: up, right, down, left.
	switch (piece.state % kStates) {
		case 0: found.fronts = nw && ne; break;
		case 1: found.fronts = ne && se; break;
		case 2: found.fronts = sw && se; break;
		default: found.fronts = nw && sw; break;
	}
	return found;
}

bool immobile (const Board& board, const Piece& piece) {
	for (const Offset nudge : {Offset{0, -1}, Offset{-1, 0}, Offset{1, 0}}) {
		Piece probe = piece;
		probe.x += nudge.x;
		probe.y += nudge.y;
		if (!board.collides(probe)) {
			return false;
		}
	}
	return true;
}

std::optional<Verdict> judge (const Board& board, const Piece& piece,
                              Rule rule, bool rotated_last, bool kicked) {
	if (rule == OFF || !rotated_last) {
		return std::nullopt;
	}
	if (piece.form == T) {
		// T pieces go by the corner rule under every setting that detects
		// anything, since that is the definition all guideline games agree on.
		const Corners found = corners(board, piece);
		if (found.count < 3) {
			return std::nullopt;
		}
		return Verdict{piece.form, found.fronts || rule == ALL};
	}
	if (rule == TSPIN || !immobile(board, piece)) {
		return std::nullopt;
	}
	// Plain all-spin scores every wedge as a full spin. With minis on, a
	// rotation that fitted without needing a kick is the lesser one.
	return Verdict{piece.form, rule == ALL || kicked};
}

} // namespace spins
} // namespace forcetris
