// The replay cross check, C++ side.
//
// Two jobs, driven by tools/test_replay_cross.py:
//
//   replaycheck write <folder> <dump>   play a scripted game through the sim,
//                                       record it, save the replay file, and
//                                       write the canonical dump of what was
//                                       saved. Python then loads the file
//                                       with engine/replay.py and dumps it
//                                       the same way; the dumps must agree.
//
//   replaycheck read <file> <dump>      load a replay - typically one the
//                                       Python engine wrote - and dump it.
//                                       Python dumps its own reading of the
//                                       same file; the dumps must agree.
//
// The canonical dump names every field of every placement, every step of the
// re-enactment with the finesse correction both off and on, and both
// summaries. If the two engines print the same dump for the same file, a
// replay means the same thing wherever it is read.
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/replay.hpp"
#include "forcetris/sim.hpp"

namespace {

using namespace forcetris;

std::string joined (const std::vector<std::string>& parts) {
	if (parts.empty()) {
		return "-";
	}
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i) {
		out += (i > 0 ? "," : "") + parts[i];
	}
	return out;
}

std::string stops_joined (const std::vector<std::array<int, 3>>& stops) {
	std::vector<std::string> parts;
	for (const auto& stop : stops) {
		parts.push_back(std::to_string(stop[0]) + ":" + std::to_string(stop[1])
			+ ":" + std::to_string(stop[2]));
	}
	return joined(parts);
}

std::string number (double value) {
	char text[40];
	std::snprintf(text, sizeof text, "%.9g", value);
	return text;
}

void dump_summary (std::ostream& out, const replay::Summary& sum, bool fixed) {
	out << "summary " << (fixed ? 1 : 0)
	    << " " << sum.placements << " " << sum.judged << " " << sum.faults
	    << " " << sum.wasted << " " << sum.presses << " " << sum.lines
	    << " " << sum.score << " " << sum.spins << " " << sum.perfects
	    << " " << sum.best_b2b << " " << sum.best_combo << " " << sum.attack
	    << " " << number(sum.rate) << " " << number(sum.ppp)
	    << " " << number(sum.pps) << " " << number(sum.apm)
	    << " " << number(sum.vs) << " " << number(sum.seconds);
	std::vector<std::string> clears;
	for (const auto& [size, count] : sum.clears) {
		clears.push_back(std::to_string(size) + ":" + std::to_string(count));
	}
	out << " " << joined(clears) << "\n";
}

void dump (const replay::Replay& game, const std::string& path) {
	std::ofstream out(path);
	const replay::Meta& meta = game.meta;
	out << "meta " << meta.gametype << " " << meta.score << " " << meta.lines
	    << " " << meta.downstack << " " << number(meta.seconds)
	    << " " << meta.das << " " << meta.arr << " " << meta.dcd
	    << " " << meta.sdf << " " << meta.are << " " << meta.finesse
	    << " " << meta.spinrule << " " << meta.cleartype
	    << " " << number(meta.forced_delay) << "\n";
	for (size_t i = 0; i < game.placements.size(); ++i) {
		const replay::Placement& place = game.placements[i];
		std::string spin = place.spin;
		for (char& letter : spin) {
			if (letter == ' ') {
				letter = '_';
			}
		}
		std::vector<std::string> queue;
		for (const int form : place.queue) {
			queue.push_back(std::to_string(form));
		}
		out << "place " << i << " " << place.form << " " << place.state
		    << " " << place.x << " " << place.y
		    << " " << (place.held ? 1 : 0) << " " << (place.forced ? 1 : 0)
		    << " " << (place.judged ? 1 : 0)
		    << " " << (place.best.has_value() ? *place.best : -1)
		    << " " << place.lines << " " << (spin.empty() ? "-" : spin)
		    << " " << (place.perfect ? 1 : 0) << " " << place.combo
		    << " " << place.b2b << " " << place.score << " " << place.attack
		    << " " << place.stored << " " << joined(queue)
		    << " " << number(place.elapsed) << " " << place.wasted() << "\n";
		out << "presses " << i << " " << joined(place.presses) << "\n";
		out << "rows " << i << " " << joined(place.rows) << "\n";
		out << "steps " << i << " " << stops_joined(place.steps(false)) << "\n";
		out << "fixedsteps " << i << " " << stops_joined(place.steps(true)) << "\n";
		out << "shown " << i << " " << joined(place.presses_shown(false)) << "\n";
		out << "fixedshown " << i << " " << joined(place.presses_shown(true)) << "\n";
	}
	dump_summary(out, game.summary(false), false);
	dump_summary(out, game.summary(true), true);
}

// The scripted game the write side plays: twelve rows of chimney and a feed
// of I pieces with one O among them, so the file carries a first hold, a
// swap out of the box, a wasted-press fault, twelve singles of combo and the
// perfect clear that finishes the seed. The last four windows then build the
// bottom row back by hand and finish it with a T kicked off the left wall -
// a mini T-spin single under the all-spin-with-minis rule, so the banner
// labels are in the file too. Deterministic to the frame.
replay::Replay play () {
	SimConfig config;
	config.das_ms = 140;
	config.arr_ms = 40;
	config.sdf = 40;
	config.forced_delay = 0.;
	config.finesse_rule = 1;
	config.spin_rule = 3;
	std::vector<int> pieces(60, 0);
	pieces[3] = O;
	pieces[16] = T;
	Sim sim(config, pieces);
	Board seed;
	for (int y = 10; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (!(3 <= x && x <= 6)) {
				seed.set(x, y, GARBAGE);
			}
		}
	}
	sim.seed(seed);

	std::map<long, Event> events;
	for (int window = 0; window < 12; ++window) {
		const long start = window * 60;
		if (window == 1) {
			// The first hold: the piece in play goes into the empty box.
			events[start + 20] = Event{Key::Hold, true};
		}
		if (window == 2) {
			// The O has arrived: swapping brings the boxed I out, held.
			events[start + 20] = Event{Key::Hold, true};
		}
		if (window == 5) {
			// Two presses that cancel: a finesse fault on the record.
			events[start + 24] = Event{Key::Left, true};
			events[start + 26] = Event{Key::Left, false};
			events[start + 28] = Event{Key::Right, true};
			events[start + 30] = Event{Key::Right, false};
		}
		events[start + 40] = Event{Key::Hard, true};
	}
	// The bottom row rebuilt by hand on the emptied board: an I against the
	// right wall, an I two taps from the left, an upright I down column five.
	events[720 + 20] = Event{Key::Right, true};
	events[720 + 40] = Event{Key::Right, false};
	events[720 + 44] = Event{Key::Hard, true};
	events[780 + 20] = Event{Key::Left, true};
	events[780 + 22] = Event{Key::Left, false};
	events[780 + 24] = Event{Key::Left, true};
	events[780 + 26] = Event{Key::Left, false};
	events[780 + 40] = Event{Key::Hard, true};
	events[840 + 20] = Event{Key::Cw, true};
	events[840 + 40] = Event{Key::Hard, true};
	// And the T: walked to the wall, sunk, kicked into the corner it fills.
	events[900 + 14] = Event{Key::Left, true};
	events[900 + 30] = Event{Key::Left, false};
	events[900 + 34] = Event{Key::Soft, true};
	events[900 + 36] = Event{Key::Soft, false};
	events[900 + 40] = Event{Key::Cw, true};
	events[900 + 44] = Event{Key::Hard, true};

	replay::Recorder recorder;
	replay::Meta meta;
	meta.played = "2026-08-18T12:00:00";
	meta.gametype = "free";
	meta.forced_delay = config.forced_delay;
	meta.finesse = config.finesse_rule;
	meta.spinrule = config.spin_rule;
	meta.cleartype = 0;
	meta.das = config.das_ms;
	meta.arr = config.arr_ms;
	meta.dcd = config.dcd_ms;
	meta.sdf = config.sdf;
	meta.are = config.are_ms;
	recorder.begin(meta);

	size_t taken = 0;
	for (long frame = 0; frame < 1000; ++frame) {
		std::optional<Event> event;
		const auto found = events.find(frame);
		if (found != events.end()) {
			event = found->second;
		}
		const bool alive = sim.step(event);
		const auto& locked = sim.locked();
		while (taken < locked.size() && locked[taken].scored) {
			// The snapshot is the board as the clear left it, which is only
			// known once the placement's score has resolved.
			recorder.add(replay::from_locked(locked[taken], sim.board().rows()));
			++taken;
		}
		if (!alive) {
			break;
		}
	}
	auto finished = recorder.finish(
		sim.score(), sim.lines_cleared(), sim.downstack(), sim.frame() * 0.02);
	if (!finished.has_value()) {
		std::cerr << "the scripted game was too short to record\n";
		std::exit(1);
	}
	return *finished;
}

} // namespace

int main (int argc, char** argv) {
	if (argc != 4) {
		std::cerr << "usage: replaycheck write <folder> <dump>\n"
		          << "       replaycheck read <file> <dump>\n";
		return 2;
	}
	const std::string mode = argv[1];
	if (mode == "write") {
		replay::Replay game = play();
		if (!replay::save(game, argv[2])) {
			std::cerr << "could not save into " << argv[2] << "\n";
			return 1;
		}
		dump(game, argv[3]);
		std::cout << game.path << "\n";
		return 0;
	}
	if (mode == "read") {
		const auto game = replay::load(argv[2]);
		if (!game.has_value()) {
			std::cerr << "could not read " << argv[2] << "\n";
			return 1;
		}
		dump(*game, argv[3]);
		return 0;
	}
	std::cerr << "unknown mode " << mode << "\n";
	return 2;
}
