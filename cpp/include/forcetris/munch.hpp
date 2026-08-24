// The munch: MinoMuncher-style statistics chewed out of one recorded game,
// re-derived from our own per-placement records (no code shared with that
// tool - its published statistics, our arithmetic). Where its numbers need
// multiplayer data a trainer does not have, the stat stays out rather than
// being faked.
#pragma once

#include <string>
#include <vector>

namespace forcetris {

namespace replay {
struct Replay;
}

namespace munch {

// One number with its identity: `id` is the profile file's key, `label`
// what the screens print.
struct Stat {
	const char* id;
	const char* label;
	double value = 0.;
};

// A named block of stats, the way the screens group them.
struct Group {
	const char* name;
	std::vector<Stat> stats;
};

struct Stats {
	std::vector<Group> groups;

	// The value for an id, or fallback when the game did not produce it.
	double get (const std::string& id, double fallback = 0.) const;
};

// Chew a recorded game. Safe on any replay, however short.
Stats crunch (const replay::Replay& game);

// The display order of every stat id crunch can produce, and the label
// for one - for screens that aggregate ids from the profile file.
const std::vector<const char*>& order ();
const char* label (const std::string& id);

} // namespace munch
} // namespace forcetris
