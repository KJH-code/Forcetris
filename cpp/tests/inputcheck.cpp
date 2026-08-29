// The burst step, pinned against the one-per-frame drain it replaces: a
// frame carrying several events walks the piece through exactly the states
// the spread-out frames would have, so the placement is the same and only
// the wait is gone. And the session actually drains - a queue of presses
// is empty after one step, not six.
#include <cstdio>
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

SimConfig base_config () {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	return config;
}

void wait_spawn (Sim& sim) {
	for (int i = 0; i < 100 && !sim.entry(); ++i) {
		sim.step(std::nullopt);
	}
}

// The same events played both ways: spread one per frame, and all in one
// burst frame. Returns the final boards for comparison, after settling
// whatever clear the drop started.
std::pair<std::vector<std::string>, std::vector<std::string>> both_ways (
		const std::vector<Event>& events) {
	std::vector<std::vector<std::string>> boards;
	for (int way = 0; way < 2; ++way) {
		Sim sim(base_config(), std::vector<int>{T, I, O, S, Z, J, L, T});
		wait_spawn(sim);
		if (way == 0) {
			for (const Event& event : events) {
				sim.step(event);
			}
		} else {
			sim.step(events);
		}
		for (int i = 0; i < 40; ++i) {
			sim.step(std::nullopt);
		}
		boards.push_back(sim.board().rows());
	}
	return {boards[0], boards[1]};
}

std::string join (const std::vector<std::string>& rows) {
	std::string out;
	for (const std::string& row : rows) {
		out += row + "\n";
	}
	return out;
}

} // namespace

int main () {
	// A same-frame tap - down and up in one burst - moves exactly one cell,
	// like a tap spread over two frames does.
	{
		const std::vector<Event> tap
			= {{Key::Left, true}, {Key::Left, false}};
		Sim sim(base_config(), std::vector<int>{T, I, O, S, Z, J, L, T});
		wait_spawn(sim);
		const int before = sim.piece().x;
		sim.step(tap);
		check("a burst tap moves one cell", sim.piece().x == before - 1,
			std::to_string(sim.piece().x));
		const auto [spread, burst] = both_ways(tap);
		check("and lands where the spread tap lands", spread == burst);
	}

	// A whole placement in one frame: tap left, rotate, hard drop. The
	// board ends identical to the same presses one per frame.
	{
		const auto [spread, burst] = both_ways({
			{Key::Left, true}, {Key::Left, false},
			{Key::Cw, true}, {Key::Cw, false},
			{Key::Hard, true}, {Key::Hard, false}});
		check("tap-rotate-drop lands the same either way", spread == burst,
			join(burst));
		bool placed = false;
		for (const std::string& row : burst) {
			placed = placed || row.find_first_not_of('.') != std::string::npos;
		}
		check("and the piece really locked", placed);
	}

	// Opposite taps in one burst cancel the way they do across frames:
	// left commits before right reads the piece.
	{
		const auto [spread, burst] = both_ways({
			{Key::Left, true}, {Key::Left, false},
			{Key::Right, true}, {Key::Right, false},
			{Key::Hard, true}, {Key::Hard, false}});
		check("opposite taps land the same either way", spread == burst,
			join(burst));
	}

	// A wall run: more taps than the distance allows still stops at the
	// wall, both ways.
	{
		std::vector<Event> hammer;
		for (int i = 0; i < 8; ++i) {
			hammer.push_back({Key::Left, true});
			hammer.push_back({Key::Left, false});
		}
		hammer.push_back({Key::Hard, true});
		const auto [spread, burst] = both_ways(hammer);
		check("hammering into the wall lands the same either way",
			spread == burst, join(burst));
	}

	// The session drains its whole queue in one frame: six queued events,
	// one step, and the piece shows all of them.
	{
		gui::Session session(base_config(), 7, replay::Meta{});
		for (int i = 0; i < 100 && !session.sim().entry(); ++i) {
			session.step();
		}
		session.key(Key::Left, true);
		session.key(Key::Left, false);
		session.key(Key::Cw, true);
		session.key(Key::Cw, false);
		session.key(Key::Hard, true);
		session.key(Key::Hard, false);
		const size_t locks = session.sim().locked().size();
		session.step();
		check("a queued burst lands in a single session step",
			session.sim().locked().size() == locks + 1,
			std::to_string(session.sim().locked().size()));
	}

	// --- The chaos cards that curse the hands. ------------------------------
	// Crossed Wires trades the two directions and the two rotations, and
	// nothing else - a hard drop under the curse is still a hard drop.
	{
		check("crossed wires trade the pairs",
			gui::crossed(Key::Left) == Key::Right
				&& gui::crossed(Key::Right) == Key::Left
				&& gui::crossed(Key::Ccw) == Key::Cw
				&& gui::crossed(Key::Cw) == Key::Ccw);
		check("and leave the rest of the hand alone",
			gui::crossed(Key::Hard) == Key::Hard
				&& gui::crossed(Key::Soft) == Key::Soft
				&& gui::crossed(Key::Hold) == Key::Hold
				&& gui::crossed(Key::Flip) == Key::Flip);
		// The swap has to be its own inverse, or the release of a held key
		// would arrive as a different key and stick it down forever.
		bool paired = true;
		for (const Key key : {Key::Left, Key::Right, Key::Soft, Key::Hard,
				Key::Hold, Key::Ccw, Key::Cw, Key::Flip}) {
			paired = paired && gui::crossed(gui::crossed(key)) == key;
		}
		check("crossing twice is not crossing at all", paired);
	}
	// The Loose Ratchet counts turns and overshoots every third one. Only
	// turns: walking the piece across the board must never trip it, or the
	// count would depend on how much the player shuffled.
	{
		int turns = 0;
		std::string pattern;
		for (int i = 0; i < 9; ++i) {
			pattern += gui::overshoots(turns, Key::Cw) ? "!" : ".";
		}
		check("every third turn goes one too far", pattern == "..!..!..!",
			pattern);
		int quiet = 0;
		bool tripped = false;
		for (const Key key : {Key::Left, Key::Right, Key::Soft, Key::Hard,
				Key::Hold, Key::Flip}) {
			for (int i = 0; i < 9; ++i) {
				tripped = tripped || gui::overshoots(quiet, key);
			}
		}
		check("and nothing but a turn is counted at all",
			!tripped && quiet == 0);
		// The two rotations share one ratchet: it is the hand that is
		// loose, not one key.
		int mixed = 0;
		const bool third = gui::overshoots(mixed, Key::Cw)
			|| gui::overshoots(mixed, Key::Ccw)
			|| gui::overshoots(mixed, Key::Cw);
		check("both rotations wind the same ratchet", third && mixed == 3);
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
