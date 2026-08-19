// Replays: the same files the Python game writes, read and written here.
//
// A port of engine/replay.py, format for format. A replay is a list of
// placements - what the piece was, where it ended, what was pressed, where
// the piece stood after each press, and a snapshot of the board once the
// clear resolved - so playback is a re-enactment, never a re-simulation.
// The files are JSON in data/replays, and the two engines must be able to
// read each other's: a replay written here is checked against the Python
// reader by the cross test, and the other way around.
#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace forcetris {

struct Locked;

namespace replay {

// Bumped when the shape of the file changes; the oldest version still worth
// reading. Both mirror engine/replay.py and must move with it.
constexpr int kFormat = 3;
constexpr int kMinFormat = 2;
// How many files to keep, oldest pruned first.
constexpr int kKeep = 30;
// Games shorter than this are not worth a file.
constexpr int kMinPlacements = 5;
// Rows a snapshot covers, floor excluded.
constexpr int kSnapshotHeight = 22;

// What the game was played with. A typed mirror of the meta dict, which is
// all the Python side ever writes into it.
struct Meta {
	std::string played;      // ISO datetime, seconds precision.
	std::string gametype = "free";
	double forced_delay = 0.;
	int finesse = 0;
	int spinrule = 0;
	int cleartype = 0;
	int das = 0, arr = 0, dcd = 0, sdf = 0, are = 0;
	// 64-bit, like the sim's own score: Python's integer is unbounded.
	long long score = 0;
	int lines = 0;
	int downstack = 0;
	double seconds = 0.;
};

struct Placement {
	int form = 0;
	int state = 0;
	int x = 0;
	int y = 0;
	bool held = false;
	std::vector<std::string> presses;
	std::vector<std::array<int, 3>> trail;
	std::optional<int> best;
	bool judged = false;
	bool forced = false;
	int lines = 0;
	std::string spin;        // The banner label, empty for none.
	bool perfect = false;
	int combo = 0;           // As shown on the HUD: counter minus one.
	int b2b = 0;
	long long score = 0;
	double elapsed = 0.;
	std::vector<std::string> rows;
	std::vector<int> queue;  // The previews the player could see. Empty in v2.
	int stored = 7;          // 7 - the garbage form - for an empty box.
	int attack = 0;

	// Presses thrown away, or 0 for a placement finesse had no opinion about.
	int wasted () const;
	bool fault () const { return wasted() > 0; }

	// Where the piece stands at each stage of being placed: spawn, each stop,
	// the landing. With `fixed`, a judged placement walks the finesse route
	// instead - same piece, same column, same orientation, fewer stops.
	std::vector<std::array<int, 3>> steps (bool fixed = false) const;
	// The presses the screen names, matching whichever path it is animating.
	std::vector<std::string> presses_shown (bool fixed = false) const;
};

// The numbers the analysis screen shows.
struct Summary {
	int placements = 0;
	int judged = 0;
	int faults = 0;
	int wasted = 0;
	int presses = 0;
	double rate = 1.;
	double ppp = 0.;
	double pps = 0.;
	int lines = 0;
	long long score = 0;
	double seconds = 0.;
	std::map<int, int> clears;
	int spins = 0;
	int perfects = 0;
	int best_b2b = 0;
	int best_combo = 0;
	int attack = 0;
	double apm = 0.;
	double vs = 0.;
};

struct Replay {
	Meta meta;
	std::vector<Placement> placements;
	std::string path;

	// The board a placement was made onto: whatever the one before it left.
	std::vector<std::string> before (size_t index) const;
	// What the list screen shows for this file.
	std::string title () const;
	Summary summary (bool fixed = false) const;
};

// A snapshot back at full height, empty rows on top.
std::vector<std::string> padded (
	const std::vector<std::string>& rows, int height = kSnapshotHeight);

// One scored lock out of the sim, as the recorder would write it down: the
// banner label built from the verdict, the HUD's counter values, the board
// as the clear left it. Shared by the game and the cross test so the two
// cannot drift apart.
Placement from_locked (
	const forcetris::Locked& lock, std::vector<std::string> rows);

// Collects placements while a game is being played.
class Recorder {
public:
	void begin (const Meta& meta);
	void add (Placement place) { placements_.push_back(std::move(place)); }
	size_t count () const { return placements_.size(); }
	// Stamp the totals and hand back the replay, or nothing if the game was
	// too short to keep.
	std::optional<Replay> finish (
		long long score, int lines, int downstack, double seconds);

private:
	Meta meta_;
	std::vector<Placement> placements_;
};

// Where the files live: FORCETRIS_REPLAYS if set, else <root>/data/replays.
std::string folder (const std::string& root);

std::optional<Replay> load (const std::string& path);
// Write into `folder`, prune to the newest kKeep, and fill in replay.path.
// Returns false if the file could not be stored.
bool save (Replay& replay, const std::string& folder);
// Saved replays, newest first. Files that will not parse are left out.
std::vector<Replay> listing (const std::string& folder);

} // namespace replay
} // namespace forcetris
