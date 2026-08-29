// The chaos cards' two simmed halves, graded: the crooked judge and the
// ring walls.
//
// Both are trades, and a trade is only honest if both sides land - so each
// block here proves the gift AND the price, and proves the board is
// untouched with the flag down. Nothing rolls a die: the judge reads the
// stack, the ring reads the wall, and the same inputs give the same game
// every time, which is what lets a replay of a cursed run be watched at all.
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "forcetris/attack.hpp"
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

// A board the tests can drive by hand: no gravity worth the name, no fuse,
// instant handling so a tap lands where it is aimed.
SimConfig plain () {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.arr_ms = 40;
	config.fall_delay = 1000;
	config.clear_delay = false;
	return config;
}

void wait_spawn (Sim& sim) {
	for (int i = 0; i < 200 && !sim.entry(); ++i) {
		sim.step(std::nullopt);
	}
}

void tap (Sim& sim, Key key) {
	sim.step(Event{key, true});
	sim.step(Event{key, false});
}

// A floor with a one-cell-wide notch left open at `hole`, `depth` rows deep:
// a piece dropped into it is wedged on both sides.
Board notched (int hole, int depth) {
	Board board;
	for (int y = kHeight - depth; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (x < hole || x > hole + 1) {
				board.set(x, y, 5);
			}
		}
	}
	return board;
}

int spin_of (const Sim& sim) {
	return sim.locked().empty() ? -1 : sim.locked().back().spin;
}

} // namespace

int main () {
	// --- The Crooked Judge. -------------------------------------------------
	// An O dropped straight into a two-wide well is boxed in on both flanks,
	// so the crooked judge calls it a full spin - no rotation, no corner
	// rule, nothing an honest rule would ever accept.
	{
		SimConfig config = plain();
		config.wild_spins = true;
		Sim sim(config, std::vector<int>(20, static_cast<int>(O)));
		sim.seed(notched(kSpawnX, 4));
		wait_spawn(sim);
		tap(sim, Key::Hard);
		check("the crooked judge calls a wedged O a spin",
			spin_of(sim) == attack::SPIN_FULL,
			std::to_string(spin_of(sim)));
	}
	// The same drop with the judge honest is no spin at all: an O is never
	// one, which is what makes the card's gift visible.
	{
		SimConfig config = plain();
		Sim sim(config, std::vector<int>(20, static_cast<int>(O)));
		sim.seed(notched(kSpawnX, 4));
		wait_spawn(sim);
		tap(sim, Key::Hard);
		check("and an honest judge never does",
			spin_of(sim) == attack::NOT_SPIN,
			std::to_string(spin_of(sim)));
	}
	// The price: a piece resting in the open is not wedged, so under the
	// crooked judge a spin done with room to slide out stops counting -
	// even for a T, even rotated on the spot.
	{
		SimConfig config = plain();
		config.wild_spins = true;
		Sim sim(config, std::vector<int>(20, static_cast<int>(T)));
		wait_spawn(sim);
		tap(sim, Key::Cw);
		tap(sim, Key::Hard);
		check("but a loose piece is nothing, however it was turned",
			spin_of(sim) == attack::NOT_SPIN,
			std::to_string(spin_of(sim)));
	}

	// --- The Ring. ----------------------------------------------------------
	// Walked to the left wall one press at a time and asked for one column
	// more, the piece comes back against the RIGHT wall - and its far edge
	// really is that wall, not some column short of it.
	{
		SimConfig config = plain();
		config.wrap_walls = true;
		Sim sim(config, std::vector<int>(20, static_cast<int>(O)));
		wait_spawn(sim);
		for (int i = 0; i < kWidth; ++i) {
			tap(sim, Key::Left);
			if (sim.piece().x == 0) {
				break;
			}
		}
		const int against_wall = sim.piece().x;
		tap(sim, Key::Left);
		int hi = -1;
		for (const Offset cell : cells_of(sim.piece())) {
			hi = std::max(hi, cell.x);
		}
		check("the ring carries a piece from one wall to the other",
			against_wall == 0 && hi == kWidth - 1,
			std::to_string(against_wall) + " -> " + std::to_string(hi));
	}
	// With the flag down the wall is a wall: the same press goes nowhere.
	{
		SimConfig config = plain();
		Sim sim(config, std::vector<int>(20, static_cast<int>(O)));
		wait_spawn(sim);
		for (int i = 0; i < kWidth; ++i) {
			tap(sim, Key::Left);
			if (sim.piece().x == 0) {
				break;
			}
		}
		tap(sim, Key::Left);
		check("a plain wall stays a wall", sim.piece().x == 0,
			std::to_string(sim.piece().x));
	}
	// A held direction keeps going: the auto-shift walks the piece to the
	// wall, through the ring and on across the board, so a finger left down
	// visits both edges - which is the whole feel the card sells.
	{
		SimConfig config = plain();
		config.wrap_walls = true;
		Sim sim(config, std::vector<int>(20, static_cast<int>(O)));
		wait_spawn(sim);
		bool touched_left = false;
		bool touched_right = false;
		sim.step(Event{Key::Left, true});
		for (int i = 0; i < 200; ++i) {
			sim.step(std::nullopt);
			touched_left = touched_left || sim.piece().x == 0;
			touched_right = touched_right || sim.piece().x == kWidth - 2;
		}
		sim.step(Event{Key::Left, false});
		check("a held direction rides the ring round",
			touched_left && touched_right,
			std::to_string(static_cast<int>(touched_left))
				+ std::to_string(static_cast<int>(touched_right)));
	}
	// The ring is a door, never a tunnel: with the far side walled off the
	// crossing is refused, exactly as a block in the way refuses a step.
	{
		SimConfig config = plain();
		config.wrap_walls = true;
		Sim sim(config, std::vector<int>(20, static_cast<int>(O)));
		Board board;
		for (int y = 0; y < kHeight; ++y) {
			board.set(kWidth - 1, y, 5);
			board.set(kWidth - 2, y, 5);
		}
		sim.seed(board);
		wait_spawn(sim);
		for (int i = 0; i < kWidth; ++i) {
			tap(sim, Key::Left);
			if (sim.piece().x == 0) {
				break;
			}
		}
		tap(sim, Key::Left);
		check("a blocked far side refuses the crossing", sim.piece().x == 0,
			std::to_string(sim.piece().x));
	}

	std::printf("%s\n", failures == 0 ? "All checks passed."
		: "Some checks FAILED.");
	return failures == 0 ? 0 : 1;
}
