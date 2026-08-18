#include "forcetris/sim.hpp"

#include <algorithm>
#include <cmath>

#include "forcetris/attack.hpp"
#include "forcetris/finesse.hpp"
#include "forcetris/spins.hpp"

namespace forcetris {

int py_round (double value) {
	// Half to even, for the non-negative values the handling maths produces.
	const double floor = std::floor(value);
	const double diff = value - floor;
	const int low = static_cast<int>(floor);
	if (diff > 0.5) {
		return low + 1;
	}
	if (diff < 0.5) {
		return low;
	}
	return low % 2 == 0 ? low : low + 1;
}

Sim::Sim (const SimConfig& config, std::vector<int> pieces)
	: config_(config), queue_(pieces.begin(), pieces.end()) {
	// apply_handling, run once since nothing in a trace retunes it mid-game.
	shift_delay_ = py_round(config.das_ms / 20.);
	shift_fdelay_ = py_round(config.arr_ms / 20.);
	dcd_frames_ = py_round(config.dcd_ms / 20.);
	soft_instant_ = config.sdf >= 40;
	soft_delay_ = std::max(1, py_round(config.fall_delay / static_cast<double>(config.sdf)));
	entry_delay_ = py_round(config.are_ms / 20.);
	// set_data seeds the first entry countdown from the mode's own entry delay
	// - free mode's twenty frames - before apply_handling has replaced the
	// delay with the player's ARE. Only the very first spawn waits this long;
	// every later one uses the ARE-derived count, as the game does.
	entry_frame_ = 20;
	piece_.form = GARBAGE;
}

void Sim::cut_das () {
	if (dcd_frames_ > 0 && das_charged_) {
		shift_frame_ = dcd_frames_ + 1;
		das_charged_ = false;
	}
}

void Sim::eval_block () {
	// A blocked spawn is a loss, unless locking the piece where it stands would
	// complete a line - then the line clears and the game goes on.
	if (board_.collides(piece_)) {
		lost_ = true;
		return;
	}
	bool trapped = true;
	bool covered[kWidth] = {};
	for (const Offset cell : cells_of(piece_)) {
		if (cell.y == 1 && cell.x >= 0 && cell.x < kWidth) {
			covered[cell.x] = true;
		}
	}
	bool clearable = true;
	for (int x = 0; x < kWidth; ++x) {
		if (!covered[x] && board_.at(x, 1) < 0) {
			clearable = false;
			break;
		}
	}
	if (clearable) {
		trapped = false;
	}
	if (trapped) {
		for (const Offset nudge : {Offset{0, 1}, Offset{1, 0}, Offset{-1, 0}}) {
			Piece probe = piece_;
			probe.x += nudge.x;
			probe.y += nudge.y;
			if (!board_.collides(probe)) {
				trapped = false;
				break;
			}
		}
	}
	if (trapped) {
		lost_ = true;
	}
}

void Sim::set_shape (int form) {
	floor_kick_ = true;
	hold_lock_ = false;
	// The spin flags live from one spawn to the next, not from lock to lock:
	// the line clearer reads tspin_flag frames after the piece locked, so
	// clearing them at lock made the spin bonus never once apply. An in-play
	// hold swap bypasses this and deliberately keeps them.
	rotated_last_ = false;
	twist_flag_ = false;
	tspin_flag_ = false;
	piece_ = Piece{form, 0, kSpawnX, kSpawnY};
	cand_x_ = piece_.x;
	eval_block();
	cut_das();
	inputs_ = 0;
	if (lost_) {
		piece_elapsed_.reset();
		if (loss_frame_ < 0) {
			loss_frame_ = frame_;
		}
	} else {
		piece_elapsed_ = 0.;
	}
}

void Sim::next_shape () {
	if (entry_ && !queue_.empty()) {
		const int form = queue_.front();
		queue_.pop_front();
		set_shape(form);
	}
}

bool Sim::hold_shape () {
	if (stored_ < 0) {
		// First hold of the game. The piece taken into storage comes off the
		// board or off the queue, and either way the queue moves on.
		if (entry_) {
			stored_ = piece_.form;
			next_shape();
		} else if (!queue_.empty()) {
			stored_ = queue_.front();
			queue_.pop_front();
		}
		return true;
	}
	if (!hold_lock_ && stored_ != piece_.form) {
		hold_lock_ = true;
		if (entry_) {
			const int coming = stored_;
			stored_ = piece_.form;
			piece_ = Piece{coming, 0, kSpawnX, kSpawnY};
			cand_x_ = piece_.x;
			// A swap bypasses set_shape, so the timer and the count restart here.
			piece_elapsed_ = 0.;
			inputs_ = 0;
		} else if (!queue_.empty()) {
			std::swap(stored_, queue_.front());
		}
		return true;
	}
	return false;
}

void Sim::spin_key (int turns) {
	++inputs_;
	const Rotation spun = rotate(board_, piece_, turns, config_.kicks, floor_kick_);
	floor_kick_ = spun.floor_kick;
	if (piece_.form != O) {
		// wall_kick leaves the twist flag exactly where the rotation left it:
		// set when it only fitted after a kick, cleared when it fitted plainly
		// or was refused. An O skips the whole kick step, flag included.
		twist_flag_ = spun.kicked;
	}
	if (spun.turned) {
		piece_ = spun.piece;
		cand_x_ = piece_.x;
		if (spun.kicked) {
			// Wall kicks reset the gravity timer.
			grav_frame_ = 0;
		}
	}
	// Set for the attempt, not the success: the engine marks the press itself,
	// refused rotations and O turns included.
	rotated_last_ = true;
	cut_das();
}

void Sim::eval_input (const std::optional<Event>& event) {
	if (!event.has_value()) {
		return;
	}
	const Key key = event->key;
	if (event->down) {
		if (key == Key::Left || key == Key::Right) {
			shift_dir_ = key == Key::Left ? -1 : 1;
			das_charged_ = false;
			shift_frame_ = shift_delay_ + 1;
			if (entry_) {
				++inputs_;
				cand_x_ = piece_.x + shift_dir_;
				rotated_last_ = false;
			}
		} else if (key == Key::Soft) {
			soft_ = true;
		} else if (key == Key::Hold) {
			hold_shape();
		} else if (entry_) {
			if (key == Key::Ccw) {
				spin_key(3);
			} else if (key == Key::Cw) {
				spin_key(1);
			} else if (key == Key::Flip) {
				spin_key(2);
			} else if (key == Key::Hard) {
				hard_drop(false);
			}
		}
	} else {
		if (key == Key::Soft) {
			soft_ = false;
		} else if ((key == Key::Left && shift_dir_ < 0)
			|| (key == Key::Right && shift_dir_ > 0)) {
			shift_dir_ = 0;
		}
	}
}

void Sim::eval_shift () {
	if (shift_frame_ > 1) {
		--shift_frame_;
	} else if (shift_dir_ != 0 && entry_) {
		shift_frame_ = shift_fdelay_;
		das_charged_ = true;
		// An auto-shift step is a move like any other, so it disarms the spin -
		// even when the step is then reverted at the wall.
		rotated_last_ = false;
		if (shift_fdelay_ < 1) {
			// ARR 0: cover the whole distance to the wall in this one frame.
			while (true) {
				Piece probe = piece_;
				probe.x = cand_x_ + shift_dir_;
				if (board_.collides(probe)) {
					break;
				}
				cand_x_ = probe.x;
			}
		} else {
			cand_x_ += shift_dir_;
		}
	}
	if (!entry_) {
		return;
	}
	// Prevent the piece from sliding into blocks: the frame's candidate either
	// commits whole or reverts whole.
	Piece probe = piece_;
	probe.x = cand_x_;
	if (board_.collides(probe)) {
		cand_x_ = piece_.x;
	} else {
		piece_.x = cand_x_;
	}
}

bool Sim::drop_reachable () const {
	Piece trial = piece_;
	trial.y = kSpawnY;
	if (board_.collides(trial)) {
		return false;
	}
	return board_.dropped(trial) == piece_;
}

bool Sim::try_retry (bool forced) {
	// The finesse judgement, exactly as eval_finesse applies it: only a retry
	// changes what the pieces do, and never on a forced or unjudgeable drop.
	if (forced || lost_ || piece_.form > 6) {
		return false;
	}
	if (config_.finesse_rule != 2) {
		return false;
	}
	if (!drop_reachable()) {
		return false;
	}
	const auto best = finesse::optimal(piece_.form, piece_.state, piece_.x);
	if (!best.has_value() || inputs_ - *best <= 0) {
		return false;
	}
	// Handed back: same piece, fresh spawn, and the time it already spent.
	const auto spent = piece_elapsed_;
	set_shape(piece_.form);
	piece_elapsed_ = spent;
	return true;
}

void Sim::lock (bool forced) {
	// eval_fallen: reset a held direction's charge, paste, start the clearer.
	if (shift_frame_ == 0 && shift_dir_ != 0) {
		shift_frame_ = shift_delay_ + 1;
	}
	// The spin verdict, decided while the piece still stands where it landed
	// and before it becomes part of the stack, as announce_spin decides it.
	int spin = attack::NOT_SPIN;
	const auto verdict = spins::judge(
		board_, piece_, static_cast<spins::Rule>(config_.spin_rule),
		rotated_last_, twist_flag_);
	if (verdict.has_value()) {
		spin = verdict->full ? attack::SPIN_FULL : attack::SPIN_MINI;
		tspin_flag_ = true;
	}
	// The finesse judgement the analysis screen shows: how many presses the
	// placement took against the fewest it needed. Placements finesse cannot
	// route - tucks, spins, forced drops - are left unjudged.
	int best = -1;
	if (!forced && piece_.form <= 6 && drop_reachable()) {
		best = finesse::optimal(piece_.form, piece_.state, piece_.x).value_or(-1);
	}
	board_.paste(piece_);
	const int lines = board_.clear_lines();
	Locked entry{
		frame_, piece_.form, piece_.state, piece_.x, piece_.y, lines, forced,
		rotated_last_, twist_flag_, inputs_, best};
	entry.spin = spin;
	locked_.push_back(entry);
	clearing_ = true;
	pending_rows_ = lines;
	sprite_frames_ = 0;
	entry_ = false;
	entry_frame_ = entry_delay_;
	piece_elapsed_.reset();
	piece_ = Piece{GARBAGE, 0, kSpawnX, kSpawnY};
	cand_x_ = piece_.x;
	grav_frame_ = 0;
}

bool Sim::hard_drop (bool forced) {
	if (!entry_ || clearing_) {
		return false;
	}
	piece_ = board_.dropped(piece_);
	cand_x_ = piece_.x;
	if (try_retry(forced)) {
		// Handed back rather than locked. A soft drop held through the retry gets
		// no new keypress to re-mark where it started from.
		return false;
	}
	lock(forced);
	return true;
}

void Sim::gravity () {
	const int grav_delay = soft_ ? soft_delay_ : config_.fall_delay;
	Piece below = piece_;
	below.y += 1;
	if (board_.collides(below)) {
		// Resting: the 30 frame grace, which movement does not reset.
		if (grav_frame_ < 29) {
			++grav_frame_;
		} else {
			grav_frame_ = 0;
			if (!try_retry(false)) {
				lock(false);
			}
		}
	} else {
		if (grav_frame_ < grav_delay - 1) {
			++grav_frame_;
		} else {
			grav_frame_ = 0;
			if (soft_ && soft_instant_) {
				// SDF at its maximum: fall to the floor now, without locking.
				piece_ = board_.dropped(piece_);
			} else {
				piece_.y += 1;
			}
			cand_x_ = piece_.x;
		}
	}
}

void Sim::clearing_step () {
	if (sprite_frames_ > 0) {
		--sprite_frames_;
	} else if (pending_rows_ > 0) {
		// One yield of the line clearer: one row named, one sprite raised for
		// the six frames its animation runs.
		--pending_rows_;
		sprite_frames_ = 6;
	} else {
		// The final yield: the clearer reports done, and the counters are
		// final - which is the moment the placement can be scored in full.
		clearing_ = false;
		resolve_score();
	}
}

void Sim::resolve_score () {
	// eval_clear_score, plus the attack sum Core.run makes once the clearer
	// has finished. tspin_flag is still set from the lock: it is cleared on
	// spawn, not at lock, precisely so this late reading works.
	Locked& last = locked_.back();
	const int total = last.lines;
	if (total > 0) {
		// A quad, or any clear that came out of a spin, carries the back to
		// back chain on. A smaller clear ends it. A placement that clears
		// nothing at all leaves it alone - but does break the combo.
		b2b_ = (tspin_flag_ || total >= 4) ? b2b_ + 1 : 0;
		lines_cleared_ += total;
		++combo_;
	} else {
		combo_ = 0;
	}
	const bool perfect = total > 0 && board_.empty();
	const int sent = attack::attack_for(
		total, static_cast<attack::SpinKind>(last.spin), b2b_ > 1,
		std::max(0, combo_ - 1), perfect);
	attack_sent_ += sent;
	last.scored = true;
	last.b2b = b2b_;
	last.combo = combo_;
	last.perfect = perfect;
	last.attack = sent;
}

bool Sim::step (const std::optional<Event>& event) {
	if (lost_) {
		return false;
	}
	// The frame's slice of real time, in the same arithmetic the fake clock
	// hands the Python side: the first frame is worth nothing.
	now_ = frame_ * 0.02;
	double delta = 0.;
	if (have_prev_) {
		delta = std::min(now_ - prev_, 0.1);
	}
	prev_ = now_;
	have_prev_ = true;

	// The frame runs to completion even if something in it loses the game -
	// Python's run() does, and the trace counts frames on both sides.
	eval_input(event);
	eval_shift();
	if (!clearing_) {
		if (entry_) {
			bool forced_lock = false;
			if (config_.forced_delay > 0. && piece_elapsed_.has_value()) {
				*piece_elapsed_ += delta;
				if (*piece_elapsed_ >= config_.forced_delay) {
					forced_lock = hard_drop(true);
				}
			}
			if (!forced_lock && entry_) {
				gravity();
			}
		} else if (entry_frame_ > 1) {
			--entry_frame_;
		} else {
			entry_ = true;
			next_shape();
		}
	}
	if (clearing_) {
		clearing_step();
	}
	++frame_;
	return !lost_;
}

} // namespace forcetris
