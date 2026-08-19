// Replay the Python engine's scripted games through the C++ sim and compare
// everything the piece visibly did.
//
// Each trace carries the piece feed, the frame-stamped inputs, and what the
// Python engine made of them: every position the piece stood in, every lock,
// the loss if there was one, and the final board. The sim has to reproduce all
// of it - a divergence of one frame anywhere shifts everything after it, so
// agreement here means the loop's timing is right, not just its outcomes.
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "forcetris/sim.hpp"

namespace {

using namespace forcetris;

struct Snap {
	long frame;
	int form, state, x, y, entry;

	friend bool operator== (const Snap& a, const Snap& b) {
		return a.frame == b.frame && a.form == b.form && a.state == b.state
			&& a.x == b.x && a.y == b.y && a.entry == b.entry;
	}
};

std::string show (const Snap& snap) {
	std::ostringstream out;
	out << "frame " << snap.frame << ": form " << snap.form << " state "
	    << snap.state << " at " << snap.x << "," << snap.y
	    << (snap.entry ? "" : " (no piece)");
	return out.str();
}

std::optional<Key> key_named (const std::string& name) {
	if (name == "left") return Key::Left;
	if (name == "right") return Key::Right;
	if (name == "soft") return Key::Soft;
	if (name == "hard") return Key::Hard;
	if (name == "hold") return Key::Hold;
	if (name == "ccw") return Key::Ccw;
	if (name == "cw") return Key::Cw;
	if (name == "flip") return Key::Flip;
	return std::nullopt;
}

// What the Python engine scored a placement as, once its clear had resolved.
struct Score {
	size_t lock = 0;
	int spin = 0;
	int b2b = 0;
	int combo = 0;
	int perfect = 0;
	int attack = 0;
	long long score = 0;
	int downstack = 0;
};

// What the recorder was told about a placement's journey.
struct Place {
	size_t lock = 0;
	int held = 0;
	int stored = 7;
	std::vector<int> queue;
	int judged = 0;
	int best = -1;
	std::vector<std::string> presses;
	std::vector<std::array<int, 3>> trail;
};

// Split a comma-joined field, with '-' standing for none at all.
std::vector<std::string> split_list (const std::string& joined) {
	std::vector<std::string> parts;
	if (joined == "-") {
		return parts;
	}
	std::string part;
	std::istringstream in(joined);
	while (std::getline(in, part, ',')) {
		parts.push_back(part);
	}
	return parts;
}

struct Trace {
	std::string name;
	SimConfig config;
	std::vector<std::string> seed;
	std::vector<int> pieces;
	std::vector<int> holes;
	std::map<long, Event> events;
	std::vector<Snap> expected;
	std::vector<Locked> locks;
	std::vector<Score> scores;
	std::vector<Place> places;
	std::vector<std::pair<long, std::string>> cues;
	long loss = -1;
	long frames = 0;
	std::vector<std::string> board;
};

// One trace run and compared. Returns the number of disagreements printed.
int grade (const Trace& trace) {
	Sim sim(trace.config, trace.pieces);
	if (!trace.seed.empty()) {
		sim.seed(Board::from_rows(trace.seed));
	}
	for (const int hole : trace.holes) {
		sim.feed_garbage(hole);
	}
	std::vector<Snap> seen;
	std::vector<std::pair<long, std::string>> heard;
	std::optional<Snap> last;
	long ran = 0;
	long lost_at = -1;
	for (long frame = 0; frame < trace.frames + 8; ++frame) {
		std::optional<Event> event;
		const auto found = trace.events.find(frame);
		if (found != trace.events.end()) {
			event = found->second;
		}
		const bool alive = sim.step(event);
		for (const std::string& name : sim.cues()) {
			heard.emplace_back(frame, name);
		}
		ran = frame + 1;
		const Piece& piece = sim.piece();
		const Snap snap{
			frame, piece.form, piece.state, piece.x, piece.y, sim.entry() ? 1 : 0};
		if (!last.has_value() || !(snap.form == last->form && snap.state == last->state
			&& snap.x == last->x && snap.y == last->y && snap.entry == last->entry)) {
			seen.push_back(snap);
			last = snap;
		}
		if (!alive) {
			lost_at = frame;
			break;
		}
		if (ran >= trace.frames && trace.loss < 0) {
			break;
		}
	}

	int bad = 0;
	const auto complain = [&trace, &bad] (const std::string& what) {
		if (bad < 6) {
			std::cout << "       [" << trace.name << "] " << what << "\n";
		}
		++bad;
	};

	// The trajectory: every change the piece went through, in order.
	const size_t common = std::min(seen.size(), trace.expected.size());
	for (size_t i = 0; i < common; ++i) {
		if (!(seen[i] == trace.expected[i])) {
			complain("diverges at change " + std::to_string(i) + ": got "
				+ show(seen[i]) + " against " + show(trace.expected[i]));
			break;
		}
	}
	if (bad == 0 && seen.size() != trace.expected.size()) {
		complain("trajectory has " + std::to_string(seen.size())
			+ " changes against " + std::to_string(trace.expected.size()));
	}

	// The locks.
	const auto& locked = sim.locked();
	if (locked.size() != trace.locks.size()) {
		complain("locked " + std::to_string(locked.size()) + " pieces against "
			+ std::to_string(trace.locks.size()));
	} else {
		for (size_t i = 0; i < locked.size(); ++i) {
			const Locked& got = locked[i];
			const Locked& want = trace.locks[i];
			if (got.frame != want.frame || got.form != want.form
				|| got.state != want.state || got.x != want.x || got.y != want.y
				|| got.forced != want.forced || got.rotated != want.rotated
				|| got.twist != want.twist) {
				const auto flags = [] (const Locked& lock) {
					return std::string(lock.forced ? " forced" : "")
						+ (lock.rotated ? " rotated" : "") + (lock.twist ? " twist" : "");
				};
				complain("lock " + std::to_string(i) + ": got form "
					+ std::to_string(got.form) + " state " + std::to_string(got.state)
					+ " at " + std::to_string(got.x) + "," + std::to_string(got.y)
					+ " frame " + std::to_string(got.frame) + flags(got)
					+ " against form " + std::to_string(want.form) + " state "
					+ std::to_string(want.state) + " at " + std::to_string(want.x) + ","
					+ std::to_string(want.y) + " frame " + std::to_string(want.frame)
					+ flags(want));
				break;
			}
		}
	}

	// The scores: what each resolved placement counted for. The Python side
	// wrote one per placement whose clear finished inside the trace, so a lock
	// named here must be scored - and score the same.
	for (const Score& want : trace.scores) {
		if (want.lock >= locked.size()) {
			complain("score names lock " + std::to_string(want.lock)
				+ " but only " + std::to_string(locked.size()) + " locked");
			break;
		}
		const Locked& got = locked[want.lock];
		if (!got.scored) {
			complain("lock " + std::to_string(want.lock)
				+ " never resolved a score");
			break;
		}
		if (got.spin != want.spin || got.b2b != want.b2b || got.combo != want.combo
			|| (got.perfect ? 1 : 0) != want.perfect || got.attack != want.attack
			|| got.score != want.score || got.downstack != want.downstack) {
			complain("score " + std::to_string(want.lock) + ": got spin "
				+ std::to_string(got.spin) + " b2b " + std::to_string(got.b2b)
				+ " combo " + std::to_string(got.combo)
				+ " perfect " + std::to_string(got.perfect ? 1 : 0)
				+ " attack " + std::to_string(got.attack)
				+ " score " + std::to_string(got.score)
				+ " downstack " + std::to_string(got.downstack)
				+ " against spin " + std::to_string(want.spin)
				+ " b2b " + std::to_string(want.b2b)
				+ " combo " + std::to_string(want.combo)
				+ " perfect " + std::to_string(want.perfect)
				+ " attack " + std::to_string(want.attack)
				+ " score " + std::to_string(want.score)
				+ " downstack " + std::to_string(want.downstack));
			break;
		}
	}

	// The recorder's view of each placement: the journey and the judgement.
	for (const Place& want : trace.places) {
		if (want.lock >= locked.size()) {
			complain("place names lock " + std::to_string(want.lock)
				+ " but only " + std::to_string(locked.size()) + " locked");
			break;
		}
		const Locked& got = locked[want.lock];
		std::vector<int> queue;
		for (const int form : got.queue3) {
			if (form >= 0) {
				queue.push_back(form);
			}
		}
		// The engine writes 7 - the garbage form - for an empty hold box; the
		// sim spells the same thing -1.
		const int stored = got.stored < 0 ? 7 : got.stored;
		const bool judged = got.best >= 0;
		if ((got.held ? 1 : 0) != want.held || stored != want.stored
			|| queue != want.queue || (judged ? 1 : 0) != want.judged
			|| (judged ? got.best : -1) != want.best
			|| got.presses != want.presses || got.trail != want.trail) {
			std::ostringstream what;
			what << "place " << want.lock << ": got held " << got.held
			     << " stored " << stored << " judged " << judged
			     << " best " << got.best << " presses";
			for (const auto& press : got.presses) what << " " << press;
			what << " trail";
			for (const auto& stop : got.trail) {
				what << " " << stop[0] << ":" << stop[1] << ":" << stop[2];
			}
			what << " against held " << want.held << " stored " << want.stored
			     << " judged " << want.judged << " best " << want.best
			     << " presses";
			for (const auto& press : want.presses) what << " " << press;
			what << " trail";
			for (const auto& stop : want.trail) {
				what << " " << stop[0] << ":" << stop[1] << ":" << stop[2];
			}
			complain(what.str());
			break;
		}
	}

	// The soundtrack: every cue, in order, on the frame it fired.
	const size_t both = std::min(heard.size(), trace.cues.size());
	for (size_t i = 0; i < both; ++i) {
		if (heard[i] != trace.cues[i]) {
			complain("cue " + std::to_string(i) + ": got '" + heard[i].second
				+ "' at frame " + std::to_string(heard[i].first) + " against '"
				+ trace.cues[i].second + "' at frame "
				+ std::to_string(trace.cues[i].first));
			break;
		}
	}
	if (bad == 0 && heard.size() != trace.cues.size()) {
		complain("fired " + std::to_string(heard.size()) + " cues against "
			+ std::to_string(trace.cues.size()));
	}

	// The ending: same loss, same frame count, same board.
	if (lost_at != trace.loss) {
		complain("lost at frame " + std::to_string(lost_at) + " against "
			+ std::to_string(trace.loss));
	}
	if (ran != trace.frames) {
		complain("ran " + std::to_string(ran) + " frames against "
			+ std::to_string(trace.frames));
	}
	if (sim.board().rows() != trace.board) {
		complain("the final board differs");
	}
	return bad;
}

} // namespace

int main (int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "usage: trace <traces.txt>\n";
		return 2;
	}
	std::ifstream source(argv[1]);
	if (!source) {
		std::cerr << "cannot read " << argv[1] << "\n";
		return 2;
	}

	int traces = 0;
	int failed = 0;
	Trace trace;
	bool open = false;
	std::string line;
	while (std::getline(source, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream in(line);
		std::string kind;
		in >> kind;
		if (kind == "trace") {
			trace = Trace{};
			in >> trace.name;
			open = true;
		} else if (kind == "config") {
			int kicks = 0;
			in >> trace.config.das_ms >> trace.config.arr_ms >> trace.config.dcd_ms
			   >> trace.config.sdf >> trace.config.are_ms >> trace.config.forced_delay
			   >> kicks >> trace.config.finesse_rule >> trace.config.fall_delay
			   >> trace.config.spin_rule >> trace.config.gametype
			   >> trace.config.timer_ms >> trace.config.start_lines
			   >> trace.config.cleartype;
			trace.config.kicks = kicks != 0;
		} else if (kind == "holes") {
			int hole = 0;
			while (in >> hole) {
				trace.holes.push_back(hole);
			}
		} else if (kind == "seed") {
			std::string row;
			in >> row;
			trace.seed.push_back(row);
		} else if (kind == "score") {
			Score score{};
			in >> score.lock >> score.spin >> score.b2b >> score.combo
			   >> score.perfect >> score.attack >> score.score >> score.downstack;
			trace.scores.push_back(score);
		} else if (kind == "place") {
			Place place{};
			std::string queue;
			std::string presses;
			std::string trail;
			in >> place.lock >> place.held >> place.stored >> queue
			   >> place.judged >> place.best >> presses >> trail;
			for (const std::string& form : split_list(queue)) {
				place.queue.push_back(std::stoi(form));
			}
			place.presses = split_list(presses);
			for (const std::string& stop : split_list(trail)) {
				std::array<int, 3> parsed{};
				std::istringstream part(stop);
				char sep = 0;
				part >> parsed[0] >> sep >> parsed[1] >> sep >> parsed[2];
				place.trail.push_back(parsed);
			}
			trace.places.push_back(place);
		} else if (kind == "cue") {
			long frame = 0;
			std::string name;
			in >> frame >> name;
			trace.cues.emplace_back(frame, name);
		} else if (kind == "pieces") {
			int form = 0;
			while (in >> form) {
				trace.pieces.push_back(form);
			}
		} else if (kind == "ev") {
			long frame = 0;
			std::string key;
			int down = 0;
			in >> frame >> key >> down;
			const auto named = key_named(key);
			if (named.has_value()) {
				trace.events[frame] = Event{*named, down != 0};
			}
		} else if (kind == "p") {
			Snap snap{};
			in >> snap.frame >> snap.form >> snap.state >> snap.x >> snap.y >> snap.entry;
			trace.expected.push_back(snap);
		} else if (kind == "lock") {
			Locked lock{};
			int forced = 0;
			int rotated = 0;
			int twist = 0;
			in >> lock.frame >> lock.form >> lock.state >> lock.x >> lock.y
			   >> forced >> rotated >> twist;
			lock.forced = forced != 0;
			lock.rotated = rotated != 0;
			lock.twist = twist != 0;
			trace.locks.push_back(lock);
		} else if (kind == "loss") {
			in >> trace.loss;
		} else if (kind == "frames") {
			in >> trace.frames;
		} else if (kind == "row") {
			std::string row;
			in >> row;
			trace.board.push_back(row);
		} else if (kind == "end" && open) {
			++traces;
			const int bad = grade(trace);
			std::cout << (bad == 0 ? "PASS " : "FAIL ") << "the sim replays '"
			          << trace.name << "' move for move -- " << trace.locks.size()
			          << " locks over " << trace.frames << " frames";
			if (bad != 0) {
				std::cout << ", " << bad << " disagreement(s)";
				++failed;
			}
			std::cout << "\n";
			open = false;
		}
	}

	std::cout << "\n";
	if (failed != 0 || traces == 0) {
		std::cout << failed << " of " << traces << " traces disagree with the Python engine.\n";
		return 1;
	}
	std::cout << "The sim agrees with the Python engine on every trace.\n";
	return 0;
}
