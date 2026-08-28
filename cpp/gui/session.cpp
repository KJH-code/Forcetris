#include "session.hpp"

#include <algorithm>

namespace forcetris {
namespace gui {

namespace {

// The clear names the banner uses, matching the Python game's.
const char* clear_name (int lines) {
	switch (lines) {
		case 1: return "SINGLE";
		case 2: return "DOUBLE";
		case 3: return "TRIPLE";
		case 4: return "QUAD";
		default: return "";
	}
}

const char* form_letter (int form) {
	static const char* names[] = {"I", "O", "T", "S", "Z", "J", "L"};
	return form >= 0 && form <= 6 ? names[form] : "?";
}

} // namespace

Session::Session (const SimConfig& config, unsigned seed, const replay::Meta& meta)
	: sim_(config, {}), rng_(seed) {
	recorder_.begin(meta);
	refill();
}

void Session::draft (const SimConfig& rules, const std::string& id) {
	sim_.retune(rules);
	recorder_.drafted(id);
}

std::vector<std::string> Session::take_cues () {
	std::vector<std::string> drained;
	drained.swap(cue_box_);
	return drained;
}

std::optional<replay::Replay> Session::finish (bool keep_short) {
	// The loss-time counters, not the live ones: a timed game can die with a
	// clear still resolving, and the Python recorder is finished before that
	// clear lands its points.
	return recorder_.finish(
		sim_.final_score(), sim_.final_lines(), sim_.final_downstack(),
		sim_.frame() * 0.02, keep_short);
}

void Session::refill () {
	// The seven bag, as gen_shapelist deals it: a full set, shuffled, whenever
	// the queue runs low enough that the previews could run out.
	while (sim_.queue().size() < 8) {
		int bag[7] = {0, 1, 2, 3, 4, 5, 6};
		std::shuffle(bag, bag + 7, rng_);
		for (const int form : bag) {
			sim_.feed(form);
		}
	}
	// The garbage, dealt ahead of need: the sim never rolls its own dice.
	// Arcade's single holes land anywhere. The cheese modes deal hole masks
	// cut to the settings picked with the mode: so many holes per row, and a
	// messiness - the odds that a new row re-rolls its holes rather than
	// copying the row below's. A re-roll must actually move: two identical
	// rows out of a re-roll would stack into the clean well full messiness
	// exists to rule out.
	const SimConfig& config = sim_.config();
	while (config.gametype >= 2 && sim_.garbage_queued() < 10) {
		if (config.gametype == 2) {
			sim_.feed_garbage(static_cast<int>(rng_() % 10));
			continue;
		}
		const int holes = std::clamp(config.cheese_holes, 1, 3);
		int mask;
		if (last_mask_ > 0
			&& static_cast<int>(rng_() % 100) >= config.cheese_messiness) {
			mask = last_mask_;
		} else {
			do {
				mask = 0;
				int placed = 0;
				while (placed < holes) {
					const int bit = 1 << (rng_() % 10);
					if ((mask & bit) == 0) {
						mask |= bit;
						++placed;
					}
				}
			} while (mask == last_mask_);
		}
		last_mask_ = mask;
		sim_.feed_garbage(mask);
	}
}

void Session::key (Key key, bool down) {
	pending_.push_back(Event{key, down});
}

bool Session::step () {
	if (over_) {
		return false;
	}
	// Everything that arrived since the last frame lands this frame, in
	// order - a burst of presses must not queue up one per frame, which
	// reads as input lag to the player who made them.
	const std::vector<Event> events(pending_.begin(), pending_.end());
	pending_.clear();
	over_ = !sim_.step(events);
	cue_box_.insert(cue_box_.end(), sim_.cues().begin(), sim_.cues().end());
	refill();
	absorb();
	return !over_;
}

void Session::absorb () {
	const auto& locked = sim_.locked();
	for (; counted_ < locked.size(); ++counted_) {
		const Locked& lock = locked[counted_];
		presses_ += lock.inputs;
		if (lock.forced) {
			++forced_;
		}
		if (lock.best >= 0) {
			++judged_;
			const int over = lock.inputs - lock.best;
			if (over > 0) {
				++faults_;
				wasted_ += over;
			}
		}
	}
	// Scores resolve a few frames after their locks, strictly in order. The
	// recorder gets each placement the moment it resolves, with the board as
	// the clear left it - the same coupling the cross test grades. On the
	// loss frame the resolution comes after the loss, and by then Python's
	// recorder is already finished: a clear that resolves into a lost game
	// stays out of the replay and off the loss screens.
	if (over_) {
		return;
	}
	for (; scored_ < locked.size() && locked[scored_].scored; ++scored_) {
		const Locked& lock = locked[scored_];
		recorder_.add(replay::from_locked(lock, sim_.board().rows()));
		best_b2b_ = std::max(best_b2b_, lock.b2b - 1);
		best_combo_ = std::max(best_combo_, lock.combo - 1);
		if (lock.spin != 0) {
			++spins_;
		}
		if (lock.perfect) {
			++perfects_;
		}
		// The banner, built the way the Python game layers it: the spin, the
		// clear it made, and a perfect clear trumping the lot.
		std::string text;
		if (lock.spin != 0) {
			text = std::string(lock.spin == 1 ? "MINI " : "")
				+ form_letter(lock.form) + "-SPIN";
		}
		if (lock.lines > 0) {
			// announce_clear names the final chain link, not the cascade's
			// total, and past the named four it falls back to a number the
			// way CLEAR_NAMES.get does.
			const int link = lock.last_link > 0 ? lock.last_link : lock.lines;
			std::string name = clear_name(link);
			if (name.empty()) {
				name = std::to_string(link) + " LINES";
			}
			if (lock.lines > link) {
				// More lines fell than the final link names: the clear
				// cascaded. Say so, and hand the GUI a cue of its own -
				// with clear delay off the whole chain resolves inside one
				// frame, so without this the player sees nothing happen.
				// The cue is session-side only, never the sim's: the graded
				// cue stream that the traces compare stays untouched.
				name = "CASCADE  " + name;
				cue_box_.push_back("cascade");
			}
			text += std::string(text.empty() ? "" : "  ") + name;
		}
		if (lock.perfect) {
			text += std::string(text.empty() ? "" : "  ") + "PERFECT CLEAR";
		}
		if (lock.b2b > 1 && lock.lines > 0) {
			text += "  B2B x" + std::to_string(lock.b2b - 1);
		}
		if (lock.combo > 1) {
			text += "  " + std::to_string(lock.combo - 1) + " COMBO";
		}
		if (!text.empty()) {
			banner_ = Banner{text, sim_.frame()};
		}
	}
}

} // namespace gui
} // namespace forcetris
