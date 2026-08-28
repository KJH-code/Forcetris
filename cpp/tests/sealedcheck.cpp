// Sealed Columns, pinned from the board up: a sealed column is wall for
// collision and the spin rules, a row completes without it, clears and
// resets leave the mask standing, and at mask zero the board is
// bit-for-bit the board it always was.
#include <cstdio>
#include <string>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/sim.hpp"

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

// The stage's own dress rehearsal: the outer columns walled off.
constexpr int kEdges = (1 << 0) | (1 << (kWidth - 1));

SimConfig base_config () {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.arr_ms = 0;
	config.clear_delay = false;
	config.sealed = kEdges;
	return config;
}

void wait_spawn (Sim& sim) {
	for (int i = 0; i < 100 && !sim.entry(); ++i) {
		sim.step(std::nullopt);
	}
}

void tap (Sim& sim, Key key) {
	sim.step(Event{key, true});
	sim.step(Event{key, false});
}

// A one-row well between the sealed edges: columns 1..8 filled except
// where a vertical I stands (kSpawnX + 1, as clearcheck's well digs it),
// so the I dropped upright completes the row.
Board notched () {
	Board board;
	for (int x = 1; x < kWidth - 1; ++x) {
		if (x != kSpawnX + 1) {
			board.set(x, kHeight - 1, S);
		}
	}
	return board;
}

} // namespace

int main () {
	// --- The board alone. ------------------------------------------------
	{
		Board board;
		board.set_sealed(kEdges);
		check("a sealed cell reads as wall", board.at(0, kHeight - 1) == GARBAGE);
		check("all the way up the visible field", board.at(0, 0) == GARBAGE);
		check("but not above the matrix", board.at(0, -1) == -1);
		check("an unsealed cell still reads empty", board.at(1, kHeight - 1) == -1);

		// A flat I against the left edge: cells at x 0..3 overlap the seal.
		Piece flat{I, 0, 1, kHeight - 2};
		check("a piece overlapping the seal collides", board.collides(flat));
		flat.x = 2;   // Cells at 1..4: clear of the wall.
		check("and slides free one column in", !board.collides(flat));

		// A row completes without its sealed columns.
		for (int x = 1; x < kWidth - 1; ++x) {
			board.set(x, kHeight - 1, S);
		}
		check("a row completes without the sealed columns",
			board.clear_lines() == 1);
		check("and the clear leaves the mask standing",
			board.sealed() == kEdges && board.at(0, 10) == GARBAGE);

		board.clear();
		check("clear() leaves it standing too", board.sealed() == kEdges);
		check("a full reset board is empty", board.empty());
	}
	{
		// Mask zero is the board as it always was.
		Board board;
		Piece flat{I, 0, 1, kHeight - 2};
		check("at mask zero the edge column is open",
			board.at(0, kHeight - 1) == -1 && !board.collides(flat));
	}

	// --- Through the sim. ------------------------------------------------
	{
		// DAS to the wall under ARR 0 stops the piece against the seal, not
		// against the real wall: an O held left comes to rest one column in.
		Sim sim(base_config(), std::vector<int>{O, O, O, O});
		wait_spawn(sim);
		sim.step(Event{Key::Left, true});
		for (int i = 0; i < 30; ++i) {
			sim.step(std::nullopt);
		}
		sim.step(Event{Key::Left, false});
		// The O's cells sit at x and x+1; against the seal the left cell
		// stands in column 1.
		check("auto-shift stops against the seal", sim.piece().x == 1,
			std::to_string(sim.piece().x));
	}
	{
		// A row completes and clears through the sim without the sealed
		// columns, and the seed keeps the mask.
		Sim sim(base_config(), std::vector<int>{I, I, I, I});
		sim.seed(notched());
		check("seeding keeps the mask", sim.board().sealed() == kEdges);
		wait_spawn(sim);
		tap(sim, Key::Cw);
		tap(sim, Key::Hard);
		for (int i = 0; i < 20; ++i) {
			sim.step(std::nullopt);
		}
		check("a row between the seals clears", sim.lines_cleared() == 1,
			std::to_string(sim.lines_cleared()));
		check("the mask survives the clear", sim.board().sealed() == kEdges);
	}

	std::printf("\n%s\n", failures == 0 ? "All checks passed."
		: "CHECKS FAILED.");
	return failures == 0 ? 0 : 1;
}
