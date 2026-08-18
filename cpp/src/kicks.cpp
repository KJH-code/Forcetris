#include "forcetris/kicks.hpp"

#include <span>
#include <vector>

namespace forcetris {
namespace {

// The candidates tried, in order, for one state change. Taken verbatim from
// Core.wall_kick: the first four rows of each are SRS with Arika's I, and the
// five-entry ones are the SRS+ 180 tables.
struct Candidates {
	std::vector<Offset> normal;   // J, L, S, T, Z
	std::vector<Offset> line;     // I
};

const Candidates* table (int from, int to) {
	// Spawn out.
	static const Candidates k0_3{{{ 1, 0}, { 1,-1}, { 0, 2}, { 1, 2}},
	                             {{ 2, 0}, {-1, 0}, {-1,-2}, { 2, 1}}};
	static const Candidates k0_1{{{-1, 0}, {-1,-1}, { 0, 2}, {-1, 2}},
	                             {{-2, 0}, { 1, 0}, { 1,-2}, {-2, 1}}};
	static const Candidates k0_2{{{ 0,-1}, { 1,-1}, {-1,-1}, { 1, 0}, {-1, 0}},
	                             {{-1, 0}, {-2, 0}, { 1, 0}, { 2, 0}, { 0, 1}}};
	// Clockwise out.
	static const Candidates k1_0{{{ 1, 0}, { 1, 1}, { 0,-2}, { 1,-2}},
	                             {{ 2, 0}, {-1, 0}, { 2,-1}, {-1, 2}}};
	static const Candidates k1_2{{{ 1, 0}, { 1, 1}, { 0,-2}, { 1,-2}},
	                             {{-1, 0}, { 2, 0}, {-1,-2}, { 2, 1}}};
	static const Candidates k1_3{{{ 1, 0}, { 1,-2}, { 1,-1}, { 0,-2}, { 0,-1}},
	                             {{ 0, 1}, { 0, 2}, { 0,-1}, { 0,-2}, {-1, 0}}};
	// Upside down out.
	static const Candidates k2_1{{{-1, 0}, {-1,-1}, { 0, 2}, {-1, 2}},
	                             {{-2, 0}, { 1, 0}, {-2,-1}, { 1, 1}}};
	static const Candidates k2_3{{{ 1, 0}, { 1,-1}, { 0, 2}, { 1, 2}},
	                             {{ 2, 0}, {-1, 0}, { 2,-1}, {-1, 1}}};
	static const Candidates k2_0{{{ 0, 1}, {-1, 1}, { 1, 1}, {-1, 0}, { 1, 0}},
	                             {{ 1, 0}, { 2, 0}, {-1, 0}, {-2, 0}, { 0,-1}}};
	// Counter-clockwise out.
	static const Candidates k3_2{{{-1, 0}, {-1, 1}, { 0,-2}, {-1,-2}},
	                             {{ 1, 0}, {-2, 0}, { 1,-2}, {-2, 1}}};
	static const Candidates k3_0{{{-1, 0}, {-1, 1}, { 0,-2}, {-1,-2}},
	                             {{-2, 0}, { 1, 0}, {-2,-1}, { 1, 2}}};
	static const Candidates k3_1{{{-1, 0}, {-1,-2}, {-1,-1}, { 0,-2}, { 0,-1}},
	                             {{ 0, 1}, { 0, 2}, { 0,-1}, { 0,-2}, { 1, 0}}};

	switch (from) {
		case 0:
			if (to == 3) return &k0_3;
			if (to == 1) return &k0_1;
			if (to == 2) return &k0_2;
			break;
		case 1:
			if (to == 0) return &k1_0;
			if (to == 2) return &k1_2;
			if (to == 3) return &k1_3;
			break;
		case 2:
			if (to == 1) return &k2_1;
			if (to == 3) return &k2_3;
			if (to == 0) return &k2_0;
			break;
		case 3:
			if (to == 2) return &k3_2;
			if (to == 0) return &k3_0;
			if (to == 1) return &k3_1;
			break;
		default:
			break;
	}
	return nullptr;
}

} // namespace

Rotation rotate (const Board& board, const Piece& piece, int turns,
                 bool kicks, bool floor_kick) {
	Rotation result{piece, false, false, floor_kick};
	if (turns % kStates == 0) {
		return result;
	}
	Piece spun = piece;
	spun.state = turned(piece.state, turns);
	if (piece.form == O) {
		// An O has one orientation, so turning it changes nothing - not even its
		// recorded state, which the Python engine leaves alone by skipping the
		// whole kick step for this piece. The two have to agree, because that
		// state is what ends up in a replay and in a finesse key.
		return result;
	}
	if (!board.collides(spun)) {
		result.piece = spun;
		result.turned = true;
		return result;
	}
	if (!kicks) {
		// Refused. Committing it would leave the piece inside the stack.
		return result;
	}
	const Candidates* candidates = table(piece.state, spun.state);
	if (candidates == nullptr) {
		return result;
	}
	const std::vector<Offset>& tries =
		piece.form == I ? candidates->line : candidates->normal;
	for (const Offset nudge : tries) {
		// A piece may be lifted once. After that the candidates that would raise it
		// are skipped rather than tried and rejected.
		if (nudge.y < 0 && !result.floor_kick) {
			continue;
		}
		if (nudge.y < 0) {
			result.floor_kick = false;
		}
		Piece nudged = spun;
		nudged.x = piece.x + nudge.x;
		nudged.y = piece.y + nudge.y;
		if (!board.collides(nudged)) {
			result.piece = nudged;
			result.turned = true;
			result.kicked = true;
			return result;
		}
	}
	return result;
}

} // namespace forcetris
