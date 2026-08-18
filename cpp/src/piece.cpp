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

} // namespace forcetris
