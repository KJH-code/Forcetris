// One game being played: the graded sim, a bag to feed it, and the running
// totals the stat panels read.
//
// The sim is the same code the trace harness grades against the Python
// engine, driven here by real keys instead of a script. Everything the GUI
// shows is derived from what the sim reports; nothing about the game is
// decided in this layer.
#pragma once

#include <deque>
#include <optional>
#include <random>
#include <string>

#include "forcetris/sim.hpp"

namespace forcetris {
namespace gui {

// What the last resolved placement earned, for the banner over the board.
struct Banner {
	std::string text;
	long frame = -1;        // The sim frame it went up on.
};

class Session {
public:
	Session (const SimConfig& config, unsigned seed);

	// A key changing state, queued for the sim. The engine polls one event
	// per frame, so a burst of presses is spread over the following frames -
	// which is exactly what the Python game does with its event queue.
	void key (Key key, bool down);

	// One 20ms frame. Returns false once the game is over.
	bool step ();

	const Sim& sim () const { return sim_; }
	bool over () const { return over_; }

	// The totals, kept per lock rather than recounted per frame.
	double seconds () const { return sim_.frame() * 0.02; }
	int pieces () const { return static_cast<int>(sim_.locked().size()); }
	int presses () const { return presses_; }
	int spins () const { return spins_; }
	int perfects () const { return perfects_; }
	int forced () const { return forced_; }
	int faults () const { return faults_; }
	int judged () const { return judged_; }
	int wasted () const { return wasted_; }
	int best_b2b () const { return best_b2b_; }
	int best_combo () const { return best_combo_; }

	const Banner& banner () const { return banner_; }

private:
	void refill ();
	void absorb ();

	Sim sim_;
	std::mt19937 rng_;
	std::deque<Event> pending_;
	bool over_ = false;

	// How much of sim.locked() the totals have absorbed.
	size_t counted_ = 0;
	size_t scored_ = 0;
	int presses_ = 0;
	int spins_ = 0;
	int perfects_ = 0;
	int forced_ = 0;
	int faults_ = 0;
	int judged_ = 0;
	int wasted_ = 0;
	int best_b2b_ = 0;
	int best_combo_ = 0;
	Banner banner_;
};

} // namespace gui
} // namespace forcetris
