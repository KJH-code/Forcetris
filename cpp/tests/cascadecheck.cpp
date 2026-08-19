// The cascade movement loop's collision verdicts, pinned against the same
// synthetic boards tools/test_cascade.py pins the Python engine with.
//
// The traces grade the cascade styles over whole games, but no natural game
// reaches the movement loop's settled-collision branch: it needs a block
// whose down link points at a cell the fills refuse - the shape a stale
// link would leave - and real play never leaves one standing. The Python
// suite builds that board by hand; this builds the same board through
// Board::set_cell and demands the same ending, fallen flag included.
#include <cstdio>
#include <string>

#include "forcetris/board.hpp"

using namespace forcetris;

namespace {

int failures = 0;

void check (const char* name, bool ok, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		!ok && !detail.empty() ? " -- " : "", !ok ? detail.c_str() : "");
	if (!ok) {
		++failures;
	}
}

// The board as tools/test_cascade.py prints it: rows from the first
// occupied one down, dots for holes, the form digit for blocks.
std::string rows_of (const Board& board) {
	std::string out;
	bool started = false;
	for (int y = 0; y < kHeight; ++y) {
		std::string row;
		bool any = false;
		for (int x = 0; x < kWidth; ++x) {
			const int cell = board.at(x, y);
			any = any || cell >= 0;
			row.push_back(cell >= 0 ? static_cast<char>('0' + cell) : '.');
		}
		started = started || any;
		if (started) {
			out += (out.empty() ? "" : "|") + row;
		}
	}
	return out;
}

// One whole clear, the way the sim drives it: a scan, then settle steps
// until nothing moves, until a scan finds nothing. Returns false on a hang.
bool settle (Board& board, int cleartype) {
	for (int guard = 0; guard < 100; ++guard) {
		int base_row = 0;
		int downstacked = 0;
		if (board.clear_pass(cleartype, base_row, downstacked) == 0) {
			return true;
		}
		int steps = 0;
		while (board.cascade_step(cleartype, base_row)) {
			if (++steps > 100) {
				return false;
			}
		}
	}
	return false;
}

} // namespace

int main () {
	// A fall stopped by settled ground mid-flight settles where it stops:
	// the dangling down link exempts the single from the locked pre-test,
	// the fill refuses the garbage, and the movement collision pastes it
	// back settled - the branch, and the flag, no trace reaches.
	{
		Board board;
		for (int x = 0; x < 9; ++x) {
			board.set_cell(x, 21, S, 0, true);
		}
		board.set_cell(9, 21, I, 0, true);
		for (int y = 18; y <= 20; ++y) {
			board.set_cell(9, y, I, 0, true);
		}
		board.set_cell(5, 19, GARBAGE, 0, true);
		board.set_cell(5, 17, S, 4, true);
		check("a fall onto garbage finishes", settle(board, 2));
		check("and stops on the garbage, one row short",
			rows_of(board) == ".....3....|.....7...0|.........0|.........0",
			rows_of(board));
		check("settled by the collision branch, not parked by the pre-test",
			board.at(5, 18) == S && board.fallen_at(5, 18),
			"the landing was pasted back floating");
	}
	// Two floating shapes never block each other: the fills cut the lower
	// one out before the upper one's turn, so they fall in lockstep.
	{
		Board board;
		for (int x = 0; x < 9; ++x) {
			board.set_cell(x, 21, S, 0, true);
		}
		board.set_cell(9, 21, I, 0, true);
		for (int y = 18; y <= 20; ++y) {
			board.set_cell(9, y, I, 0, true);
		}
		board.set_cell(0, 19, S, 4, true);
		board.set_cell(0, 20, S, 1, true);
		board.set_cell(0, 18, S, 0, true);
		check("the follower finishes", settle(board, 2));
		check("stacked where they started, one row down",
			rows_of(board) == "3........0|3........0|3........0",
			rows_of(board));
	}
	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
