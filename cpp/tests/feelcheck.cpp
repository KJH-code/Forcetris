// How long the player waits, counted rather than felt.
//
// "Sluggish" is not a bug report anyone can act on, so this turns it into
// frames: the stretch after a hard drop with no piece to control, how long a
// held soft drop takes to reach the floor, and how long a held direction takes
// to reach the wall. Every one of them is a whole number of the engine's 20ms
// frames.
//
// What ships is TETR.IO's own default handling - 167/33/17/6 - because a
// beginner's first minutes should feel like the game everyone learns on, and
// lowering the numbers is what improving looks like. That choice is pinned
// here digit for digit, along with the two things that must never come back
// whatever the handling says: the clear-animation freeze, and a spawn delay.
// The stacker's Instant set and the trainer's old numbers are pinned too, so
// the buttons keep meaning what they say.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "forcetris/sim.hpp"

#include "../gui/config.hpp"

using namespace forcetris;
using forcetris::gui::Config;
using forcetris::gui::Handling;

namespace {

int failures = 0;

void check (const char* name, bool ok, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		!ok && !detail.empty() ? " -- " : "", !ok ? detail.c_str() : "");
	if (!ok) {
		++failures;
	}
}

std::string number (int value) { return std::to_string(value); }

std::vector<int> bag (int count) {
	std::vector<int> forms;
	for (int i = 0; i < count; ++i) {
		forms.push_back(i % 7);
	}
	return forms;
}

// A board with `rows` full rows at the floor, each missing the rightmost
// column, so one standing I fills all of them at once.
Board well (int rows) {
	Board board;
	for (int r = 0; r < rows; ++r) {
		const int y = kHeight - 1 - r;
		for (int x = 0; x < kWidth - 1; ++x) {
			board.set(x, y, GARBAGE);
		}
	}
	return board;
}

void idle (Sim& sim, int frames) {
	for (int i = 0; i < frames; ++i) {
		sim.step(std::optional<Event>{});
	}
}

void press (Sim& sim, Key key) {
	sim.step(std::optional<Event>(Event{key, true}));
}

// Frames from a hard drop that clears `rows` rows to the frame on which a
// piece can be moved again. The forced drop and the finesse retry are off:
// neither is what is being measured, and a retry would confuse the count.
int freeze (const Config& config, int rows) {
	SimConfig sim_config = config.sim();
	sim_config.forced_delay = 0.;
	sim_config.fuse = false;
	sim_config.finesse_rule = 0;
	std::vector<int> forms{I};
	for (const int form : bag(40)) {
		forms.push_back(form);
	}
	Sim sim(sim_config, forms);
	if (rows > 0) {
		sim.seed(well(rows));
	}
	while (!sim.entry()) {
		idle(sim, 1);
	}
	// Stand the I up and walk it into the well at the wall.
	press(sim, Key::Cw);
	sim.step(std::optional<Event>(Event{Key::Cw, false}));
	press(sim, Key::Right);
	idle(sim, 60);
	sim.step(std::optional<Event>(Event{Key::Right, false}));
	press(sim, Key::Hard);
	int dead = 0;
	while ((!sim.entry() || sim.clearing()) && dead < 2000) {
		idle(sim, 1);
		++dead;
	}
	return dead;
}

// Frames a held soft drop takes to carry a piece from spawn to the floor.
int softdrop (const Config& config) {
	SimConfig sim_config = config.sim();
	sim_config.forced_delay = 0.;
	sim_config.fuse = false;
	Sim sim(sim_config, bag(20));
	while (!sim.entry()) {
		idle(sim, 1);
	}
	press(sim, Key::Soft);
	int frames = 1;
	while (sim.entry() && frames < 4000) {
		Piece below = sim.piece();
		below.y += 1;
		if (sim.board().collides(below)) {
			break;
		}
		idle(sim, 1);
		++frames;
	}
	return frames;
}

// Frames a held direction takes to carry a piece from spawn to the wall.
int traverse (const Config& config) {
	SimConfig sim_config = config.sim();
	sim_config.forced_delay = 0.;
	sim_config.fuse = false;
	Sim sim(sim_config, bag(20));
	while (!sim.entry()) {
		idle(sim, 1);
	}
	press(sim, Key::Left);
	int frames = 1;
	int last = sim.piece().x;
	int still = 0;
	while (frames < 4000 && still <= 20) {
		idle(sim, 1);
		++frames;
		if (sim.piece().x == last) {
			++still;
		} else {
			last = sim.piece().x;
			still = 0;
		}
	}
	return frames - still;
}

} // namespace

int main () {
	// --- What the game ships with. ------------------------------------------
	{
		Config config;
		check("the shipped handling is TETR.IO's defaults, digit for digit",
			config.das == 167 && config.arr == 33 && config.dcd == 17
				&& config.sdf == 6 && config.are == 0 && !config.clear_delay);
		check("the shipped handling has no clear freeze",
			freeze(config, 4) == 1, number(freeze(config, 4)) + " frames");
		// A held direction: the press moves a column and arms eight frames
		// of DAS, then two frames of ARR per column - on the 20ms grid
		// that is 167 -> 8 and 33 -> 2, so three columns land on frame 11.
		check("a held direction repeats on TETR.IO's cadence",
			traverse(config) == 11, number(traverse(config)) + " frames");
		// SDF 6 against the fuse modes' gravity is five frames a row: a
		// deliberate, visible soft drop, not the stacker's teleport.
		check("the shipped soft drop walks the well at SDF 6",
			softdrop(config) == 100, number(softdrop(config)) + " frames");
		check("a quiet lock costs one frame either way",
			freeze(config, 0) == 1, number(freeze(config, 0)) + " frames");
	}

	// --- The three sets, and what each of them costs. ------------------------
	{
		Config standard;
		apply_handling(standard, Handling::Standard);
		Config instant;
		apply_handling(instant, Handling::Instant);
		Config trainer;
		apply_handling(trainer, Handling::Trainer);

		check("Standard is what the game ships with",
			standard.das == Config{}.das && standard.arr == Config{}.arr
				&& standard.dcd == Config{}.dcd && standard.sdf == Config{}.sdf
				&& standard.are == Config{}.are
				&& standard.clear_delay == Config{}.clear_delay);

		// The stacker's set, one click away and kept exact.
		check("Instant keeps the stacker's numbers",
			instant.das == 100 && instant.arr == 0 && instant.sdf == 40
				&& !instant.clear_delay);
		// One auto-shift frame covers the whole distance at ARR 0, so the wall
		// arrives the moment DAS expires: the press itself moves a column and
		// arms five frames of DAS, and the jump lands on the last of them.
		check("Instant reaches the wall the frame DAS expires",
			traverse(instant) == 6, number(traverse(instant)) + " frames");
		check("Instant drops instantly and never freezes",
			softdrop(instant) == 1 && freeze(instant, 4) == 1);

		// The trainer's numbers, kept whole - clear-animation freeze and all.
		check("Trainer restores the trainer's numbers",
			trainer.das == 140 && trainer.arr == 40 && trainer.sdf == 6
				&& trainer.clear_delay);
		check("Trainer freezes the board for most of a second on a quad",
			freeze(trainer, 4) == 29, number(freeze(trainer, 4)) + " frames");
		check("Trainer's soft drop takes two seconds from spawn to floor",
			softdrop(trainer) == 100,
			number(softdrop(trainer)) + " frames");
	}

	// --- The freeze scales with the clear, and only with the delay on. ------
	{
		Config on;
		apply_handling(on, Handling::Trainer);
		Config off;
		apply_handling(off, Handling::Instant);
		// Six frames of sprite plus a resume per clearing pass, and a naive
		// quad is four passes: 8, 15, 22, 29.
		const int want[4] = {8, 15, 22, 29};
		bool graded = true;
		std::string seen;
		for (int rows = 1; rows <= 4; ++rows) {
			const int held = freeze(on, rows);
			seen += number(held) + " ";
			graded = graded && held == want[rows - 1];
			graded = graded && freeze(off, rows) == 1;
		}
		check("the animated clear costs seven frames a pass, the instant one none",
			graded, seen);
	}

	std::printf("%s\n", failures == 0 ? "all feel checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
