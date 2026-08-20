// The versus wire, pinned by driving the sim directly - like the cheese
// modes it has no Python counterpart, so the rules are spelled out here:
// attack queued at a board rises only on a lock that cleared nothing, eight
// rows at a time; a clear cancels the queue before anything goes out; and a
// back to back chain four deep banks a surge that fires when the chain
// breaks.
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

SimConfig versus_config () {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.gametype = 5;
	return config;
}

void run_frames (Sim& sim, int frames) {
	for (int i = 0; i < frames; ++i) {
		sim.step(std::nullopt);
	}
}

// Step until the newest lock has resolved its score, or the guard runs out.
bool run_to_resolution (Sim& sim, size_t lock) {
	for (int guard = 0; guard < 400; ++guard) {
		if (sim.locked().size() > lock && sim.locked()[lock].scored) {
			return true;
		}
		sim.step(std::nullopt);
	}
	return false;
}

// A board whose bottom `depth` rows are full except one column: the well the
// quads run down.
Board welled (int depth, int well) {
	Board board;
	for (int y = kHeight - depth; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (x != well) {
				board.set(x, y, S);
			}
		}
	}
	return board;
}

// Drop the current piece flat where it stands, and run to its resolution.
void plain_drop (Sim& sim) {
	const size_t lock = sim.locked().size();
	sim.step(Event{Key::Hard, true});
	run_to_resolution(sim, lock);
}

// Turn the I upright and send it down the well, running to resolution.
void quad_drop (Sim& sim) {
	const size_t lock = sim.locked().size();
	sim.step(Event{Key::Cw, true});
	sim.step(Event{Key::Cw, false});
	sim.step(Event{Key::Hard, true});
	run_to_resolution(sim, lock);
}

} // namespace

int main () {
	const int well = kSpawnX + 1;   // Where an upright spawn I stands.

	// Queued attack rises on a lock that cleared nothing - all of it up to
	// eight rows, each with the hole the dealer rolled, in dealt order.
	{
		Sim sim(versus_config(), std::vector<int>(10, 0));
		for (int hole = 0; hole < 10; ++hole) {
			sim.feed_garbage(1 << hole);
		}
		sim.receive_attack(3);
		run_frames(sim, 25);
		check("nothing rises while the piece is still in play",
			sim.board().garbage_rows() == 0);
		plain_drop(sim);
		check("a no-clear lock lets the queue rise",
			sim.board().garbage_rows() == 3,
			std::to_string(sim.board().garbage_rows()));
		check("and drains it", sim.pending_garbage() == 0);
		check("the first dealt hole rose first",
			sim.board().at(0, kHeight - 3) < 0
			&& sim.board().at(2, kHeight - 1) < 0);
	}

	// More than eight queued rows rise eight at a time.
	{
		Sim sim(versus_config(), std::vector<int>(10, 0));
		for (int i = 0; i < 20; ++i) {
			sim.feed_garbage(1 << (i % 10));
		}
		sim.receive_attack(12);
		run_frames(sim, 25);
		plain_drop(sim);
		check("eight rows is the lift's limit",
			sim.board().garbage_rows() == 8,
			std::to_string(sim.board().garbage_rows()));
		check("the rest stays queued", sim.pending_garbage() == 4);
		run_frames(sim, 25);
		plain_drop(sim);
		check("and rises on the next quiet lock",
			sim.board().garbage_rows() == 12,
			std::to_string(sim.board().garbage_rows()));
	}

	// A clear cancels the queue before anything goes out, and garbage never
	// rises on a clearing lock.
	{
		Sim sim(versus_config(), std::vector<int>(10, 0));
		sim.seed(welled(4, well));
		for (int i = 0; i < 10; ++i) {
			sim.feed_garbage(1 << (i % 10));
		}
		sim.receive_attack(2);
		run_frames(sim, 25);
		quad_drop(sim);
		check("the quad cleared", sim.locked().back().lines == 4);
		check("its attack ate the queue first", sim.pending_garbage() == 0);
		check("the rest went out",
			sim.take_outgoing() == sim.locked().back().attack - 2,
			std::to_string(sim.locked().back().attack));
		check("no garbage rose through the clear",
			sim.board().garbage_rows() == 0);
		check("take_outgoing drains", sim.take_outgoing() == 0);
	}

	// A queue bigger than the clear survives it, smaller by the attack, and
	// rises on the next quiet lock.
	{
		Sim sim(versus_config(), std::vector<int>(10, 0));
		// Eight rows deep so the quad leaves rubble standing: a perfect
		// clear's bonus would out-eat the queue and hide the arithmetic.
		sim.seed(welled(8, well));
		for (int i = 0; i < 10; ++i) {
			sim.feed_garbage(1 << (i % 10));
		}
		sim.receive_attack(6);
		run_frames(sim, 25);
		quad_drop(sim);
		const int eaten = sim.locked().back().attack;
		check("the clear ate what it sent",
			sim.pending_garbage() == 6 - eaten,
			std::to_string(sim.pending_garbage()));
		check("and sent nothing past the queue", sim.take_outgoing() == 0);
		run_frames(sim, 25);
		plain_drop(sim);
		check("the leftover rose on the next quiet lock",
			sim.board().garbage_rows() == 6 - eaten,
			std::to_string(sim.board().garbage_rows()));
	}

	// An empty dealer leaves the queue pending rather than losing it.
	{
		Sim sim(versus_config(), std::vector<int>(10, 0));
		sim.receive_attack(3);
		run_frames(sim, 25);
		plain_drop(sim);
		check("no dealt holes, nothing rises",
			sim.board().garbage_rows() == 0);
		check("but nothing is lost", sim.pending_garbage() == 3);
		for (int hole = 0; hole < 5; ++hole) {
			sim.feed_garbage(1 << hole);
		}
		run_frames(sim, 25);
		plain_drop(sim);
		check("it rises once holes are dealt",
			sim.board().garbage_rows() == 3);
	}

	// The surge: a back to back chain four deep banks a row per link, and
	// the clear that breaks the chain fires the bank on top of its own
	// attack.
	{
		Sim sim(versus_config(), std::vector<int>(30, 0));
		sim.seed(welled(20, well));
		int charges[5] = {};
		for (int quad = 0; quad < 5; ++quad) {
			run_frames(sim, 30);
			quad_drop(sim);
			charges[quad] = sim.surge_charge();
			sim.take_outgoing();
		}
		check("no charge before the chain is four deep",
			charges[0] == 0 && charges[1] == 0 && charges[2] == 0,
			std::to_string(charges[2]));
		check("the fourth and fifth links bank a row each",
			charges[3] == 1 && charges[4] == 2,
			std::to_string(charges[3]) + "," + std::to_string(charges[4]));
		// Break the chain with a plain single, built from what the queue
		// holds: two flat I pieces against the walls lay row 21 at columns
		// 0-3 and 6-9, one upright I fills column 5, and a second upright I
		// tapped left fills column 4 - completing row 21 alone.
		const size_t before = sim.locked().size();
		run_frames(sim, 5);
		// Flat I against each wall: over-tapping is safe, a tap into the
		// wall simply does not move.
		for (int tap = 0; tap < 5; ++tap) {
			sim.step(Event{Key::Left, true});
			sim.step(Event{Key::Left, false});
		}
		sim.step(Event{Key::Hard, true});
		run_to_resolution(sim, before);
		for (int tap = 0; tap < 5; ++tap) {
			sim.step(Event{Key::Right, true});
			sim.step(Event{Key::Right, false});
		}
		sim.step(Event{Key::Hard, true});
		run_to_resolution(sim, before + 1);
		run_frames(sim, 5);
		check("the bank waits while the row builds", sim.surge_charge() == 2);
		const size_t third = sim.locked().size();
		sim.step(Event{Key::Cw, true});
		sim.step(Event{Key::Cw, false});
		sim.step(Event{Key::Hard, true});
		run_to_resolution(sim, third);
		run_frames(sim, 5);
		check("still no fire without a clear", sim.surge_charge() == 2);
		sim.take_outgoing();
		const size_t fourth = sim.locked().size();
		sim.step(Event{Key::Cw, true});
		sim.step(Event{Key::Cw, false});
		sim.step(Event{Key::Left, true});
		sim.step(Event{Key::Left, false});
		sim.step(Event{Key::Hard, true});
		run_to_resolution(sim, fourth);
		check("the breaking clear cleared", sim.locked().back().lines >= 1);
		check("and fired the bank on top of its own attack",
			sim.take_outgoing() == sim.locked().back().attack + 2,
			std::to_string(sim.locked().back().attack));
		check("the bank is spent", sim.surge_charge() == 0);
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
