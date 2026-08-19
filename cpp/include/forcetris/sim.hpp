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

#include <array>
#include <deque>
#include <optional>
#include <string>
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
	// Free mode's gravity: frames between one row and the next. Timed and
	// arcade bring their own pace and ignore this.
	int fall_delay = 30;
	// Which rotations count as spins - spins::Rule, defaulting as the game does.
	// Scoring only: no placement moves differently for it.
	int spin_rule = 2;
	// The clear style: 0 naive, 1 sticky cascade, 2 linked cascade. Naive is
	// the guideline's and the trainer's default; the cascade styles let what
	// was left hanging fall, one row per frame, and clear again.
	int cleartype = 0;
	// The mode: 0 free, 1 timed, 2 arcade. Timed runs its clock down and ends
	// the game at zero; arcade ramps gravity with the level and pushes garbage
	// rows up from the floor.
	int gametype = 0;
	// Timed mode's clock, in milliseconds. The game's own five minutes by
	// default; a trace shortens it to something a trace can outlive.
	int timer_ms = 5 * 60 * 1000;
	// Lines already on the counter when the game starts: zero in play, set by
	// a trace that needs arcade's high levels within a trace's reach.
	int start_lines = 0;
};

enum class Key : int { Left, Right, Soft, Hard, Hold, Ccw, Cw, Flip };

struct Event {
	Key key;
	bool down = true;
};

// One piece locking, which is the unit a trace is compared in. The score half
// is settled a few frames after the geometry half: the chain counters are only
// final once the line clearer has finished, so until `scored` goes up the
// fields after it still hold their defaults.
struct Locked {
	long frame = 0;
	int form = 0;
	int state = 0;
	int x = 0;
	int y = 0;
	int lines = 0;
	bool forced = false;
	// The two flags the spin verdict read, as they stood at the lock. Written
	// down so a trace can grade the flag bookkeeping at every lock rather than
	// only where a spin happened to land.
	bool rotated = false;
	bool twist = false;
	// The presses finesse counted on the piece, against the fewest that could
	// have made the placement. `best` is -1 when the placement is not judgeable.
	int inputs = 0;
	int best = -1;
	// What the recorder writes down about the journey: the presses by name,
	// where the piece stood after each of them, whether it came out of the
	// hold box, what the box held, and the previews the player could see.
	std::vector<std::string> presses;
	std::vector<std::array<int, 3>> trail;   // state, x, y after each press.
	bool held = false;
	int stored = -1;
	std::array<int, 3> queue3{-1, -1, -1};
	// Filled when the clear resolves.
	bool scored = false;
	int spin = 0;        // attack::SpinKind: 0 none, 1 mini, 2 full.
	int b2b = 0;         // The counters as eval_clear_score left them.
	int combo = 0;
	bool perfect = false;
	int attack = 0;
	// The game score once this placement had settled. 64-bit because Python's
	// is unbounded: a marathon past two billion points must diverge nowhere.
	long long score = 0;
	int downstack = 0;   // Garbage rows dug out so far, this clear included.
	// The final chain link of the clear - what announce_clear names. Equal to
	// `lines` under naive clearing; a cascade's total spreads over its links.
	int last_link = 0;
};

// Python's round(): half rounds to the even neighbour, which is not what
// std::round does, and the handling maths goes through it.
int py_round (double value);

class Sim {
public:
	// `pieces` is the queue in the order the bag would have dealt it. The sim
	// never generates pieces itself, so the trace controls the game completely.
	Sim (const SimConfig& config, std::vector<int> pieces);

	// Start from a board other than an empty one. Only sensible before the
	// first step, which is when a seeded trace or a practice setup wants it.
	void seed (const Board& board) { board_ = board; }

	// Append to the piece queue. The game proper feeds its bag through this as
	// the queue runs down; a trace deals everything up front instead.
	void feed (int form) { queue_.push_back(form); }

	// Where arcade's next garbage rows leave their hole. The sim never rolls
	// its own dice, for pieces or for holes: the game feeds its RNG through
	// here, a trace feeds the recorded sequence.
	void feed_garbage (int hole) { holes_.push_back(hole); }

	// One frame, with at most one input event - the engine polls one per frame.
	// Returns false once the game has been lost.
	bool step (const std::optional<Event>& event);

	long frame () const { return frame_; }
	bool entry () const { return entry_; }
	bool clearing () const { return clearing_; }
	const SimConfig& config () const { return config_; }
	const Piece& piece () const { return piece_; }
	const Board& board () const { return board_; }
	const std::vector<Locked>& locked () const { return locked_; }
	long loss_frame () const { return loss_frame_; }
	int stored () const { return stored_; }
	const std::deque<int>& queue () const { return queue_; }

	// The chain counters and totals, as the analysis screen reads them. The
	// counters show one behind their HUD figures: the HUD prints b2b - 1.
	int b2b () const { return b2b_; }
	int combo () const { return combo_; }
	int attack_sent () const { return attack_sent_; }
	int lines_cleared () const { return lines_cleared_; }
	long long score () const { return score_; }
	int downstack () const { return downstack_; }
	int level () const { return level_; }
	// The counters as they stood at the loss, which is what the loss screens
	// read: a timed game can die with a clear still resolving, and Python's
	// eval_loss takes its high score entry and replay before that clear
	// resolves. Fall back to the live figures while the game is still on.
	long long final_score () const { return lost_ ? final_score_ : score_; }
	int final_lines () const { return lost_ ? final_lines_ : lines_cleared_; }
	int final_attack () const { return lost_ ? final_attack_ : attack_sent_; }
	int final_downstack () const {
		return lost_ ? final_downstack_ : downstack_;
	}
	// Timed mode's remaining milliseconds; the elapsed count elsewhere.
	long timer_ms () const { return timer_ms_; }
	// How many holes the queue still holds, so the game knows when to roll.
	size_t garbage_queued () const { return holes_.size(); }

	// The sound cues the last step fired, in the order Core would have fired
	// them, by the names the files in sound/ carry.
	const std::vector<std::string>& cues () const { return cues_; }

	// Seconds the piece in play has been in play, for the forced drop meter.
	// Empty between pieces or when the game is over.
	std::optional<double> piece_elapsed () const { return piece_elapsed_; }

private:
	void eval_input (const std::optional<Event>& event);
	void eval_shift ();
	void gravity ();
	bool hard_drop (bool forced);
	void lock (bool forced, int posdif);
	bool eval_finesse (bool forced);
	bool drop_reachable () const;
	void set_shape (int form);
	void next_shape ();
	bool hold_shape ();
	void spin_key (int turns, const char* pressed);
	void cut_das ();
	void eval_block ();
	void clearing_step ();
	void resolve_score ();
	void note_input (const char* name);
	void settle_move ();
	void ramp_arcade ();
	void eval_timer ();
	void cue (std::string name) { cues_.push_back(std::move(name)); }

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
	int sprite_frames_ = 0;     // Frames the clear animation has left.
	// The line clearer, unrolled from its generator: which half of the loop
	// the next resume lands in, the lowest row the current pass cleared, and
	// the rows each chain link took - line_list, lifecycle and all.
	int clear_phase_ = 0;       // 0 the row scan, 1 the settle loop.
	int clear_base_ = 0;
	std::vector<int> line_list_;

	int shift_dir_ = 0;         // -1 left, 1 right, 0 none.
	int shift_frame_ = 0;
	bool das_charged_ = false;
	bool soft_ = false;
	int grav_frame_ = 0;
	int inputs_ = 0;            // Presses finesse counts on the piece in play.

	// The spin bookkeeping, tracked as Core tracks it: `rotated_last_` is true
	// while the last thing done to the piece was a rotation, `twist_flag_` while
	// the last rotation only fitted after a kick, and `tspin_flag_` from a spin
	// verdict at lock until the next spawn - the line clearer reads it frames
	// after the piece is gone.
	bool rotated_last_ = false;
	bool twist_flag_ = false;
	bool tspin_flag_ = false;
	int b2b_ = 0;
	int combo_ = 0;
	int attack_sent_ = 0;
	int lines_cleared_ = 0;
	int downstack_ = 0;

	// The mode's moving parts: gravity that arcade ramps, the level driving
	// it, the countdown to the next garbage row, the hole feed, and the clock
	// that timed mode runs down and the others run up.
	int fall_delay_ = 30;
	int level_ = 1;
	int line_frame_ = 300;
	std::deque<int> holes_;
	long timer_ms_ = 0;

	// The score, in the game's own arithmetic: a point or so per drop plus the
	// clear formula, with the combo multiplier trailing the counter by one
	// clear the way current_combo does. 64-bit: Python's integer is unbounded
	// and a long game must not wrap where the reference keeps counting.
	long long score_ = 0;
	// The loss snapshot the final_* accessors serve.
	long long final_score_ = 0;
	int final_lines_ = 0;
	int final_attack_ = 0;
	int final_downstack_ = 0;
	double current_combo_ = 1.0;
	bool hard_flag_ = false;
	int soft_pos_ = 21;          // Where the current soft drop was marked from.

	// The journey the recorder writes down, reset with the piece.
	std::vector<std::string> input_log_;
	std::vector<std::optional<std::array<int, 3>>> trail_;
	bool from_hold_ = false;
	// The finesse judgement of the placement being locked, stashed by
	// eval_finesse for the Locked entry the way last_judged/last_best are.
	std::optional<int> last_best_;
	bool last_judged_ = false;

	std::vector<std::string> cues_;
	bool gameover_cued_ = false;

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
