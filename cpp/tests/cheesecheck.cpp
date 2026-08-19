// The cheese modes, pinned by driving the sim directly - these are the C++
// side's own modes, with no Python engine to grade them against, so their
// behaviour is spelled out here instead: the race primes its stack from the
// dealt holes and caps it, the quota drains as rows rise, the last dug row
// ends the game won rather than lost, and survival's floor rises on its
// clock with the piece riding the push.
#include <cstdio>
#include <optional>
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

SimConfig cheese_config (int gametype) {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.gametype = gametype;
	return config;
}

void run_frames (Sim& sim, int frames) {
	for (int i = 0; i < frames; ++i) {
		sim.step(std::nullopt);
	}
}

} // namespace

int main () {
	// The race primes the board from the dealt holes - up to nine rows on
	// the board at once, the rest of the quota held back until digging makes
	// room - and the holes come up in the order they were dealt.
	{
		SimConfig config = cheese_config(3);
		config.cheese_total = 15;
		Sim sim(config, std::vector<int>(30, 0));
		for (int hole = 0; hole < 15; ++hole) {
			sim.feed_garbage(hole % 10);
		}
		run_frames(sim, 15);
		check("the race primes nine rows and no more",
			sim.board().garbage_rows() == 9,
			std::to_string(sim.board().garbage_rows()));
		check("and holds the rest of the quota back",
			sim.cheese_left() == 6, std::to_string(sim.cheese_left()));
		check("the first dealt hole sits at the top of the stack",
			sim.board().at(0, kHeight - 9) < 0
			&& sim.board().at(1, kHeight - 9) == GARBAGE);
		check("and the ninth at the floor",
			sim.board().at(8, kHeight - 1) < 0
			&& sim.board().at(0, kHeight - 1) == GARBAGE);
		check("nothing has been won or lost yet",
			!sim.won() && !sim.board().collides(sim.piece()));
	}

	// A one-row race, dug with a single vertical I dropped into the hole:
	// the clear resolves, the garbage is gone, and the game ends won - not
	// lost - with the downstack counted and the clock stopped.
	{
		SimConfig config = cheese_config(3);
		config.cheese_total = 1;
		Sim sim(config, std::vector<int>(10, 0));
		// A vertical I at spawn stands in column kSpawnX + 1; the hole is
		// dealt to meet it so the drop needs no walking.
		const int hole = kSpawnX + 1;
		sim.feed_garbage(hole);
		run_frames(sim, 25);
		check("one row of cheese stands", sim.board().garbage_rows() == 1);
		check("the quota is spent", sim.cheese_left() == 0);
		check("a piece is in play", sim.entry());
		sim.step(Event{Key::Cw, true});
		bool upright = false;
		for (const Offset cell : cells_of(sim.piece())) {
			upright = upright || cell.x == hole;
		}
		check("the turned I stands over the hole", upright);
		sim.step(Event{Key::Cw, false});
		sim.step(Event{Key::Hard, true});
		run_frames(sim, 40);
		check("digging the last row wins the race", sim.won());
		check("winning is not losing", sim.board().garbage_rows() == 0);
		check("the dig counted as downstack", sim.downstack() == 1);
		check("the win stops the sim", !sim.step(std::nullopt));
		const long ended = sim.frame();
		sim.step(std::nullopt);
		check("and the clock with it", sim.frame() == ended);
	}

	// Survival: the floor rises exactly on its clock, the risen rows carry
	// their holes, and the game only ends when the stack finally wins.
	{
		SimConfig config = cheese_config(4);
		config.cheese_period = 50;
		Sim sim(config, std::vector<int>(60, 0));
		for (int hole = 0; hole < 40; ++hole) {
			sim.feed_garbage(hole % 10);
		}
		run_frames(sim, 45);
		check("no cheese before the first tick",
			sim.board().garbage_rows() == 0);
		run_frames(sim, 10);
		check("one row on the first tick", sim.board().garbage_rows() == 1,
			std::to_string(sim.board().garbage_rows()));
		run_frames(sim, 200);
		check("four more over the next four ticks",
			sim.board().garbage_rows() == 5,
			std::to_string(sim.board().garbage_rows()));
		check("the game is still on", !sim.won());
		// Left alone long enough, the stack reaches the ceiling and the
		// next spawn has nowhere to stand: survival ends in a loss, never
		// in a win.
		int guard = 0;
		while (sim.step(std::nullopt) && ++guard < 6000) {
		}
		check("the rising floor ends it eventually", guard < 6000);
		check("as a loss", !sim.won());
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
