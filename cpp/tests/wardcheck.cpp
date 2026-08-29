// The ward cards' simmed halves, graded: the free hand, the floor sweep and
// the cold shoulder - plus the rubble dealer the sifter leans on.
//
// A ward is a promise about what will NOT happen to you, which is the
// hardest kind to see, so every block here proves the guarded case AND the
// unguarded one from the same inputs. Nothing rolls a die on the sim's side:
// the sweep is a counter, the shoulder is arithmetic, and the dealer's dice
// are the Session's, seeded and repeatable.
#include <cstdio>
#include <deque>
#include <optional>
#include <string>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/replay.hpp"
#include "forcetris/sim.hpp"
#include "../gui/session.hpp"

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

// A board the tests drive by hand: no gravity worth the name, no fuse,
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

// Rows `rows` deep with the right-hand four columns open, filled with `fill`
// - GARBAGE for rubble a sweep may take, anything else for the player's own
// stack, which it may not. A flat I laid in the gutter completes exactly one
// row, so every lock is one clear and the counter is easy to read.
Board floored (int rows, int fill) {
	Board board;
	for (int y = kHeight - rows; y < kHeight; ++y) {
		for (int x = 0; x + 4 < kWidth; ++x) {
			board.set(x, y, fill);
		}
	}
	return board;
}

// Lay `count` flat I pieces into the right-hand gutter, one clear each.
void dig (Sim& sim, int count) {
	for (int i = 0; i < count; ++i) {
		wait_spawn(sim);
		for (int step = 0; step < kWidth; ++step) {
			tap(sim, Key::Right);
		}
		tap(sim, Key::Hard);
		for (int settle = 0; settle < 30; ++settle) {
			sim.step(std::nullopt);
		}
	}
}

} // namespace

int main () {
	// --- The Free Hand. -----------------------------------------------------
	// The box locks after one swap and the second press does nothing: that is
	// the rule the card buys out of.
	{
		SimConfig config = plain();
		Sim sim(config, {static_cast<int>(I), static_cast<int>(O),
			static_cast<int>(T), static_cast<int>(S), static_cast<int>(Z)});
		wait_spawn(sim);
		tap(sim, Key::Hold);   // Fills the empty box; no swap to lock yet.
		tap(sim, Key::Hold);   // The first real swap.
		const int once = sim.piece().form;
		tap(sim, Key::Hold);
		check("the hold box locks after one swap",
			sim.piece().form == once && sim.hold_locked(),
			std::to_string(sim.piece().form));
	}
	// With a free hand the same two presses swap twice, and the box never
	// reads locked - so the screen never dims it and needs to know nothing
	// about the card.
	{
		SimConfig config = plain();
		config.free_hold = true;
		Sim sim(config, {static_cast<int>(I), static_cast<int>(O),
			static_cast<int>(T), static_cast<int>(S), static_cast<int>(Z)});
		wait_spawn(sim);
		tap(sim, Key::Hold);
		tap(sim, Key::Hold);
		const int once = sim.piece().form;
		tap(sim, Key::Hold);
		check("a free hand swaps again, and the box never dims",
			sim.piece().form != once && !sim.hold_locked(),
			std::to_string(once) + " -> "
				+ std::to_string(sim.piece().form));
	}
	// A piece may not swap with itself, free hand or not: the stored form
	// guard is what stops an endless swap on one press held down.
	{
		SimConfig config = plain();
		config.free_hold = true;
		Sim sim(config, std::vector<int>(8, static_cast<int>(T)));
		wait_spawn(sim);
		tap(sim, Key::Hold);
		const int held = sim.stored();
		tap(sim, Key::Hold);
		check("and never swaps a piece with its own twin",
			sim.stored() == held && held == static_cast<int>(T));
	}
	// The fuse rides through every swap, first or fifth. A free hand that
	// reset the piece clock would be a refill on demand - the whole reason
	// the fuse branch exists in the swap.
	{
		SimConfig config = plain();
		config.fuse = true;
		config.free_hold = true;
		config.forced_delay = 4.;
		Sim sim(config, {static_cast<int>(I), static_cast<int>(O),
			static_cast<int>(T), static_cast<int>(S), static_cast<int>(Z)});
		wait_spawn(sim);
		tap(sim, Key::Hold);   // Fill the box, so the swaps below are swaps.
		for (int i = 0; i < 40; ++i) {
			sim.step(std::nullopt);
		}
		const double burned = sim.piece_elapsed().value_or(-1.);
		tap(sim, Key::Hold);
		tap(sim, Key::Hold);
		const double after = sim.piece_elapsed().value_or(-1.);
		check("a free hand never refills the wick",
			burned > 0. && after >= burned - 1e-9,
			std::to_string(burned) + " -> " + std::to_string(after));
	}
	// The press record is cleared on the FIRST swap only. Clearing it every
	// time would let a player wash their finesse before every lock.
	{
		SimConfig config = plain();
		config.free_hold = true;
		Sim sim(config, {static_cast<int>(I), static_cast<int>(O),
			static_cast<int>(T), static_cast<int>(S), static_cast<int>(Z)});
		wait_spawn(sim);
		tap(sim, Key::Hold);   // Fill the box.
		tap(sim, Key::Left);
		tap(sim, Key::Hold);   // The first swap wipes that press...
		tap(sim, Key::Left);
		tap(sim, Key::Hold);   // ...the second must keep this one.
		tap(sim, Key::Hard);
		const int counted = sim.locked().empty() ? -1
			: sim.locked().back().inputs;
		check("only the first swap clears the press record",
			counted > 0, std::to_string(counted));
	}

	// --- The Floor Sweep. ---------------------------------------------------
	// Eight clears made over rubble, and the eighth takes the bottom garbage
	// row with it: one extra row dug, one extra line counted, and the rubble
	// is one shorter than the digging alone would leave it.
	{
		SimConfig config = plain();
		config.sweep_every = 8;
		Sim sim(config, std::vector<int>(60, static_cast<int>(I)));
		sim.seed(floored(12, GARBAGE));
		dig(sim, 8);
		check("the eighth clear over rubble sweeps the floor",
			sim.lines_cleared() == 9 && sim.downstack() == 9
				&& sim.board().garbage_rows() == 3,
			std::to_string(sim.lines_cleared()) + " lines, "
				+ std::to_string(sim.board().garbage_rows()) + " rubble");
	}
	// The same eight clears with the card down: eight lines, four rows left.
	// The two runs differ by exactly the one row the card promises.
	{
		SimConfig config = plain();
		Sim sim(config, std::vector<int>(60, static_cast<int>(I)));
		sim.seed(floored(12, GARBAGE));
		dig(sim, 8);
		check("and without the card the floor stays where it is",
			sim.lines_cleared() == 8 && sim.downstack() == 8
				&& sim.board().garbage_rows() == 4,
			std::to_string(sim.lines_cleared()) + " lines, "
				+ std::to_string(sim.board().garbage_rows()) + " rubble");
	}
	// The counter only ticks on clears that had rubble to sweep, so a clean
	// board can never bank a sweep for later. Eight clears with nothing down
	// leave the counter where it started: the ninth clear, made over fresh
	// rubble, must not fire.
	{
		SimConfig config = plain();
		config.sweep_every = 8;
		Sim sim(config, std::vector<int>(60, static_cast<int>(I)));
		sim.seed(floored(8, 5));   // The player's own stack, not rubble.
		dig(sim, 8);
		check("a clean board never banks a sweep",
			sim.lines_cleared() == 8 && sim.downstack() == 0,
			std::to_string(sim.lines_cleared()) + " lines, "
				+ std::to_string(sim.downstack()) + " dug");
	}

	// --- The Cold Shoulder. -------------------------------------------------
	// The house rounding, row by row: a blow is thinned but never erased.
	{
		SimConfig config = plain();
		config.gametype = 5;
		config.garbage_scale = 0.75;
		const int table[6][2] = {{1, 1}, {2, 2}, {3, 2}, {4, 3}, {6, 4},
			{8, 6}};
		bool right = true;
		std::string detail;
		for (const auto& row : table) {
			Sim sim(config, std::vector<int>(8, static_cast<int>(I)));
			sim.receive_attack(row[0]);
			if (sim.pending_garbage() != row[1]) {
				right = false;
				detail += std::to_string(row[0]) + "->"
					+ std::to_string(sim.pending_garbage()) + " want "
					+ std::to_string(row[1]) + "; ";
			}
		}
		check("the cold shoulder thins a blow the way the house rounds",
			right, detail);
	}
	// The floor: however cold the shoulder, a landed blow weighs at least a
	// row. Nothing the player carries can make an attack free.
	{
		SimConfig config = plain();
		config.gametype = 5;
		config.garbage_scale = 0.5;
		Sim sim(config, std::vector<int>(8, static_cast<int>(I)));
		sim.receive_attack(1);
		check("a blow that landed never weighs nothing",
			sim.pending_garbage() == 1,
			std::to_string(sim.pending_garbage()));
	}
	// And with no shoulder the queue is the blow, untouched - the default
	// path is the one every duel shipped so far has taken.
	{
		SimConfig config = plain();
		config.gametype = 5;
		Sim sim(config, std::vector<int>(8, static_cast<int>(I)));
		sim.receive_attack(7);
		sim.receive_attack(-3);
		check("and an unwarded board takes the whole blow",
			sim.pending_garbage() == 7,
			std::to_string(sim.pending_garbage()));
	}

	// --- The rubble dealer. -------------------------------------------------
	// The Sifter buys messiness down, and messiness is the odds that a new
	// garbage row re-rolls its holes rather than copying the row below. At
	// full messiness consecutive rows differ; at the floor the sifter can
	// reach, they mostly line up. Graded through Session, which owns the
	// dice - the sim never rolls its own.
	{
		const auto dealt = [] (int messiness) {
			SimConfig config = plain();
			config.gametype = 3;
			config.cheese_holes = 1;
			config.cheese_messiness = messiness;
			replay::Meta meta;
			gui::Session session(config, 4242u, meta);
			const std::deque<int>& holes = session.sim().garbage_holes();
			return std::vector<int>(holes.begin(), holes.end());
		};
		const std::vector<int> messy = dealt(100);
		const std::vector<int> tidy = dealt(20);
		int messy_repeats = 0;
		int tidy_repeats = 0;
		for (size_t i = 1; i < messy.size(); ++i) {
			messy_repeats += messy[i] == messy[i - 1] ? 1 : 0;
			tidy_repeats += tidy[i] == tidy[i - 1] ? 1 : 0;
		}
		check("full messiness never repeats a hole", messy_repeats == 0,
			std::to_string(messy_repeats));
		check("and a sifted pile lines its holes up",
			tidy_repeats > messy_repeats,
			std::to_string(tidy_repeats) + " vs "
				+ std::to_string(messy_repeats));
	}

	std::printf("%s\n",
		failures == 0 ? "all ward checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
