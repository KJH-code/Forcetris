// The high score cross check, C++ side.
//
// Driven by tools/hiscore_cross.py. `write <folder>` submits a scripted
// sequence of entries - ties, misses and all - printing each one's announced
// place first; the Python side replays the same sequence through SFH.encode
// and the two hiscore.dat files must be byte-identical, the places identical
// to SortedCollection's. `read <folder>` prints whatever table a folder
// holds, so a Python-written file can be checked against the Python reading.
#include <iostream>
#include <string>
#include <vector>

#include "forcetris/hiscore.hpp"

namespace {

using namespace forcetris;

struct Turn {
	const char* gametype;
	const char* name;      // Exactly eight characters.
	std::uint64_t score;
	std::uint32_t lines;
	std::uint32_t timer;
};

// The scripted submissions: enough per table to push entries off the end,
// an exact three-field tie to pin both halves of the tie quirk, a zero score
// that ties the Pajitnov defaults, and the lines column deciding one rank.
const Turn kTurns[] = {
	{"free", "AAAAAAAA", 5000, 20, 12345},
	{"free", "BBBBBBBB", 7000, 30, 11111},
	{"free", "CCCCCCCC", 5000, 20, 12345},   // The exact tie with AAAAAAAA.
	{"free", "DDDDDDDD", 5000, 20, 12000},   // Same score, faster.
	{"free", "EEEEEEEE", 5000, 25, 12000},   // Same score and time, more lines.
	{"free", "ZZZZZZZZ", 0, 0, 0},           // Ties the sitting defaults.
	{"free", "F1      ", 100, 1, 50000},
	{"free", "F2      ", 200, 2, 40000},
	{"free", "F3      ", 300, 3, 30000},
	{"free", "F4      ", 400, 4, 20000},
	{"free", "F5      ", 500, 5, 10000},
	{"free", "MISSES  ", 0, 99, 4294967295}, // Worst of eleven: dropped.
	{"timed", "TIMEDONE", 9000, 40, 29500},
	{"timed", "TIMEDTWO", 8000, 35, 30000},
	{"arcade", "ARCADEON", 12000, 64, 60000},
	{"arcade", "ARCADETW", 11000, 60, 61000},
	{"arcade", "ARCADETH", 12000, 64, 60000}, // Tie in another table.
};

void dump (const hiscore::Tables& tables) {
	static const char* kNames[] = {"arcade", "timed", "free"};
	for (int t = 0; t < hiscore::kTables; ++t) {
		for (const hiscore::Entry& entry : tables[t]) {
			std::cout << "row " << kNames[t] << " ["
			          << std::string(entry.name.begin(), entry.name.end())
			          << "] " << entry.score << " " << entry.lines << " "
			          << entry.timer << " " << hiscore::shown_timer(entry.timer)
			          << "\n";
		}
	}
}

} // namespace

int main (int argc, char** argv) {
	if (argc != 3) {
		std::cerr << "usage: hiscorecheck write|read <folder>\n";
		return 2;
	}
	const std::string mode = argv[1];
	const std::string folder = argv[2];
	if (mode == "write") {
		for (const Turn& turn : kTurns) {
			hiscore::Entry entry;
			std::copy(turn.name, turn.name + 8, entry.name.begin());
			entry.score = turn.score;
			entry.lines = turn.lines;
			entry.timer = turn.timer;
			const int at = hiscore::place(
				hiscore::load(folder), turn.gametype, entry);
			std::cout << "place " << turn.gametype << " " << turn.name
			          << " " << at << "\n";
			if (!hiscore::submit(folder, turn.gametype, entry)) {
				std::cerr << "could not write into " << folder << "\n";
				return 1;
			}
		}
		return 0;
	}
	if (mode == "read") {
		dump(hiscore::load(folder));
		return 0;
	}
	std::cerr << "unknown mode " << mode << "\n";
	return 2;
}
