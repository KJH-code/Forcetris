#include "forcetris/piece.hpp"

#include <algorithm>
#include <mutex>

namespace forcetris {
namespace {

// The spawn shapes, exactly as Shape.__init__ lays them out.
constexpr std::array<Cells, kForms> kSpawn = {{
	{{ {-1, 0}, { 0, 0}, { 1, 0}, { 2, 0} }},   // I
	{{ { 0,-1}, { 1,-1}, { 1, 0}, { 0, 0} }},   // O
	{{ {-1, 0}, { 0,-1}, { 1, 0}, { 0, 0} }},   // T
	{{ {-1, 0}, { 0, 0}, { 0,-1}, { 1,-1} }},   // S
	{{ {-1,-1}, { 0,-1}, { 0, 0}, { 1, 0} }},   // Z
	{{ {-1,-1}, {-1, 0}, { 0, 0}, { 1, 0} }},   // J
	{{ {-1, 0}, { 0, 0}, { 1, 0}, { 1,-1} }},   // L
}};

// One clockwise turn of a single cell.
//
// Everything but the I turns about its centre cell, so the transform is the
// plain (x, y) -> (-y, x) for a downward y axis. The I turns about the corner
// between its two middle cells, which Shape.rotate reaches by shifting half a
// cell, turning, and shifting back. That works out to (x, y) -> (1 - y, x), and
// the integer form is used here so nothing depends on how floats round.
constexpr Offset spin_cw (int form, Offset cell) {
	return form == I ? Offset{1 - cell.y, cell.x} : Offset{-cell.y, cell.x};
}

Cells rotate (int form, Cells cells) {
	if (form == O) {
		// The O has no orientation to change.
		return cells;
	}
	for (Offset& cell : cells) {
		cell = spin_cw(form, cell);
	}
	std::sort(cells.begin(), cells.end());
	return cells;
}

// Built once, on first use.
std::array<std::array<Cells, kStates>, kForms> build () {
	std::array<std::array<Cells, kStates>, kForms> table{};
	for (int form = 0; form < kForms; ++form) {
		Cells cells = kSpawn[form];
		std::sort(cells.begin(), cells.end());
		table[form][0] = cells;
		for (int state = 1; state < kStates; ++state) {
			cells = rotate(form, cells);
			table[form][state] = cells;
		}
	}
	return table;
}

} // namespace

const Cells& offsets (int form, int state) {
	static const auto table = build();
	static constexpr Cells empty{};
	if (form < 0 || form >= kForms || state < 0 || state >= kStates) {
		return empty;
	}
	return table[form][state];
}

namespace {

// The spawn links, cell for cell in kSpawn's order, as Shape.__init__ writes
// them: bit 0 up, 1 right, 2 down, 3 left.
constexpr std::array<std::array<unsigned char, kCells>, kForms> kSpawnLinks = {{
	{{ 2, 10, 10, 8 }},   // I:  R, RL, RL, L
	{{ 6, 12,  9, 3 }},   // O:  RD, DL, LU, UR
	{{ 2,  4,  8, 11 }},  // T:  R, D, L, URL
	{{ 2,  9,  6, 8 }},   // S:  R, UL, RD, L
	{{ 2, 12,  3, 8 }},   // Z:  R, DL, UR, L
	{{ 4,  3, 10, 8 }},   // J:  D, UR, RL, L
	{{ 2, 10,  9, 4 }},   // L:  R, RL, UL, D
}};

struct Linked {
	Offset cell;
	unsigned char mask;
};

// One clockwise turn: the cell turns as spin_cw turns it, and every link
// turns with it - up becomes right becomes down becomes left.
std::array<std::array<std::array<Linked, kCells>, kStates>, kForms> build_links () {
	std::array<std::array<std::array<Linked, kCells>, kStates>, kForms> table{};
	for (int form = 0; form < kForms; ++form) {
		std::array<Linked, kCells> cells{};
		for (int i = 0; i < kCells; ++i) {
			cells[i] = Linked{kSpawn[form][i], kSpawnLinks[form][i]};
		}
		table[form][0] = cells;
		for (int state = 1; state < kStates; ++state) {
			if (form != O) {
				// The O never turns, links included.
				for (Linked& linked : cells) {
					linked.cell = spin_cw(form, linked.cell);
					linked.mask = static_cast<unsigned char>(
						((linked.mask << 1) | (linked.mask >> 3)) & 0xF);
				}
			}
			table[form][state] = cells;
		}
	}
	return table;
}

} // namespace

unsigned char link_mask (int form, int state, Offset cell) {
	static const auto table = build_links();
	if (form < 0 || form >= kForms || state < 0 || state >= kStates) {
		return 0;
	}
	for (const Linked& linked : table[form][state]) {
		if (linked.cell == cell) {
			return linked.mask;
		}
	}
	return 0;
}

} // namespace forcetris
