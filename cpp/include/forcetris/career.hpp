// The career: the ladder's conquest and the daily run, written down the
// profile file's way - tolerant key=value lines a future build can extend
// without breaking a past one.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace forcetris {
namespace career {

struct State {
	// Stars per ladder stage, keyed by the rank's name - keyed rather than
	// indexed so a ladder that grows a rung never shifts old progress.
	std::map<std::string, int> stars;
	// The daily run: the date whose attempt is burned, and what it scored.
	// A date with a score of -1 was started and never finished - the
	// attempt is spent all the same.
	std::string daily_date;
	long long daily_score = -1;
	// Lines this build did not understand, preserved verbatim.
	std::vector<std::string> unknown;
};

// Where the career lives: FORCETRIS_CAREER if set, else <root>/data/career.dat.
std::string path (const std::string& root);

State load (const std::string& path);
bool save (const std::string& path, const State& state);

// The gate: the first stage is always open, and each one after opens once
// the stage before it holds at least one star.
bool open (const State& state, const std::vector<std::string>& ladder,
	size_t stage);

} // namespace career
} // namespace forcetris
