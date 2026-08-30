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

namespace {

// The same half-to-even round, kept in doubles: the score formula mirrors
// Python's int(round(stake / 50, 0) * 50), whose figures outgrow int long
// before they outgrow a double's integer range.
double py_round_whole (double value) {
	const double floor = std::floor(value);
	const double diff = value - floor;
	if (diff > 0.5) {
		return floor + 1.0;
	}
	if (diff < 0.5) {
		return floor;
	}
	return std::fmod(floor, 2.0) == 0.0 ? floor : floor + 1.0;
}

} // namespace

Sim::Sim (const SimConfig& config, std::vector<int> pieces)
	: config_(config), queue_(pieces.begin(), pieces.end()) {
	// apply_handling. The handling half never moves mid-game; the soft drop
	// half is re-derived each step because arcade's gravity ramp drags it.
	shift_delay_ = py_round(config.das_ms / 20.);
	shift_fdelay_ = py_round(config.arr_ms / 20.);
	dcd_frames_ = py_round(config.dcd_ms / 20.);
	soft_instant_ = config.sdf >= 40;
	entry_delay_ = py_round(config.are_ms / 20.);
	// set_data: each mode brings its own pace, and seeds the first entry
	// countdown from its own entry delay before apply_handling has replaced
	// the delay with the player's ARE. Only the very first spawn waits this
	// long; every later one uses the ARE-derived count, as the game does.
	if (config.gametype == 1) {
		fall_delay_ = 10;
		entry_frame_ = 10;
	} else if (config.gametype == 2) {
		fall_delay_ = 45;
		entry_frame_ = 30;
	} else {
		fall_delay_ = config.fall_delay;
		entry_frame_ = 20;
	}
	if (config.gametype == 3) {
		cheese_left_ = std::max(0, config.cheese_total);
	} else if (config.gametype == 4) {
		cheese_frame_ = std::max(1, config.cheese_period);
	}
	soft_delay_ = std::max(1, py_round(fall_delay_ / static_cast<double>(config.sdf)));
	timer_ms_ = config.gametype == 1 ? config.timer_ms : 0;
	lines_cleared_ = config.start_lines;
	// Sealed Columns is terrain, so it lives on the board from the start.
	board_.set_sealed(config.sealed);
	piece_.form = GARBAGE;
}

void Sim::retune (const SimConfig& rules) {
	// Field by field rather than a whole-struct copy: the point of this
	// function is what it does *not* take, and a copy would silently start
	// taking new fields the day one is added.
	config_.spin_rule = rules.spin_rule;
	config_.cleartype = rules.cleartype;
	config_.fuse_base = rules.fuse_base;
	config_.fuse_min = rules.fuse_min;
	config_.fuse_decay = rules.fuse_decay;
	config_.fuse_bank_cap = rules.fuse_bank_cap;
	config_.fuse_draw_cap = rules.fuse_draw_cap;
	config_.fuse_refuel_line = rules.fuse_refuel_line;
	config_.fuse_refuel_attack = rules.fuse_refuel_attack;
	config_.flash_frac = rules.flash_frac;
	config_.flash_floor = rules.flash_floor;
	config_.flow_gain_line = rules.flow_gain_line;
	config_.flow_gain_attack = rules.flow_gain_attack;
	config_.flow_flash_gain = rules.flow_flash_gain;
	config_.flow_burn_loss = rules.flow_burn_loss;
	config_.overdrive_secs = rules.overdrive_secs;
	config_.overdrive_mult = rules.overdrive_mult;
	config_.flow_ignite = rules.flow_ignite;
	config_.fuse_pressure = rules.fuse_pressure;
	config_.flow_gain_dig = rules.flow_gain_dig;
	config_.flow_gain_taken = rules.flow_gain_taken;
	config_.flow_cap = rules.flow_cap;
	config_.flow_keep = rules.flow_keep;
	config_.overdrive_refill = rules.overdrive_refill;
	config_.flash_finesse = rules.flash_finesse;
	config_.flow_flood_loss = rules.flow_flood_loss;
	// The card effects, and the gimmicks a card may now carry: cold iron
	// rides a rule card, and the sealed mask must land on the board the
	// moment the rules do.
	config_.attack_scale = rules.attack_scale;
	config_.crit_every = rules.crit_every;
	config_.hold_churn = rules.hold_churn;
	config_.garbage_scale = rules.garbage_scale;
	config_.sweep_every = rules.sweep_every;
	config_.free_hold = rules.free_hold;
	config_.cold_iron = rules.cold_iron;
	config_.sealed = rules.sealed;
	board_.set_sealed(config_.sealed);
	// The chaos cards rewrite what the board itself does, so they land the
	// moment they are drafted like every other card. Gravity comes with
	// them: a card that opens the walls pays for it in fall speed, and
	// that price has to arrive with the gift.
	config_.wild_spins = rules.wild_spins;
	config_.wrap_walls = rules.wrap_walls;
	config_.fall_delay = rules.fall_delay;
	// Gravity is read from the member, not the tuning, so a card drafted
	// mid-run has to move the member too - without this the ring's price
	// and the counterweight's gift were both quietly nothing until the
	// next stage rebuilt the sim. Arcade owns its own ramp and overwrites
	// this on its next level, which is correct: its ladder is the mode's,
	// not a card's.
	if (config_.gametype != 2) {
		fall_delay_ = config_.fall_delay;
	}
	// The bank may now hold less than it is holding; a temper that shrinks
	// the reservoir takes the overflow with it rather than leaving a value
	// the sim's own ceiling says is impossible.
	fuse_bank_ = std::min(fuse_bank_, config_.fuse_bank_cap);
}

void Sim::cut_das () {
	if (dcd_frames_ > 0 && das_charged_) {
		shift_frame_ = dcd_frames_ + 1;
		das_charged_ = false;
	}
}

void Sim::note_input (const char* name) {
	// One press of a key finesse counts. The position is filled in on the
	// *next* press rather than this one, because a press is not finished when
	// the key goes down - what matters is where the piece came to rest.
	++inputs_;
	settle_move();
	input_log_.push_back(name);
	trail_.push_back(std::nullopt);
}

void Sim::settle_move () {
	// Close off the press before this one at wherever the piece now is.
	if (!trail_.empty() && !trail_.back().has_value()) {
		trail_.back() = std::array<int, 3>{piece_.state, piece_.x, piece_.y};
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

// The fuse ruleset's level: arcade has a real one, everything else earns
// one per ten lines - defined here so no graded mode logic learns of it.
int Sim::fuse_level () const {
	return config_.gametype == 2 ? level_ : lines_cleared_ / 10;
}

// Deal a fresh fuse to the piece just spawned: the schedule for the level,
// topped up from the refuel bank - but never past the base. At level zero
// the schedule is the base, so the bank only starts paying out once the
// game has begun to squeeze.
void Sim::fuse_prime () {
	const double schedule = std::clamp(
		config_.fuse_base - fuse_level() * config_.fuse_decay,
		config_.fuse_min, config_.fuse_base);
	const double draw = std::max(0., std::min(
		{fuse_bank_, config_.fuse_draw_cap, config_.fuse_base - schedule}));
	fuse_bank_ -= draw;
	fuse_total_ = schedule + draw;
	fuse_warned_ = false;
}

// The Flow accounting at a lock: a forced drop bleeds the gauge, and a
// lock inside the Flash window earns speed's one small bonus. Quality -
// the lines and attack the placement resolves into - charges the gauge
// where the clear is scored, not here.
void Sim::fuse_lock (bool forced) {
	if (lost_) {
		return;
	}
	if (forced) {
		// Only a clock forces a drop, so this is a burn room by
		// construction.
		if (config_.fuse) {
			flow_ = std::max(0., flow_ - config_.flow_burn_loss);
		}
		return;
	}
	if (config_.fuse && piece_elapsed_.has_value() && fuse_total_ > 0.) {
		const double window = std::max(
			config_.flash_floor, fuse_total_ * config_.flash_frac);
		if (*piece_elapsed_ <= window) {
			cue("flash");
			fuse_charge(config_.flow_flash_gain);
		}
		return;
	}
	// The clean flash. With no clock to be fast against, the bonus goes to
	// the placement that wasted the fewest presses - and only when a card
	// asked for it. Paying for pace alone once made pace the whole game;
	// finesse is not pace, it is care.
	if (charging() && config_.flash_finesse > 0 && last_judged_
		&& last_best_.has_value()
		&& inputs_ - *last_best_ < config_.flash_finesse) {
		cue("flash");
		fuse_charge(config_.flow_flash_gain);
	}
}

// Feed the gauge and ignite Overdrive when it fills, unless one is
// already burning - the gauge holds full until that one gutters out.
void Sim::fuse_charge (double gain) {
	// The other board's Overdrive smothers this one's supply. This is
	// where heat pressure lives now: it used to lean only on the wick,
	// which meant it vanished the day the duels put their clocks away -
	// and the duel is exactly the fight it was written for.
	if (pressured_ && config_.fuse_pressure > 1.) {
		gain /= config_.fuse_pressure;
	}
	// A floor of a hundred under the ceiling: however a build stacks, the
	// bar Overdrive lights at stays reachable.
	flow_ = std::min(std::max(100., config_.flow_cap), flow_ + gain);
	// The bar the gauge has to reach is a tuning, not a constant: a draught
	// lowers it. The gauge still caps at 100 and still empties when
	// Overdrive guts out, so a lower bar buys an earlier light, never a
	// standing one.
	if (flow_ >= config_.flow_ignite && overdrive_frames_ == 0) {
		overdrive_frames_ = static_cast<long>(config_.overdrive_secs * 50.);
		cue("overdrive");
	}
}

void Sim::receive_attack (int rows) {
	if (rows <= 0) {
		return;
	}
	// The cold shoulder thins the blow before it queues. A smaller queue
	// also eats less of what this board sends back, since cancellation
	// happens against it - the card defends twice for one slot. The floor
	// of one row is the promise the other side is owed: a blow that landed
	// never weighs nothing.
	const int landed = config_.garbage_scale == 1.0 ? rows
		: std::max(1, py_round(rows * config_.garbage_scale));
	pending_garbage_ += landed;
	// Being hit stokes the fire, where a build asked for it - counted on
	// what actually landed, so a ward that thinned the blow thinned this
	// too and the trade stays honest.
	if (charging() && config_.flow_gain_taken > 0.) {
		fuse_charge(config_.flow_gain_taken * landed);
	}
}

void Sim::impose_gravity (int frames) {
	// The member, not the config: gravity is read from fall_delay_ every
	// step, and a skill that only moved the tuning would be a banner with
	// nothing behind it.
	if (frames >= 1) {
		fall_delay_ = frames;
	}
}

void Sim::drain_flow (double share) {
	flow_ = std::max(0., flow_ * (1. - std::clamp(share, 0., 1.)));
}

void Sim::set_pressure (bool on) {
	// Silent on purpose: a cue raised between steps would be wiped by the
	// next step's clear before anyone drained it. The screen watches the
	// flip and plays the rumble itself.
	pressured_ = on;
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
	// A new piece is a fresh count. Nothing spent on the last one is its fault.
	inputs_ = 0;
	input_log_.clear();
	trail_.clear();
	from_hold_ = false;
	if (lost_) {
		piece_elapsed_.reset();
		if (loss_frame_ < 0) {
			loss_frame_ = frame_;
		}
	} else {
		piece_elapsed_ = 0.;
		if (config_.fuse) {
			fuse_prime();
		}
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
	// A free hand never locks the box, so the swap is allowed again and
	// again - the guard on the stored form still stops a piece swapping
	// with itself.
	if ((!hold_lock_ || config_.free_hold) && stored_ != piece_.form) {
		const bool first_swap = !hold_lock_;
		hold_lock_ = true;
		if (entry_) {
			const int coming = stored_;
			stored_ = piece_.form;
			piece_ = Piece{coming, 0, kSpawnX, kSpawnY};
			cand_x_ = piece_.x;
			// A swap bypasses set_shape, so the timer and the count restart
			// here - except under the fuse, where the burn rides through the
			// swap: a hold that reset it would be a free refill on demand.
			if (!config_.fuse) {
				piece_elapsed_ = 0.;
			}
			// Only the first swap of a piece clears the press record. A
			// free hand that cleared it every time would let a player wash
			// their finesse before every lock; the presses really happened,
			// and the recording says so.
			if (first_swap) {
				inputs_ = 0;
				input_log_.clear();
				trail_.clear();
			}
			from_hold_ = true;
		} else if (!queue_.empty()) {
			std::swap(stored_, queue_.front());
		}
		return true;
	}
	return false;
}

void Sim::spin_key (int turns, const char* pressed) {
	note_input(pressed);
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
	// refused rotations and O turns included. The cue is for the attempt too.
	rotated_last_ = true;
	cue("rotate");
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
				note_input(key == Key::Left ? "left" : "right");
				cand_x_ = piece_.x + shift_dir_;
				// Whether this press disarms the spin is commit_move's call:
				// a press the wall refuses moved nothing.
				// Only the initial press is heard; auto-shift steps are not.
				cue("move");
			}
		} else if (key == Key::Soft) {
			soft_ = true;
			soft_pos_ = piece_.y;
		} else if (key == Key::Hold) {
			if (hold_shape()) {
				cue("hold");
			}
		} else if (entry_) {
			if (key == Key::Ccw) {
				spin_key(3, "ccw");
			} else if (key == Key::Cw) {
				spin_key(1, "cw");
			} else if (key == Key::Flip) {
				spin_key(2, "flip");
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
		// No disarm here: an auto-shift tick held against the wall moves
		// nothing, and a spin set up by holding the key into the stack - the
		// way a human plays a twist - must survive it. commit_move decides.
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
	commit_move();
}

// Prevent the piece from sliding into blocks: the pending candidate either
// commits whole or reverts whole. Split out of eval_shift so a frame that
// carries a burst of events can settle each move before the next event
// reads the piece.
void Sim::commit_move () {
	if (!entry_) {
		return;
	}
	Piece probe = piece_;
	probe.x = cand_x_;
	if (board_.collides(probe) && config_.wrap_walls) {
		// Ring walls: the two edges are one door. A move the WALL refused -
		// the piece already stands against it and asked for one more column
		// - lands it against the far wall instead, which is why a held
		// direction crosses and keeps going. A move a block refused is
		// still refused: the piece must be at the edge it is leaving, so a
		// stack in the way can never be tunnelled through.
		int lo = kWidth;
		int hi = -1;
		for (const Offset cell : cells_of(piece_)) {
			lo = std::min(lo, cell.x);
			hi = std::max(hi, cell.x);
		}
		if (cand_x_ < piece_.x && lo == 0) {
			probe.x = piece_.x + (kWidth - 1 - hi);
		} else if (cand_x_ > piece_.x && hi == kWidth - 1) {
			probe.x = piece_.x - lo;
		}
		if (probe.x != cand_x_ && !board_.collides(probe)) {
			cand_x_ = probe.x;
		}
	}
	probe.x = cand_x_;
	if (board_.collides(probe)) {
		cand_x_ = piece_.x;
	} else {
		if (piece_.x != cand_x_) {
			// A move disarms the spin only when the piece actually went
			// somewhere. Rotations resync cand_x_ on the spot, so a kick's
			// displacement never trips this - and a walk through the ring
			// is a move like any other, so it disarms too.
			rotated_last_ = false;
		}
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

bool Sim::eval_finesse (bool forced) {
	// judge_placement: answered whatever the finesse setting says, because the
	// replay is worth analysing even with the counter switched off.
	last_judged_ = false;
	last_best_.reset();
	if (!forced && !lost_ && piece_.form <= 6 && drop_reachable()) {
		last_best_ = finesse::optimal(piece_.form, piece_.state, piece_.x);
		last_judged_ = last_best_.has_value();
	}
	// Then the rule the player picked: the count and the cue from rule one up,
	// the piece handed back only under retry.
	if (config_.finesse_rule == 0 || !last_judged_) {
		return false;
	}
	if (inputs_ - *last_best_ <= 0) {
		return false;
	}
	cue("finesse");
	if (config_.finesse_rule != 2) {
		return false;
	}
	// Handed back: same piece, fresh spawn, and the time it already spent. A
	// soft drop held through the retry gets no new keypress to re-mark where
	// it started from, and a mark left at the floor would score a climb.
	// The fuse rides through too - set_shape would deal a fresh one and
	// double-draw the bank for a piece that never left play.
	const auto spent = piece_elapsed_;
	const double fuse_total = fuse_total_;
	const double fuse_bank = fuse_bank_;
	set_shape(piece_.form);
	piece_elapsed_ = spent;
	fuse_total_ = fuse_total;
	fuse_bank_ = fuse_bank;
	soft_pos_ = piece_.y;
	return true;
}

void Sim::lock (bool forced, int posdif) {
	// eval_fallen: reset a held direction's charge, score the drop, judge the
	// spin, close the trail, write the placement down, paste, start the
	// clearer.
	if (shift_frame_ == 0 && shift_dir_ != 0) {
		shift_frame_ = shift_delay_ + 1;
	}
	// eval_drop_score: a point for landing, more for the distance dropped -
	// the full rate for a hard drop, a third of it under soft drop or none.
	if (!lost_) {
		if (hard_flag_) {
			score_ += py_round(1.0 + 0.6 * posdif);
			hard_flag_ = false;
		} else {
			score_ += py_round(1.0 + 0.6 * posdif / 3.0);
		}
	}
	// The spin verdict, decided while the piece still stands where it landed
	// and before it becomes part of the stack, as announce_spin decides it.
	int spin = attack::NOT_SPIN;
	auto verdict = spins::judge(
		board_, piece_, static_cast<spins::Rule>(config_.spin_rule),
		rotated_last_, twist_flag_);
	if (config_.wild_spins) {
		// The crooked judge, as a curse: the rulebook is thrown out and
		// nothing replaces it. No spin scores, however honestly it was
		// turned - the back-to-back chain a spin build lives on simply
		// stops arriving, and the board has to be cleared the plain way.
		//
		// It used to mean the opposite (any boxed-in lock counted as a full
		// spin), which on a real stack is most locks: an unbreakable chain
		// and by a distance the strongest effect in the game. The flag's
		// name always described this reading; only the arithmetic caught up.
		verdict = std::nullopt;
	}
	if (verdict.has_value()) {
		spin = verdict->full ? attack::SPIN_FULL : attack::SPIN_MINI;
		tspin_flag_ = true;
		cue("tspin");
	}
	// Closes off the last press of a piece that settled under gravity. A hard
	// drop has already done it, and settle_move only fills a stop still open.
	settle_move();
	// The Flow accounting, while the piece's clock still stands.
	fuse_lock(forced);
	Locked entry{};
	entry.frame = frame_;
	entry.form = piece_.form;
	entry.state = piece_.state;
	entry.x = piece_.x;
	entry.y = piece_.y;
	entry.forced = forced;
	entry.rotated = rotated_last_;
	entry.twist = twist_flag_;
	entry.inputs = inputs_;
	entry.best = last_judged_ ? *last_best_ : -1;
	entry.spin = spin;
	entry.presses = input_log_;
	for (const auto& stop : trail_) {
		if (stop.has_value()) {
			entry.trail.push_back(*stop);
		}
	}
	entry.held = from_hold_;
	entry.stored = stored_;
	for (int slot = 0; slot < 3 && slot < static_cast<int>(queue_.size()); ++slot) {
		entry.queue3[slot] = queue_[slot];
	}
	board_.paste(piece_);
	// The clearer starts here, lazily: its first resume happens later this
	// same frame, in clearing_step, exactly where run() calls next() on it.
	// What it clears - and what that was worth - lands on this entry when the
	// clear resolves.
	line_list_.assign(1, 0);
	clear_phase_ = 0;
	clear_base_ = 0;
	locked_.push_back(std::move(entry));
	clearing_ = true;
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
	// Closed off before the piece is slammed down, so the last press is
	// recorded where the player left the piece standing rather than at the
	// floor. The drop itself is the stop after it.
	settle_move();
	const Piece landed = board_.dropped(piece_);
	const int posdif = landed.y - piece_.y;
	piece_ = landed;
	cand_x_ = piece_.x;
	if (eval_finesse(forced)) {
		// Handed back rather than locked, cue and all.
		return false;
	}
	hard_flag_ = true;
	cue(forced ? "forced" : "drop");
	lock(forced, posdif);
	return true;
}

void Sim::ramp_arcade () {
	// eval_level: the tiers slow the climb down as it goes.
	const int lines = lines_cleared_;
	if (lines <= 640) {
		level_ = 1 + lines / 10;
	} else if (lines <= 1920) {
		level_ = 64 + (lines - 640) / 20;
	} else if (lines <= 3840) {
		level_ = 128 + (lines - 1920) / 30;
	} else if (lines <= 6400) {
		level_ = 192 + (lines - 3840) / 40;
	} else {
		level_ = 256;
	}
	// Only gravity climbs with the level; handling stays the player's.
	fall_delay_ = level_ < 180 ? 45 - (40 * level_ / 180) : 5;
	// From level 64, garbage rows push up from the floor, faster each tier.
	if (level_ >= 64) {
		if (line_frame_ == 0) {
			if (!holes_.empty()) {
				board_.push_garbage(holes_.front());
				holes_.pop_front();
			}
			// Prevent intersections: the piece rides the stack up.
			if (board_.collides(piece_)) {
				piece_.y -= 1;
			}
			if (level_ >= 256) {
				line_frame_ = 120;
			} else if (level_ >= 192) {
				line_frame_ = 180;
			} else if (level_ >= 128) {
				line_frame_ = 240;
			} else {
				line_frame_ = 300;
			}
		} else {
			--line_frame_;
		}
	}
}

void Sim::eval_timer () {
	// The sim's clock is its frame: twenty even milliseconds, the value the
	// engine reads off env.clock. Timed mode notices emptiness one frame
	// after the subtraction that caused it, exactly as eval_timer does.
	if (config_.gametype == 1) {
		if (timer_ms_ > 0) {
			timer_ms_ -= 20;
		} else {
			timer_ms_ = 0;
			if (!lost_) {
				lost_ = true;
				loss_frame_ = frame_;
				piece_elapsed_.reset();
			}
		}
	} else {
		timer_ms_ += 20;
	}
}

void Sim::gravity () {
	const int grav_delay = soft_ ? soft_delay_ : fall_delay_;
	Piece below = piece_;
	below.y += 1;
	if (board_.collides(below)) {
		// Resting: the 30 frame grace, which movement does not reset.
		if (grav_frame_ < 29) {
			++grav_frame_;
		} else {
			grav_frame_ = 0;
			// A piece that settled under gravity is as much the player's
			// placement as one they dropped, so it is judged the same way.
			if (!eval_finesse(false)) {
				cue("lock");
				// The resting piece is its own ghost, so the soft drop's
				// distance is measured from where the mark was left.
				lock(false, soft_ ? piece_.y - soft_pos_ : 0);
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
	// One frame of the clearing block: six frames of sprite animation per
	// pass, then one resume of the clearer - which is one pass of the row
	// scan, or one settle step of the cascade, or the report that it is done.
	// With the clear delay off the whole machine runs to completion inside
	// this one call instead of yielding a frame per stage, so a clearing
	// lock resolves on its lock frame the way a quiet lock always has.
	const bool delayed = config_.clear_delay;
	if (sprite_frames_ > 0) {
		--sprite_frames_;
		return;
	}
	// Without the delay, the per-pass cues would all land on this one frame
	// and play over each other; only the last pass's verdict is voiced.
	std::string last_cue;
	while (true) {
		if (clear_phase_ == 0) {
			int base = 0;
			int dug = 0;
			const int rows = board_.clear_pass(
				config_.cleartype, base, dug, config_.cold_iron);
			downstack_ += dug;
			// Rubble dug out feeds the gauge, where a build asked for it.
			// The road's own rooms are half digging, so this is the faucet
			// that pays for the work the score never noticed.
			if (dug > 0 && charging() && config_.flow_gain_dig > 0.) {
				fuse_charge(config_.flow_gain_dig * dug);
			}
			if (rows > 0) {
				// One yield with sprites: the pass's rows named and heard.
				// Under naive that is one row and a running count - a quad is
				// clear, clear, clear, tetris - and under the cascade styles
				// it is the whole chain link at once.
				line_list_.back() += rows;
				clear_base_ = base;
				if (config_.cleartype >= 1) {
					clear_phase_ = 1;
				}
				if (delayed) {
					sprite_frames_ = 6;
					cue(line_list_.back() > 3 ? "tetris" : "clear");
					return;
				}
				last_cue = line_list_.back() > 3 ? "tetris" : "clear";
				continue;
			}
			// The final yield: the clearer reports done, and the counters are
			// final - which is the moment the placement can be scored in full.
			if (!last_cue.empty()) {
				cue(last_cue);
			}
			// Cold Iron: only now, with every already-frozen row shattered
			// and settled, do the rows this lock completed freeze - so a row
			// never freezes and shatters inside the same lock. The cue is
			// sim-side like the fuse cues: no graded trace runs cold iron.
			if (config_.cold_iron && board_.freeze_full_rows() > 0) {
				cue("freeze");
			}
			clearing_ = false;
			resolve_score();
			return;
		}
		// The settle loop: one row of falling per resume. A step that moves
		// nothing ends the loop and rolls straight into the next row scan,
		// inside the same resume, exactly as the generator falls through.
		if (board_.cascade_step(config_.cleartype, clear_base_)) {
			if (delayed) {
				return;
			}
			continue;
		}
		line_list_.push_back(0);
		clear_phase_ = 0;
	}
}

void Sim::resolve_score () {
	// eval_clear_score, plus the attack sum Core.run makes once the clearer
	// has finished. tspin_flag is still set from the lock: it is cleared on
	// spawn, not at lock, precisely so this late reading works.
	Locked& last = locked_.back();
	int total = 0;
	for (const int chain : line_list_) {
		total += chain;
	}
	last.lines = total;
	if (total > 0) {
		// A quad, or any clear that came out of a spin, carries the back to
		// back chain on. A smaller clear ends it. A placement that clears
		// nothing at all leaves it alone - but does break the combo.
		b2b_ = (tspin_flag_ || total >= 4) ? b2b_ + 1 : 0;
		lines_cleared_ += total;
		// predict_score: each chain link's line value - arcade's level fattens
		// it - then the cascade multiplier per extra link, the combo
		// multiplier as it stood *before* this clear moved the counter, the
		// spin and perfect multipliers, and timed mode's clock bonus, rounded
		// to the nearest fifty. The perfect it pays for is the base game's -
		// the bottom row alone - not the one the banner announces. The
		// trailing zero a cascade's last settle appends is dropped first,
		// as predict_score drops it.
		if (line_list_.back() == 0) {
			line_list_.pop_back();
		}
		// What announce_clear names is the last chain link, not the total.
		last.last_link = line_list_.back();
		double linescore = 500.0;
		if (config_.gametype == 2) {
			linescore += level_ * 2.5;
		}
		double stake = 0.0;
		for (const int chain : line_list_) {
			stake += linescore * chain * (1.0 + 0.8 * (chain - 1));
		}
		stake *= std::pow(1.3, static_cast<double>(line_list_.size() - 1));
		stake *= current_combo_;
		if (twist_flag_) {
			stake *= 2.7;
		}
		if (tspin_flag_) {
			stake *= 1.8;
		}
		bool floor_clear = true;
		for (int x = 0; x < kWidth; ++x) {
			if (board_.at(x, kHeight - 1) >= 0) {
				floor_clear = false;
				break;
			}
		}
		if (floor_clear) {
			stake *= 2.0;
		}
		if (config_.gametype == 1) {
			stake *= 1.0 + (300.0 - static_cast<double>(timer_ms_ / 1000)) / 100.0;
		}
		if (charging() && overdrive_frames_ > 0) {
			stake *= config_.overdrive_mult;
		}
		score_ += static_cast<long long>(py_round_whole(stake / 50.0) * 50.0);
		++combo_;
		current_combo_ = std::pow(1.6, combo_);
	} else {
		combo_ = 0;
		current_combo_ = 1.0;
	}
	const bool perfect = total > 0 && board_.empty();
	const int sent = attack::attack_for(
		total, static_cast<attack::SpinKind>(last.spin), b2b_ > 1,
		std::max(0, combo_ - 1), perfect);
	// The clear pays two different debts. Only a fuse has a bank to refuel,
	// so that half stays behind `fuse`. The gauge is the other half and it
	// answers to quality alone - lines plus the base attack, so spins,
	// quads, back-to-backs and perfect clears are what fill it, not haste -
	// which is true with or without a clock on the piece, so it charges
	// wherever the rail is up. Refuel and Flow read the unboosted attack,
	// or Overdrive would feed itself.
	int boosted = sent;
	if (config_.fuse && total > 0) {
		fuse_bank_ = std::min(config_.fuse_bank_cap,
			fuse_bank_ + config_.fuse_refuel_line * total
				+ config_.fuse_refuel_attack * sent);
	}
	if (charging()) {
		if (total > 0) {
			fuse_charge(config_.flow_gain_line * total
				+ config_.flow_gain_attack * sent);
		}
		if (overdrive_frames_ > 0) {
			boosted = py_round(sent * config_.overdrive_mult);
			// The backdraft: a clear resolved inside Overdrive burns the
			// bottom garbage row off this board too - worth no score and
			// no attack, but the dig is real.
			if (total > 0 && board_.burn_bottom_garbage()) {
				++downstack_;
				++lines_cleared_;
				cue("burn");
			}
			// A wick thick enough to be fed while it burns: every cleared
			// line adds seconds back to the fire. Self-limiting on
			// purpose - it may top the burn back up to a full one, never
			// past it, so the reward is keeping the fire alive rather
			// than owning it.
			if (total > 0 && config_.overdrive_refill > 0.) {
				const long whole = std::lround(config_.overdrive_secs * 50.);
				overdrive_frames_ = std::min(whole, overdrive_frames_
					+ std::lround(config_.overdrive_refill * total * 50.));
			}
		}
	}
	// The floor sweep, outside the rail on purpose: it is a card the board
	// answers to, not the gauge. Only clears made with rubble still down
	// tick the counter, so "every eighth clear" means every eighth clear
	// that had something to sweep - the card face is literally what
	// happens, the way the dice below are.
	if (config_.sweep_every > 0 && total > 0 && board_.garbage_rows() > 0
		&& ++sweep_count_ >= config_.sweep_every) {
		sweep_count_ = 0;
		if (board_.burn_bottom_garbage()) {
			++downstack_;
			++lines_cleared_;
			cue("burn");
		}
	}
	// The card effects on the blow going out: a heavy hand scales it, and
	// loaded dice land every Nth attacking clear double. The crit counter
	// only ticks on clears that actually carry attack, so the promise on
	// the card face - every third strike - is literally what happens.
	if (total > 0 && boosted > 0) {
		if (config_.attack_scale != 1.0) {
			boosted = py_round(boosted * config_.attack_scale);
		}
		if (config_.crit_every > 0
			&& ++crit_count_ >= config_.crit_every) {
			crit_count_ = 0;
			boosted *= 2;
			cue("crit");
		}
	}
	// The turning rack: every clear stirs the hold. A real state change,
	// heard as the hold it is.
	if (config_.hold_churn && total > 0 && stored_ >= 0 && !queue_.empty()) {
		std::swap(stored_, queue_.front());
		cue("hold");
	}
	attack_sent_ += boosted;
	if (config_.gametype == 5) {
		// The versus wire. A clear first banks or fires the surge: a back to
		// back chain four deep banks a row per link, and the clear that
		// breaks the chain fires the bank on top of its own attack. Whatever
		// crosses the wire cancels against the garbage queued at us before
		// the rest goes out; a lock that cleared nothing lets the queue rise.
		if (total > 0) {
			int fired = 0;
			if (b2b_ >= 4) {
				++surge_charge_;
			} else if (b2b_ == 0 && surge_charge_ > 0) {
				fired = surge_charge_;
				surge_charge_ = 0;
			}
			const int wire = boosted + fired;
			const int cancelled = std::min(pending_garbage_, wire);
			pending_garbage_ -= cancelled;
			outgoing_ += wire - cancelled;
		} else {
			apply_pending_garbage();
		}
	}
	// announce_chains, then the perfect on top of them.
	if (total > 0) {
		if (combo_ > 1) {
			cue("combo" + std::to_string(std::min(combo_ - 1, 10)));
		}
		if (b2b_ > 1) {
			cue("b2b");
		}
	}
	if (perfect) {
		cue("perfect");
	}
	last.scored = true;
	last.b2b = b2b_;
	last.combo = combo_;
	last.perfect = perfect;
	last.attack = boosted;
	last.score = score_;
	last.downstack = downstack_;
}

void Sim::apply_pending_garbage () {
	// Up to eight rows of what has been sent at us rise at once, each with
	// the hole the dealer rolled for it. The queue can outlast the dealt
	// holes in a hand-fed test; what cannot rise now stays pending.
	int rising = std::min(pending_garbage_, 8);
	// The flood's own price, charged once for the wave rather than per
	// row: what rises on you costs you the fire you were banking.
	if (rising > 0 && !holes_.empty() && config_.flow_flood_loss > 0.) {
		flow_ = std::max(0., flow_ - config_.flow_flood_loss);
	}
	while (rising > 0 && !holes_.empty()) {
		const int mask = holes_.front() & 0x3FF;
		board_.push_garbage_mask(mask != 0 ? mask : 1 << 4);
		holes_.pop_front();
		--pending_garbage_;
		--rising;
		if (board_.collides(piece_)) {
			piece_.y -= 1;
		}
	}
}

void Sim::eval_cheese () {
	if (config_.gametype == 3) {
		// The race: the board carries up to nine rows of cheese at once; as
		// they are dug, the quota sends more up from the floor, one row a
		// frame, the piece riding the push the way arcade's does.
		if (cheese_left_ > 0 && board_.garbage_rows() < 9 && !holes_.empty()) {
			const int mask = holes_.front() & 0x3FF;
			board_.push_garbage_mask(mask != 0 ? mask : 1 << 4);
			holes_.pop_front();
			--cheese_left_;
			if (board_.collides(piece_)) {
				piece_.y -= 1;
			}
		}
		// Finished when the quota is spent and none of it still stands. The
		// clearer settles first: the last row's cascade may still be falling
		// on the frame the row itself vanished.
		if (!won_ && !lost_ && cheese_left_ == 0 && !clearing_
			&& board_.garbage_rows() == 0) {
			won_ = true;
			loss_frame_ = frame_;
			piece_elapsed_.reset();
		}
	} else if (config_.gametype == 4) {
		// Survival: the floor rises on its own clock, ready or not.
		if (cheese_frame_ > 1) {
			--cheese_frame_;
		} else if (!holes_.empty()) {
			const int mask = holes_.front() & 0x3FF;
			board_.push_garbage_mask(mask != 0 ? mask : 1 << 4);
			holes_.pop_front();
			if (board_.collides(piece_)) {
				piece_.y -= 1;
			}
			cheese_frame_ = std::max(1, config_.cheese_period);
		}
	}
}

bool Sim::step (const std::optional<Event>& event) {
	return step_frame(event.has_value() ? &*event : nullptr,
		event.has_value() ? 1 : 0);
}

bool Sim::step (const std::vector<Event>& events) {
	return step_frame(events.data(), events.size());
}

bool Sim::step_frame (const Event* events, size_t count) {
	if (lost_ || won_) {
		return false;
	}
	cues_.clear();
	// The frame's slice of real time, in the same arithmetic the fake clock
	// hands the Python side: the first frame is worth nothing.
	now_ = frame_ * 0.02;
	double delta = 0.;
	if (have_prev_) {
		delta = std::min(now_ - prev_, 0.1);
	}
	prev_ = now_;
	have_prev_ = true;

	// apply_handling, the half of it that can move: soft drop is a multiple
	// of gravity, so arcade's ramp drags it along a frame behind.
	soft_delay_ = std::max(
		1, py_round(fall_delay_ / static_cast<double>(config_.sdf)));

	// The frame runs to completion even if something in it loses the game -
	// Python's run() does, and the trace counts frames on both sides. A
	// burst of events lands in order, each move committed before the next
	// event reads the piece - the same states a one-per-frame drain would
	// have walked through, minus the wait. A loss mid-burst drops the rest:
	// those presses belong to the game-over screen, not the corpse.
	for (size_t i = 0; i < count && !lost_ && !won_; ++i) {
		eval_input(events[i]);
		if (i + 1 < count) {
			commit_move();
		}
	}
	eval_shift();
	if (!clearing_) {
		if (entry_) {
			bool forced_lock = false;
			if ((config_.forced_delay > 0. || config_.fuse)
				&& piece_elapsed_.has_value()) {
				// Under the fuse the limit is the piece's own fuse, frozen
				// while Overdrive burns; otherwise the trainer's flat delay,
				// arithmetic untouched.
				if (overdrive_frames_ == 0) {
					// Under the other board's heat the fuse burns faster;
					// under your own Overdrive it does not burn at all.
					*piece_elapsed_ += delta
						* (config_.fuse && pressured_
							? config_.fuse_pressure : 1.);
				}
				const double limit
					= config_.fuse ? fuse_total_ : config_.forced_delay;
				if (config_.fuse && !fuse_warned_ && limit > 0.
					&& *piece_elapsed_ >= limit * 0.8) {
					fuse_warned_ = true;
					cue("fusewarn");
				}
				if (limit > 0. && *piece_elapsed_ >= limit) {
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
	// As run() orders it: the arcade ramp and the clock tick between the
	// gravity section and the clearing block, every frame, clearing or not.
	if (config_.gametype == 2) {
		ramp_arcade();
	} else if (config_.gametype == 3 || config_.gametype == 4) {
		eval_cheese();
	}
	// A finish line, if this game has one: crossed once the clearer has
	// settled, the way the cheese race waits for the last cascade to fall
	// before calling itself finished.
	if (config_.line_quota > 0 && !won_ && !lost_ && !clearing_
		&& lines_cleared_ >= config_.line_quota) {
		won_ = true;
		loss_frame_ = frame_;
		piece_elapsed_.reset();
	}
	// The score finish line, same shape: a stage won by points rather than
	// rows, so spins, quads and back-to-backs are the fast way through.
	if (config_.score_quota > 0 && !won_ && !lost_ && !clearing_
		&& score_ >= config_.score_quota) {
		won_ = true;
		loss_frame_ = frame_;
		piece_elapsed_.reset();
	}
	// The clock finish line: outlast the watch and the stage is won.
	if (config_.survive_ms > 0 && !won_ && !lost_ && !clearing_
		&& frame_ * 20 >= config_.survive_ms) {
		won_ = true;
		loss_frame_ = frame_;
		piece_elapsed_.reset();
	}
	// Overdrive burns down in real frames, clearing or not; when it gutters
	// out the gauge starts over from empty.
	if (overdrive_frames_ > 0 && --overdrive_frames_ == 0) {
		// The gauge starts over - or keeps the share a build bought it.
		// Clamped well under the whole, so no stack of cards can leave a
		// fire that relights itself.
		flow_ *= std::clamp(config_.flow_keep, 0., 0.9);
		cue("overdrive_end");
	}
	eval_timer();
	if ((lost_ || won_) && !gameover_cued_) {
		// eval_loss runs between the clock tick and the frame's clearing
		// resume: the gameover cue fires first, and the high score entry and
		// the recorder are finished before a still-resolving clear lands its
		// points - so the counters are written down here, ahead of the
		// resolution below, for the loss screens to read.
		cue("gameover");
		gameover_cued_ = true;
		final_score_ = score_;
		final_lines_ = lines_cleared_;
		final_attack_ = attack_sent_;
		final_downstack_ = downstack_;
	}
	if (clearing_) {
		clearing_step();
	}
	++frame_;
	return !(lost_ || won_);
}

} // namespace forcetris
