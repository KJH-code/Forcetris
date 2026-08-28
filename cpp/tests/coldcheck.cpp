// Cold Iron, pinned lock by lock: a completed row freezes where it stands
// instead of clearing - no line credit, a freeze cue - and the first
// clearing pass of a later lock shatters it, paying the credit then. A row
// never freezes and shatters inside the same lock, and the iron mark rides
// its row through every splice.
#include <algorithm>
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

SimConfig base_config (bool delay = false) {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.arr_ms = 0;
	config.clear_delay = delay;
	config.cold_iron = true;
	return config;
}

void run_frames (Sim& sim, int frames) {
	for (int i = 0; i < frames; ++i) {
		sim.step(std::nullopt);
	}
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

// The one-column well where a vertical I stands, `depth` rows deep.
Board welled (int depth) {
	Board board;
	for (int y = kHeight - depth; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (x != kSpawnX + 1) {
				board.set(x, y, S);
			}
		}
	}
	return board;
}

// Drop the current piece upright down the well.
void quad_drop (Sim& sim) {
	tap(sim, Key::Cw);
	tap(sim, Key::Hard);
}

bool cued (const Sim& sim, const char* name) {
	const auto& cues = sim.cues();
	return std::find(cues.begin(), cues.end(), std::string(name))
		!= cues.end();
}

int iron_rows (const Board& board) {
	int count = 0;
	for (int y = 0; y < kHeight; ++y) {
		if (board.iron_row(y)) {
			++count;
		}
	}
	return count;
}

} // namespace

int main () {
	// --- One row: freeze, then shatter. ----------------------------------
	{
		Sim sim(base_config(), std::vector<int>{I, I, I, I, I});
		sim.seed(welled(1));
		wait_spawn(sim);
		tap(sim, Key::Cw);
		// The cues are the lock frame's own, so read them before the
		// release step wipes the box.
		sim.step(Event{Key::Hard, true});
		check("the freeze cue fires at the lock", cued(sim, "freeze"));
		sim.step(Event{Key::Hard, false});
		run_frames(sim, 4);
		check("a completed row freezes instead of clearing",
			sim.lines_cleared() == 0, std::to_string(sim.lines_cleared()));
		check("the frozen row still stands",
			sim.board().at(0, kHeight - 1) >= 0);
		check("and is marked iron", sim.board().iron_row(kHeight - 1));
		check("the freezing lock is credited nothing",
			sim.locked().front().scored && sim.locked().front().lines == 0);
		const long long before = sim.score();

		// The next lock - a flat I laid on top, nowhere near completing
		// anything - shatters it.
		wait_spawn(sim);
		tap(sim, Key::Hard);
		run_frames(sim, 4);
		check("the next lock shatters the frozen row",
			sim.lines_cleared() == 1, std::to_string(sim.lines_cleared()));
		check("no iron is left", iron_rows(sim.board()) == 0);
		check("the shattering lock takes the credit",
			sim.locked().back().scored && sim.locked().back().lines == 1);
		check("and the line score lands with it", sim.score() >= before + 400,
			std::to_string(sim.score() - before));
	}

	// --- Two rows freeze together and shatter together. ------------------
	{
		Sim sim(base_config(), std::vector<int>{I, I, I, I});
		sim.seed(welled(2));
		wait_spawn(sim);
		quad_drop(sim);
		run_frames(sim, 4);
		check("two completed rows freeze together",
			iron_rows(sim.board()) == 2 && sim.lines_cleared() == 0,
			std::to_string(iron_rows(sim.board())));
		wait_spawn(sim);
		tap(sim, Key::Hard);
		run_frames(sim, 4);
		check("and shatter together on the next lock",
			sim.lines_cleared() == 2 && iron_rows(sim.board()) == 0,
			std::to_string(sim.lines_cleared()));
	}

	// --- A quiet lock freezes nothing. -----------------------------------
	{
		Sim sim(base_config(), std::vector<int>{I, I});
		wait_spawn(sim);
		sim.step(Event{Key::Hard, true});
		check("a quiet lock fires no freeze cue", !cued(sim, "freeze"));
		sim.step(Event{Key::Hard, false});
		run_frames(sim, 4);
		check("and marks no iron", iron_rows(sim.board()) == 0);
	}

	// --- Shattering re-freezes nothing by itself. ------------------------
	// After a shatter's splices settle the stack two rows down, no row is
	// full any more - so the freeze pass at the end of that same lock must
	// mark nothing, and the game is back to a plain board plus leftovers.
	{
		Sim sim(base_config(), std::vector<int>{I, I, I, I, I});
		sim.seed(welled(2));
		wait_spawn(sim);
		quad_drop(sim);
		run_frames(sim, 4);
		wait_spawn(sim);
		tap(sim, Key::Hard);
		run_frames(sim, 4);
		check("shattering re-freezes nothing by itself",
			iron_rows(sim.board()) == 0, std::to_string(iron_rows(sim.board())));
	}

	// --- With the clear delay on, the beat is the same, just slower. ------
	{
		Sim sim(base_config(true), std::vector<int>{I, I, I, I});
		sim.seed(welled(1));
		wait_spawn(sim);
		quad_drop(sim);
		run_frames(sim, 20);
		check("animated: the row froze and nothing cleared",
			sim.lines_cleared() == 0 && sim.board().iron_row(kHeight - 1));
		wait_spawn(sim);
		tap(sim, Key::Hard);
		run_frames(sim, 20);
		check("animated: the next lock shatters it",
			sim.lines_cleared() == 1, std::to_string(sim.lines_cleared()));
	}

	// --- Off by default. --------------------------------------------------
	{
		SimConfig config = base_config();
		config.cold_iron = false;
		Sim sim(config, std::vector<int>{I, I});
		sim.seed(welled(1));
		wait_spawn(sim);
		quad_drop(sim);
		run_frames(sim, 4);
		check("with cold iron off a clear is a clear",
			sim.lines_cleared() == 1 && iron_rows(sim.board()) == 0);
	}

	std::printf("\n%s\n", failures == 0 ? "All checks passed."
		: "CHECKS FAILED.");
	return failures == 0 ? 0 : 1;
}
