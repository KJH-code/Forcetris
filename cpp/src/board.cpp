#include "forcetris/board.hpp"

#include <algorithm>

namespace forcetris {

std::array<Offset, kCells> cells_of (const Piece& piece) {
	const Cells& shape = offsets(piece.form, piece.state);
	std::array<Offset, kCells> found{};
	for (int i = 0; i < kCells; ++i) {
		found[i] = Offset{piece.x + shape[i].x, piece.y + shape[i].y};
	}
	return found;
}

Board::Board () { clear(); }

void Board::clear () {
	for (auto& row : cells_) {
		row.fill(-1);
	}
}

Board Board::from_rows (const std::vector<std::string>& rows) {
	Board board;
	const int top = kHeight - static_cast<int>(rows.size());
	for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
		const int y = top + i;
		if (y < 0) {
			continue;
		}
		const std::string& row = rows[i];
		for (int x = 0; x < kWidth && x < static_cast<int>(row.size()); ++x) {
			const char mark = row[x];
			board.set(x, y, mark == '.' ? -1 : mark - '0');
		}
	}
	return board;
}

int Board::at (int x, int y) const {
	if (x < 0 || x >= kWidth || y >= kHeight) {
		// The walls and the floor. Solid, so a piece wedged against one counts as
		// wedged in for the spin rules.
		return GARBAGE;
	}
	if (y < 0) {
		// Above the matrix, where a spawning piece legitimately sits.
		return -1;
	}
	return cells_[y][x];
}

void Board::set (int x, int y, int form) {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		return;
	}
	cells_[y][x] = form;
}

bool Board::collides (const Piece& piece) const {
	for (const Offset cell : cells_of(piece)) {
		if (cell.y < 0) {
			continue;
		}
		if (cell.x < 0 || cell.x >= kWidth || cell.y >= kHeight) {
			return true;
		}
		if (cells_[cell.y][cell.x] >= 0) {
			return true;
		}
	}
	return false;
}

Piece Board::dropped (const Piece& piece) const {
	Piece falling = piece;
	while (true) {
		Piece lower = falling;
		lower.y += 1;
		if (collides(lower)) {
			return falling;
		}
		falling = lower;
	}
}

bool Board::paste (const Piece& piece) {
	if (collides(piece)) {
		return false;
	}
	for (const Offset cell : cells_of(piece)) {
		set(cell.x, cell.y, piece.form);
	}
	return true;
}

int Board::clear_lines () {
	int taken = 0;
	for (int y = kHeight - 1; y >= 0; --y) {
		const bool full = std::all_of(
			cells_[y].begin(), cells_[y].end(), [] (int cell) { return cell >= 0; });
		if (!full) {
			continue;
		}
		++taken;
		for (int above = y; above > 0; --above) {
			cells_[above] = cells_[above - 1];
		}
		cells_[0].fill(-1);
		// The row that dropped into this one has not been looked at yet.
		++y;
	}
	return taken;
}

bool Board::empty () const {
	for (const auto& row : cells_) {
		for (const int cell : row) {
			if (cell >= 0) {
				return false;
			}
		}
	}
	return true;
}

std::vector<std::string> Board::rows () const {
	std::vector<std::string> found;
	found.reserve(kHeight);
	for (const auto& row : cells_) {
		std::string line(kWidth, '.');
		for (int x = 0; x < kWidth; ++x) {
			if (row[x] >= 0) {
				line[x] = static_cast<char>('0' + row[x]);
			}
		}
		found.push_back(std::move(line));
	}
	while (!found.empty() && found.front().find_first_not_of('.') == std::string::npos) {
		found.erase(found.begin());
	}
	return found;
}

} // namespace forcetris
