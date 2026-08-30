// The opponent: a planner and a driver, in the tradition of the classic
// open bots (MisaMino, ColdClear) rather than copied from any of them.
//
// The planner searches every placement the piece can actually reach - taps,
// kicked rotations through the game's own kick tables, and sonic drops, so
// tucks and spins are in the move set, not special cases - and scores each
// with a two-part evaluation: the clear's real attack (surge bank included)
// plus a Dellacherie-style reading of the board it leaves behind. The driver
// turns the chosen route into the same key events a player would press, one
// per frame, paced to a chosen speed.
//
// Nothing here rolls hidden dice: a driver with the same seed against the
// same game plays the same moves, which is what botcheck pins.
#pragma once

#include <deque>
#include <random>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/sim.hpp"

namespace forcetris {
namespace bot {

// One step of a planned route, in the vocabulary the driver types with.
enum class Move : int { Left, Right, Cw, Ccw, Flip, Drop };

// A reachable placement: where the piece ends, how to get there, and what
// the evaluator thought of it.
struct Plan {
	bool use_hold = false;
	Piece landed{};
	std::vector<Move> route;
	// How the route ends, for the spin verdict: was the last thing done to
	// the piece a rotation, and did that rotation kick.
	bool rotated_last = false;
	bool kicked_last = false;
	int cleared = 0;
	int spin = 0;            // attack::SpinKind at plan time.
	double score = 0.;
};

// What the planner is allowed and told.
struct Options {
	int depth = 1;           // 1 greedy, 2 adds the next piece's hard drops,
	                         // 3+ runs a beam that deep with full reach.
	int width = 16;          // The beam's width; read only when depth >= 3.
	bool tucks = true;       // Moves after a drop.
	bool spins = true;       // Rotations after a drop.
	bool build = false;      // Reserve a quad well and spend clears dearly.
	bool kicks = true;       // The game's kick setting.
	int spin_rule = 2;       // spins::Rule, for the verdicts.
	int b2b = 0;             // The chain counters as the sim holds them.
	int combo = 0;
	int surge_charge = 0;
};

// Every placement reachable from `from` on `board`: BFS over taps, rotations
// (through kicks::rotate, so kicks behave exactly as the game's) and sonic
// drops. Deduped by final cells, cheapest route kept.
std::vector<Plan> candidates (const Board& board, const Piece& from,
                              bool floor_kick, const Options& options);

// The best plan for the piece in play, considering the hold box. `hold` is
// the stored form or -1; with -1 the hold branch plays the queue's head.
// Returns the ranked candidates, best first - the driver's blunders pick
// from further down the same list.
std::vector<Plan> plan (const Board& board, const Piece& piece,
                        bool floor_kick, int hold,
                        const std::deque<int>& queue,
                        const Options& options);

// The difficulty ladder: each rank plays at its real Tetra League average
// pace, blunders at its own rate, and only the upper half tucks, spins and
// looks ahead - the way those ranks actually play.
struct Rank {
	const char* name;
	double pps;
	double blunder;
	int depth;
	bool tucks;
	bool spins;
	bool build = false;   // Quad-well stacking instead of plain downstack.
	int width = 0;        // Beam width when depth >= 3.
};
const std::vector<Rank>& ranks ();

// How strong a foe is, said as a word rather than a league letter.
//
// "Bot (B)" asks the player to know what B means before it means anything,
// and outside TETR.IO nobody does. A word carries the same ordering - a
// Keen foe is plainly worse news than a Rough one - without a lookup, and
// it sits in front of a name the way an epithet should: "Keen Underwarden"
// rather than "The Underwarden (B)". Out of range clamps to the ends.
const char* might_of (int rank_index);

// The hands: one key event per frame, paced to a speed, deterministic under
// its seed. Call next() every frame the bot's sim is about to step and feed
// whatever it returns in.
class Driver {
public:
	Driver (unsigned seed, const Rank& rank);

	std::optional<Event> next (const Sim& sim);

	// The plan being executed, for the tests to hold the lock against.
	const Plan& current () const { return current_; }

private:
	void adopt (const Sim& sim);

	std::mt19937 rng_;
	Rank rank_;
	Plan current_{};
	std::vector<Event> script_;
	size_t cursor_ = 0;
	long due_frame_ = 0;
	size_t planned_locks_ = 0;
	bool live_ = false;
};

} // namespace bot
} // namespace forcetris
