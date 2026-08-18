// The game loop: gravity, auto-shift, hold, the forced drop timer, locking and
// line clears, stepped one frame at a time.
//
// A port of Core.run and the methods under it, for the game's free mode with
// naive clearing - which is the trainer's default and the mode the eventual
// C++ game will run. It is graded against the Python engine by tools/
// dump_trace.py: the same scripted inputs are fed to both, and every frame the
// piece stands somewhere different is compared.
//
// Faithfulness beats taste everywhere the two disagree. The 30-frame lock
// grace that does not reset on movement, the DAS counter that keeps charging
// through a line clear, the +1 on the shift counter, the half-to-even rounding
// Python applies to the handling numbers - all ported as they are, because the
// point is that a replay or a trace means the same thing on both engines.
#pragma once

#include <deque>
#include <optional>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/kicks.hpp"

namespace forcetris {

struct SimConfig {
	// TETR.IO handling, in the units the settings menu shows.
	int das_ms = 140;
	int arr_ms = 40;
	int dcd_ms = 0;
	int sdf = 6;
	int are_ms = 0;
	// Seconds a piece may stay in play. 0 turns the timer off.
	double forced_delay = 1.0;
	bool kicks = true;
	// 0 off, 1 count, 2 retry - only retry changes what the pieces do.
	int finesse_rule = 1;
	// Free mode's gravity: frames between one row and the next.
	int fall_delay = 30;
};

enum class Key : int { Left, Right, Soft, Hard, Hold, Ccw, Cw, Flip };

struct Event {
	Key key;
	bool down = true;
};

// One piece locking, which is the unit a trace is compared in.
struct Locked {
	long frame = 0;
	int form = 0;
	int state = 0;
	int x = 0;
	int y = 0;
	int lines = 0;
	bool forced = false;
};

// Python's round(): half rounds to the even neighbour, which is not what
// std::round does, and the handling maths goes through it.
int py_round (double value);

class Sim {
public:
	// `pieces` is the queue in the order the bag would have dealt it. The sim
	// never generates pieces itself, so the trace controls the game completely.
	Sim (const SimConfig& config, std::vector<int> pieces);

	// One frame, with at most one input event - the engine polls one per frame.
	// Returns false once the game has been lost.
	bool step (const std::optional<Event>& event);

	long frame () const { return frame_; }
	bool entry () const { return entry_; }
	const Piece& piece () const { return piece_; }
	const Board& board () const { return board_; }
	const std::vector<Locked>& locked () const { return locked_; }
	long loss_frame () const { return loss_frame_; }
	int stored () const { return stored_; }

private:
	void eval_input (const std::optional<Event>& event);
	void eval_shift ();
	void gravity ();
	bool hard_drop (bool forced);
	void lock (bool forced);
	bool try_retry (bool forced);
	bool drop_reachable () const;
	void set_shape (int form);
	void next_shape ();
	bool hold_shape ();
	void spin_key (int turns);
	void cut_das ();
	void eval_block ();
	void clearing_step ();

	SimConfig config_;
	// The handling, in frames.
	int shift_delay_ = 0;
	int shift_fdelay_ = 0;
	int dcd_frames_ = 0;
	int soft_delay_ = 1;
	bool soft_instant_ = false;
	int entry_delay_ = 0;

	Board board_;
	std::deque<int> queue_;
	Piece piece_{};
	int cand_x_ = kSpawnX;      // newshape's x: where the frame's presses want the piece.
	int stored_ = -1;           // The held piece, or -1 for none.
	bool hold_lock_ = false;
	bool floor_kick_ = true;

	bool entry_ = false;
	int entry_frame_ = 0;
	bool clearing_ = false;
	int pending_rows_ = 0;      // Row-clear yields the generator still owes.
	int sprite_frames_ = 0;     // Frames the clear animation has left.

	int shift_dir_ = 0;         // -1 left, 1 right, 0 none.
	int shift_frame_ = 0;
	bool das_charged_ = false;
	bool soft_ = false;
	int grav_frame_ = 0;
	int inputs_ = 0;            // Presses finesse counts on the piece in play.

	// The forced drop clock, in the same arithmetic Python runs.
	double now_ = 0.;
	bool have_prev_ = false;
	double prev_ = 0.;
	std::optional<double> piece_elapsed_;

	long frame_ = 0;
	bool lost_ = false;
	long loss_frame_ = -1;
	std::vector<Locked> locked_;
};

} // namespace forcetris
