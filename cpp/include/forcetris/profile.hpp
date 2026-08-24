// The profile: every finished game written down, one line each, so the
// screens can show where a player has been and how they are growing.
//
// This side's own file (the Python game keeps no such history), stored as
// key=value pairs per line - a reader that meets a key it does not know
// skips it, and a writer from the future can add keys without breaking a
// reader from the past. The same tolerance the replay format lives by.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace forcetris {
namespace profile {

// One finished game. Only `played` and `gametype` are identity; everything
// else is a number the screens aggregate and chart. `extras` carries any
// keys this build does not model, preserved on rewrite.
struct GameRecord {
	std::string played;       // ISO datetime, seconds precision.
	std::string gametype = "free";
	double seconds = 0.;
	int pieces = 0;
	int lines = 0;
	long long score = 0;
	int attack = 0;
	int downstack = 0;
	double pps = 0.;
	double apm = 0.;
	double vs = 0.;
	double finesse = 0.;      // Percent, 0-100.
	double tr = -1.;          // Estimated TR, -1 when not computable.
	int won = -1;             // Versus: 1 won, 0 lost; -1 elsewhere.
	// The munch numbers, keyed by their stat name (apl, dspp, ...).
	std::map<std::string, double> stats;
};

// Where the history lives: FORCETRIS_PROFILE if set, else
// <root>/data/profile.dat.
std::string path (const std::string& root);

// Append one game to the history. Returns false if the file cannot be
// written; losing a line of history is not a reason to take the game down.
bool append (const std::string& path, const GameRecord& record);

// The whole history, oldest first. Lines that will not parse are skipped.
std::vector<GameRecord> load (const std::string& path);

} // namespace profile
} // namespace forcetris
