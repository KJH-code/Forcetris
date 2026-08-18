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
};

struct Trace {
	std::string name;
	SimConfig config;
	std::vector<std::string> seed;
	std::vector<int> pieces;
	std::map<long, Event> events;
	std::vector<Snap> expected;
	std::vector<Locked> locks;
	std::vector<Score> scores;
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
	std::vector<Snap> seen;
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
			|| (got.perfect ? 1 : 0) != want.perfect || got.attack != want.attack) {
			complain("score " + std::to_string(want.lock) + ": got spin "
				+ std::to_string(got.spin) + " b2b " + std::to_string(got.b2b)
				+ " combo " + std::to_string(got.combo)
				+ " perfect " + std::to_string(got.perfect ? 1 : 0)
				+ " attack " + std::to_string(got.attack)
				+ " against spin " + std::to_string(want.spin)
				+ " b2b " + std::to_string(want.b2b)
				+ " combo " + std::to_string(want.combo)
				+ " perfect " + std::to_string(want.perfect)
				+ " attack " + std::to_string(want.attack));
			break;
		}
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
			   >> trace.config.spin_rule;
			trace.config.kicks = kicks != 0;
		} else if (kind == "seed") {
			std::string row;
			in >> row;
			trace.seed.push_back(row);
		} else if (kind == "score") {
			Score score{};
			in >> score.lock >> score.spin >> score.b2b >> score.combo
			   >> score.perfect >> score.attack;
			trace.scores.push_back(score);
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
