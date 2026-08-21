// The clear delay knob, pinned from both sides: with it on, the reference
// timing the traces grade - seven frames a cleared row before the score
// lands; with it off, the whole clear resolves on the lock frame like a
// quiet lock always has, and nothing about the outcome changes but the
// clock: same score, same counters, same board, one clear cue instead of
// a pile.
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

SimConfig base_config (bool delay) {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.clear_delay = delay;
	return config;
}

void run_frames (Sim& sim, int frames) {
	for (int i = 0; i < frames; ++i) {
		sim.step(std::nullopt);
	}
}

// The first spawn waits out the mode's own entry count; step until the
// piece is actually in play before typing at it.
void wait_spawn (Sim& sim) {
	for (int i = 0; i < 100 && !sim.entry(); ++i) {
		sim.step(std::nullopt);
	}
}

// A board welled at kSpawnX + 1 - where a vertical I stands - filled that
// many rows up from the floor.
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

// Stand the I upright and drop it down the well. Assumes the piece has
// already spawned.
void quad_drop (Sim& sim) {
	sim.step(Event{Key::Cw, true});
	sim.step(Event{Key::Cw, false});
	sim.step(Event{Key::Hard, true});
	sim.step(Event{Key::Hard, false});
}

// Frames from the lock until the placement is scored.
int frames_to_score (Sim& sim) {
	int waited = 0;
	while (!sim.locked().back().scored && waited < 100) {
		sim.step(std::nullopt);
		++waited;
	}
	return waited;
}

int frames_to_entry (Sim& sim) {
	int waited = 0;
	while (!sim.entry() && waited < 100) {
		sim.step(std::nullopt);
		++waited;
	}
	return waited;
}

} // namespace

int main () {
	// The reference timing, asserted directly rather than only through the
	// traces: seven frames a row before the score lands, spawn the frame
	// after. A single, then a quad.
	{
		Sim sim(base_config(true), std::vector<int>{0, 1, 2, 3});
		sim.seed(welled(1));
		wait_spawn(sim);
		quad_drop(sim);
		// Seven frames a row; the hard drop's release step already spent one.
		const int waited = frames_to_score(sim);
		check("a delayed single scores seven frames after the lock",
			waited == 6, std::to_string(waited));
		check("and the next piece spawns the frame after that",
			frames_to_entry(sim) == 1);
	}
	{
		Sim sim(base_config(true), std::vector<int>{0, 1, 2, 3});
		sim.seed(welled(4));
		wait_spawn(sim);
		quad_drop(sim);
		const int waited = frames_to_score(sim);
		check("a delayed quad takes seven frames a row",
			waited == 27, std::to_string(waited));
	}

	// With the delay off, the clear resolves on the lock frame - the score,
	// the cues, everything - and the next piece spawns the frame after.
	{
		Sim sim(base_config(false), std::vector<int>{0, 1, 2, 3});
		sim.seed(welled(4));
		wait_spawn(sim);
		sim.step(Event{Key::Cw, true});
		sim.step(Event{Key::Cw, false});
		sim.step(Event{Key::Hard, true});
		check("an instant quad is scored on its lock frame",
			!sim.locked().empty() && sim.locked().back().scored);
		// One clear cue, not a pile of four playing over each other.
		int clear_cues = 0;
		bool tetris = false;
		for (const std::string& cue : sim.cues()) {
			if (cue == "clear" || cue == "tetris") {
				++clear_cues;
				tetris = tetris || cue == "tetris";
			}
		}
		check("with exactly one clear cue, the loudest",
			clear_cues == 1 && tetris,
			std::to_string(clear_cues));
		sim.step(Event{Key::Hard, false});
		check("and the next piece spawns immediately", sim.entry());
	}

	// Same play, both modes: nothing about the outcome may differ - not the
	// score, not the counters, not the board - only the clock.
	{
		long long score[2];
		int lines[2], b2b[2], attack[2];
		std::vector<std::string> rows[2];
		long frames[2];
		for (int mode = 0; mode < 2; ++mode) {
			Sim sim(base_config(mode == 0),
				std::vector<int>{0, 0, 1, 2, 3, 4, 5, 6});
			sim.seed(welled(8));
			wait_spawn(sim);
			quad_drop(sim);
			// Straight to the next drop the moment it spawns: waiting a
			// fixed stretch instead would let gravity pull the two modes'
			// pieces different distances and split the drop score.
			wait_spawn(sim);
			quad_drop(sim);   // Back to back, well still four deep.
			run_frames(sim, 40);
			const Locked& last = sim.locked().back();
			score[mode] = last.score;
			lines[mode] = sim.lines_cleared();
			b2b[mode] = last.b2b;
			attack[mode] = sim.locked()[0].attack + last.attack;
			rows[mode] = sim.board().rows();
			frames[mode] = sim.frame();
		}
		check("both modes score the same",
			score[0] == score[1],
			std::to_string(score[0]) + " vs " + std::to_string(score[1]));
		check("clear the same lines", lines[0] == lines[1]);
		check("keep the same back to back", b2b[0] == b2b[1]);
		check("send the same attack", attack[0] == attack[1],
			std::to_string(attack[0]) + " vs " + std::to_string(attack[1]));
		check("and leave the same board", rows[0] == rows[1]);
		check("only the clock differs", frames[0] > frames[1],
			std::to_string(frames[0]) + " vs " + std::to_string(frames[1]));
	}

	// The cascade styles fall through the same way: a sticky clear with the
	// delay off resolves in the lock frame and leaves the board the delayed
	// run leaves.
	{
		std::vector<std::string> rows[2];
		bool instant_scored = false;
		for (int mode = 0; mode < 2; ++mode) {
			SimConfig config = base_config(mode == 0);
			config.cleartype = 1;
			Sim sim(config, std::vector<int>{0, 1, 2, 3});
			Board board = welled(1);
			// A block standing on the doomed row, to give the cascade
			// something to settle.
			board.set(0, kHeight - 2, S);
			sim.seed(board);
			wait_spawn(sim);
			quad_drop(sim);
			if (mode == 1) {
				instant_scored = !sim.locked().empty()
					&& sim.locked().back().scored;
			}
			run_frames(sim, 40);
			rows[mode] = sim.board().rows();
		}
		check("an instant sticky clear is scored on its lock frame",
			instant_scored);
		check("and settles to the board the delayed one settles to",
			rows[0] == rows[1]);
	}

	// Versus: with the delay off, a clear cancels the garbage in flight on
	// the lock frame itself, not seven frames a row later.
	{
		SimConfig config = base_config(false);
		config.gametype = 5;
		Sim sim(config, std::vector<int>{0, 1, 2, 3});
		sim.seed(welled(8));
		sim.receive_attack(2);
		wait_spawn(sim);
		check("the attack is pending before the clear",
			sim.pending_garbage() == 2);
		sim.step(Event{Key::Cw, true});
		sim.step(Event{Key::Cw, false});
		sim.step(Event{Key::Hard, true});
		check("an instant clear cancels it on the lock frame",
			sim.pending_garbage() == 0,
			std::to_string(sim.pending_garbage()));
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
