#include "forcetris/board.hpp"

#include <algorithm>
#include <vector>

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
	for (auto& row : links_) {
		row.fill(0);
	}
	for (auto& row : fallen_) {
		row.fill(false);
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
	// Hand-placed cells are settled rubble with no links, the way a seeded
	// board's blocks are built.
	set_cell(x, y, form, 0, form >= 0);
}

void Board::set_cell (int x, int y, int form, unsigned char links, bool fallen) {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		return;
	}
	cells_[y][x] = form;
	links_[y][x] = form >= 0 ? links : 0;
	fallen_[y][x] = form >= 0 && fallen;
}

unsigned char Board::links_at (int x, int y) const {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		return 0;
	}
	return links_[y][x];
}

bool Board::fallen_at (int x, int y) const {
	if (x < 0 || x >= kWidth || y < 0 || y >= kHeight) {
		// The walls and the floor are as settled as anything can be.
		return true;
	}
	return fallen_[y][x];
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
	// A locking piece keeps its intra-piece links - the linked cascade style
	// follows them later - and is not yet settled: only a cascade, or never
	// being asked, makes it so.
	const Cells& shape = offsets(piece.form, piece.state);
	for (const Offset rel : shape) {
		set_cell(piece.x + rel.x, piece.y + rel.y, piece.form,
			link_mask(piece.form, piece.state, rel), false);
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
			links_[above] = links_[above - 1];
			fallen_[above] = fallen_[above - 1];
		}
		cells_[0].fill(-1);
		links_[0].fill(0);
		fallen_[0].fill(false);
		// The row that dropped into this one has not been looked at yet.
		++y;
	}
	return taken;
}

void Board::push_garbage (int hole) {
	push_garbage_mask(1 << hole);
}

void Board::push_garbage_mask (int mask) {
	for (int y = 0; y + 1 < kHeight; ++y) {
		cells_[y] = cells_[y + 1];
		links_[y] = links_[y + 1];
		fallen_[y] = fallen_[y + 1];
	}
	const auto open = [mask] (int x) { return (mask >> x & 1) != 0; };
	for (int x = 0; x < kWidth; ++x) {
		const bool block = !open(x);
		cells_[kHeight - 1][x] = block ? GARBAGE : -1;
		// Chained left and right, never across a hole, as add_garbage
		// chains them: each stretch of garbage falls as one, or not at all.
		unsigned char links = 0;
		if (block) {
			if (x != 0 && !open(x - 1)) {
				links |= 8;   // Left.
			}
			if (x != kWidth - 1 && !open(x + 1)) {
				links |= 2;   // Right.
			}
		}
		links_[kHeight - 1][x] = links;
		fallen_[kHeight - 1][x] = block;
	}
}

int Board::garbage_rows () const {
	int rows = 0;
	for (int y = 0; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (cells_[y][x] == GARBAGE) {
				++rows;
				break;
			}
		}
	}
	return rows;
}

int Board::clear_pass (int cleartype, int& base_row, int& downstacked) {
	// One iteration of the clearer's `while cleared` loop: the bottom-up row
	// scan, the link surgery, and either the in-place blanking the cascade
	// styles use or the splice that naive rows and garbage rows always get.
	int taken = 0;
	base_row = 0;
	for (int y = kHeight - 1; y >= 0; --y) {
		bool full = true;
		for (int x = 0; x < kWidth; ++x) {
			if (cells_[y][x] < 0) {
				full = false;
				break;
			}
		}
		if (!full) {
			continue;
		}
		// The clearer's cheap garbage test: is either of the first two cells
		// grey. Digging such a row out is the downstack half of the VS score.
		const bool garbagerow =
			cells_[y][0] == GARBAGE || cells_[y][1] == GARBAGE;
		if (garbagerow) {
			++downstacked;
		}
		++taken;
		if (y > base_row) {
			base_row = y;
		}
		for (int x = 0; x < kWidth; ++x) {
			// The cleared block's links decide; the neighbour's links change.
			if (y + 1 < kHeight && cells_[y + 1][x] >= 0 && (links_[y][x] & 4)) {
				links_[y + 1][x] &= static_cast<unsigned char>(~1);
			}
			if (y - 1 >= 0 && cells_[y - 1][x] >= 0 && (links_[y][x] & 1)) {
				links_[y - 1][x] &= static_cast<unsigned char>(~4);
			}
			if (cleartype > 0) {
				cells_[y][x] = -1;
				links_[y][x] = 0;
				fallen_[y][x] = false;
			}
		}
		if (cleartype < 1 || garbagerow) {
			// Splice the row out and put a blank one on top. Under naive this
			// is every row - one per pass - and under the cascade styles it is
			// the garbage rows, which also end the pass where they stood.
			for (int above = y; above > 0; --above) {
				cells_[above] = cells_[above - 1];
				links_[above] = links_[above - 1];
				fallen_[above] = fallen_[above - 1];
			}
			cells_[0].fill(-1);
			links_[0].fill(0);
			fallen_[0].fill(false);
			return taken;
		}
	}
	return taken;
}

void Board::fill_group (int cleartype, int x, int y, std::vector<Loose>& group) {
	// The fills, iteratively, preserving the recursion's preorder: the cell
	// itself, then Up, Right, Down, Left. Only floating cells are cut - a
	// group that touches something settled is left to the locked test - and
	// the walks stay on the playable field.
	std::vector<Offset> stack{{x, y}};
	while (!stack.empty()) {
		const Offset at = stack.back();
		stack.pop_back();
		if (at.x < 0 || at.x >= kWidth || at.y < 0 || at.y >= kHeight) {
			continue;
		}
		if (cells_[at.y][at.x] < 0 || fallen_[at.y][at.x]) {
			continue;
		}
		const unsigned char links = links_[at.y][at.x];
		group.push_back(Loose{at, cells_[at.y][at.x], links});
		cells_[at.y][at.x] = -1;
		links_[at.y][at.x] = 0;
		// Pushed in reverse so Up pops first, matching the recursion.
		if (cleartype == 1) {
			stack.push_back({at.x - 1, at.y});
			stack.push_back({at.x, at.y + 1});
			stack.push_back({at.x + 1, at.y});
			stack.push_back({at.x, at.y - 1});
		} else {
			if (links & 8) {
				stack.push_back({at.x - 1, at.y});
			}
			if (links & 4) {
				stack.push_back({at.x, at.y + 1});
			}
			if (links & 2) {
				stack.push_back({at.x + 1, at.y});
			}
			if (links & 1) {
				stack.push_back({at.x, at.y - 1});
			}
		}
	}
}

bool Board::cascade_step (int cleartype, int base_row) {
	// Everything above and one row below the cleared rows floats again -
	// except garbage, which never does.
	for (int y = 0; y <= std::min(kHeight - 1, base_row + 1); ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (cells_[y][x] >= 0 && cells_[y][x] != GARBAGE) {
				fallen_[y][x] = false;
			}
		}
	}
	const auto occupied = [this] (int x, int y) {
		return y >= kHeight || (y >= 0 && x >= 0 && x < kWidth && cells_[y][x] >= 0);
	};
	std::vector<std::vector<Loose>> moved;
	for (int y = kHeight - 1; y >= 0; --y) {
		std::vector<std::vector<Loose>> tempshapes;
		for (int x = 0; x < kWidth; ++x) {
			if (cells_[y][x] < 0 || fallen_[y][x]) {
				continue;
			}
			std::vector<Loose> group;
			fill_group(cleartype, x, y, group);
			// The locked test: a member with no Down link over an occupied
			// cell is resting on something outside the group - the group's
			// own cells are already lifted out - so the group stays.
			bool locked = false;
			for (const Loose& member : group) {
				if (!(member.links & 4) && occupied(member.at.x, member.at.y + 1)) {
					locked = true;
					break;
				}
			}
			if (locked) {
				for (const Loose& member : group) {
					cells_[member.at.y][member.at.x] = member.form;
					links_[member.at.y][member.at.x] = member.links;
					// Still floating: pasted back without the settled flag.
				}
				continue;
			}
			tempshapes.push_back(std::move(group));
		}
		for (std::vector<Loose>& group : tempshapes) {
			// One row down, one verdict: the first cell of the group - in the
			// fill's own order - that lands on something decides for all.
			const Loose* blocking = nullptr;
			bool onto_settled = false;
			for (const Loose& member : group) {
				const int below = member.at.y + 1;
				if (below >= kHeight) {
					blocking = &member;
					onto_settled = true;
					break;
				}
				if (cells_[below][member.at.x] >= 0) {
					blocking = &member;
					onto_settled = fallen_[below][member.at.x];
					break;
				}
			}
			if (blocking == nullptr) {
				for (Loose& member : group) {
					member.at.y += 1;
				}
				moved.push_back(std::move(group));
				continue;
			}
			for (const Loose& member : group) {
				cells_[member.at.y][member.at.x] = member.form;
				links_[member.at.y][member.at.x] = member.links;
				// Landed on something settled: settled too. Blocked by
				// another floating group: stay put and retry next step - a
				// backstop the fills never actually feed, kept so that
				// termination does not hinge on proving they cannot.
				fallen_[member.at.y][member.at.x] = onto_settled;
			}
		}
	}
	for (const std::vector<Loose>& group : moved) {
		for (const Loose& member : group) {
			cells_[member.at.y][member.at.x] = member.form;
			links_[member.at.y][member.at.x] = member.links;
			fallen_[member.at.y][member.at.x] = false;
		}
	}
	return !moved.empty();
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
