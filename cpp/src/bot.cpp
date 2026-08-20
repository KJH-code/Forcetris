#include "forcetris/bot.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>

#include "forcetris/attack.hpp"
#include "forcetris/kicks.hpp"
#include "forcetris/spins.hpp"

namespace forcetris {
namespace bot {

namespace {

// The search space: state, x shifted into range, y shifted for the spawn
// rows above the matrix, and the floor kick allowance.
constexpr int kXPad = 3;
constexpr int kXSpan = kWidth + 2 * kXPad;
constexpr int kYPad = 2;
constexpr int kYSpan = kHeight + kYPad + 1;

struct Node {
	int state = 0;
	int x = 0;
	int y = 0;
	bool fk = true;
};

int node_index (const Node& node) {
	return ((node.state * kXSpan + (node.x + kXPad)) * kYSpan
		+ (node.y + kYPad)) * 2 + (node.fk ? 1 : 0);
}

bool in_space (const Node& node) {
	return node.x >= -kXPad && node.x < kWidth + kXPad
		&& node.y >= -kYPad && node.y <= kHeight;
}

// A placement key: the final cells, normalized, so the same silhouette
// reached as different states of an I or S is one placement.
std::array<int, kCells> cell_key (const Piece& piece) {
	std::array<int, kCells> key{};
	const auto cells = cells_of(piece);
	for (int i = 0; i < kCells; ++i) {
		key[i] = cells[i].y * 64 + cells[i].x;
	}
	std::sort(key.begin(), key.end());
	return key;
}

// The classic Dellacherie reading of a board, computed after the clear.
// Cells outside the walls count filled, above the top empty. A building
// rank reads it differently around wells: one well is not a flaw but the
// point - the deepest run is exempt from the well penalty, and every row
// standing complete except that column is credit in the bank, the way the
// classic open bots reserve a quad well instead of fearing one.
double shape_score (const Board& board, bool build = false) {
	double holes = 0.;
	double col_transitions = 0.;
	double row_transitions = 0.;
	double wells = 0.;
	double deepest_run = 0.;
	int well_column = -1;
	for (int x = 0; x < kWidth; ++x) {
		bool above_filled = false;
		int well_depth = 0;
		double run_value = 0.;
		for (int y = 0; y < kHeight; ++y) {
			const bool filled = board.at(x, y) >= 0;
			const bool up = y == 0 ? false : board.at(x, y - 1) >= 0;
			if (filled != up) {
				col_transitions += 1.;
			}
			if (!filled && above_filled) {
				holes += 1.;
			}
			above_filled = above_filled || filled;
			// A well cell: empty, both sides filled (walls count).
			const bool left = x == 0 || board.at(x - 1, y) >= 0;
			const bool right = x == kWidth - 1 || board.at(x + 1, y) >= 0;
			if (!filled && !above_filled && left && right) {
				++well_depth;
				wells += well_depth;   // 1 + 2 + ... per deepening run.
				run_value += well_depth;
				if (run_value > deepest_run) {
					deepest_run = run_value;
					well_column = x;
				}
			} else if (filled) {
				well_depth = 0;
				run_value = 0.;
			}
		}
		// The floor counts filled.
		if (board.at(x, kHeight - 1) < 0) {
			col_transitions += 1.;
		}
	}
	for (int y = 0; y < kHeight; ++y) {
		bool last = true;   // The left wall.
		for (int x = 0; x < kWidth; ++x) {
			const bool filled = board.at(x, y) >= 0;
			if (filled != last) {
				row_transitions += 1.;
			}
			last = filled;
		}
		if (!last) {
			row_transitions += 1.;   // The right wall.
		}
	}
	double score = -3.2178882868487753 * row_transitions
		+ -9.348695305445199 * col_transitions
		+ -7.899265427351652 * holes
		+ -3.3855972247263626 * wells;
	if (build) {
		// A builder hates holes even more than Dellacherie does: a hole
		// under the stack is rows that can never bank.
		score += -4. * holes;
	}
	if (build && well_column >= 0) {
		// Give the reserved well its penalty back, and pay for the rows
		// banked against it: complete except the well column, counted up
		// from the floor and capped where a quad already pays out.
		score += 3.3855972247263626 * deepest_run;
		int banked = 0;
		for (int y = kHeight - 1; y >= 0 && banked < 8; --y) {
			if (board.at(well_column, y) >= 0) {
				break;
			}
			bool complete = true;
			for (int x = 0; x < kWidth; ++x) {
				if (x != well_column && board.at(x, y) < 0) {
					complete = false;
					break;
				}
			}
			if (!complete) {
				break;
			}
			++banked;
		}
		score += 11. * banked;
	}
	return score;
}

// A standing T-slot: a notch a T could spin into for a double. Coarse on
// purpose - the bonus only has to pull the stacking toward keeping one.
bool t_slot_standing (const Board& board) {
	for (int y = 2; y < kHeight; ++y) {
		for (int x = 1; x + 1 < kWidth; ++x) {
			if (board.at(x, y) >= 0 || board.at(x, y - 1) >= 0) {
				continue;
			}
			if (board.at(x - 1, y) < 0 || board.at(x + 1, y) < 0) {
				continue;
			}
			const bool left_roof = board.at(x - 1, y - 1) >= 0;
			const bool right_roof = board.at(x + 1, y - 1) >= 0;
			if (left_roof == right_roof) {
				continue;
			}
			// The lower row must be one cell short of clearing.
			int missing = 0;
			for (int col = 0; col < kWidth; ++col) {
				if (board.at(col, y) < 0) {
					++missing;
				}
			}
			if (missing == 1) {
				return true;
			}
		}
	}
	return false;
}

// What one placement is worth: the transient half (its clear's real attack,
// surge included, plus the chain bookkeeping) and the counters it leaves for
// the next ply. `board` is consumed: pasted, judged, cleared.
struct Outcome {
	double transient = 0.;
	int b2b = 0;
	int combo = 0;
	int surge = 0;
	int cleared = 0;
	int spin = 0;
};

// Covered empty cells, for telling a dig from a wasted clear.
int count_holes (const Board& board) {
	int holes = 0;
	for (int x = 0; x < kWidth; ++x) {
		bool above = false;
		for (int y = 0; y < kHeight; ++y) {
			const bool filled = board.at(x, y) >= 0;
			if (!filled && above) {
				++holes;
			}
			above = above || filled;
		}
	}
	return holes;
}

Outcome play_out (Board& board, const Plan& plan, const Options& options) {
	Outcome out;
	// How tall the stack already stands: past the danger line the bot stops
	// saving up for quads and digs with whatever clears at all.
	int peak = 0;
	for (int x = 0; x < kWidth; ++x) {
		for (int y = 0; y < kHeight; ++y) {
			if (board.at(x, y) >= 0) {
				peak = std::max(peak, kHeight - y);
				break;
			}
		}
	}
	const bool danger = peak >= 12;
	const int holes_before = options.build ? count_holes(board) : 0;
	// The verdict is judged where the piece rests, before it joins the stack.
	int spin = attack::NOT_SPIN;
	if (plan.rotated_last) {
		const auto verdict = spins::judge(
			board, plan.landed, static_cast<spins::Rule>(options.spin_rule),
			true, plan.kicked_last);
		if (verdict.has_value()) {
			spin = verdict->full ? attack::SPIN_FULL : attack::SPIN_MINI;
		}
	}
	board.paste(plan.landed);
	// Landing height, taken before the clear, as Dellacherie takes it.
	double height_sum = 0.;
	for (const Offset cell : cells_of(plan.landed)) {
		height_sum += kHeight - 1 - cell.y;
	}
	const double landing = height_sum / kCells;
	// Which of the piece's cells sit in rows about to go.
	int eroded_cells = 0;
	for (const Offset cell : cells_of(plan.landed)) {
		if (cell.y < 0) {
			continue;
		}
		bool full = true;
		for (int x = 0; x < kWidth; ++x) {
			if (board.at(x, cell.y) < 0) {
				full = false;
				break;
			}
		}
		if (full) {
			++eroded_cells;
		}
	}
	const int cleared = board.clear_lines();
	out.cleared = cleared;
	out.spin = spin;
	// The chain counters, moved the way resolve_score moves them.
	out.b2b = options.b2b;
	out.combo = options.combo;
	out.surge = options.surge_charge;
	double attack_value = 0.;
	if (cleared > 0) {
		out.b2b = (spin != attack::NOT_SPIN || cleared >= 4)
			? options.b2b + 1 : 0;
		out.combo = options.combo + 1;
		const bool perfect = board.empty();
		int fired = 0;
		if (out.b2b >= 4) {
			out.surge = options.surge_charge + 1;
		} else if (out.b2b == 0 && options.surge_charge > 0) {
			fired = options.surge_charge;
			out.surge = 0;
		}
		attack_value = attack::attack_for(
			cleared, static_cast<attack::SpinKind>(spin), out.b2b > 1,
			std::max(0, out.combo - 1), perfect) + fired;
	} else {
		out.combo = 0;
	}
	// The transient: real attack dominates, the classic transient terms
	// keep the low-rank stacking sane, and the chain is worth keeping -
	// until the stack is tall, when any clear beats a saved-up one. A
	// building rank plays it the way the strong ranks actually play: a
	// clear that is not a quad or a spin is stack spent for nothing and
	// costs, the erosion credit only pays on the clears worth making, and
	// the chain - the surge banked on it above all - is dear to break.
	const bool shaping = options.build && !danger;
	out.transient = attack_value * (danger ? 5. : 12.)
		+ -4.500158825082766 * landing;
	// A small clear that opens a covered hole is a dig, not a waste - the
	// stack cannot bank rows over a hole, so digging promptly is the fast
	// way back to quads.
	const bool digs = shaping && cleared > 0
		&& count_holes(board) < holes_before;
	const bool worth = cleared >= 4 || (cleared > 0 && spin != attack::NOT_SPIN);
	if (!shaping || worth) {
		out.transient += 3.4181268101392694 * (cleared * eroded_cells);
	} else if (cleared > 0) {
		static const double waste[4] = {0., 26., 22., 16.};
		out.transient -= waste[cleared] * (digs ? 0.5 : 1.);
	}
	if (danger) {
		out.transient += cleared * 14.;
	} else if (cleared > 0 && cleared < 4 && spin == attack::NOT_SPIN
		&& options.b2b > 0) {
		// Breaking the chain cheaply.
		out.transient -= shaping
			? 24. + 6. * std::min(options.surge_charge, 4) : 6.;
	}
	if (out.b2b > options.b2b) {
		out.transient += shaping ? 10. : 4.;   // Growing it.
	}
	return out;
}

// Every hard-drop placement of `form`, for the second ply: state times
// column, no routing - the lookahead only needs to know how good the board
// can still be, not how to get there.
double best_drop_score (const Board& board, int form, const Options& options,
                        int b2b, int combo, int surge) {
	double best = -1e18;
	const int states = form == O ? 1 : (form == I || form == S || form == Z)
		? 2 : 4;
	for (int state = 0; state < states; ++state) {
		for (int x = -kXPad; x < kWidth + kXPad; ++x) {
			Piece probe{form, state, x, kSpawnY};
			if (board.collides(probe)) {
				continue;
			}
			const Piece landed = board.dropped(probe);
			Plan plan;
			plan.landed = landed;
			Board after = board;
			Options ply = options;
			ply.b2b = b2b;
			ply.combo = combo;
			ply.surge_charge = surge;
			const Outcome out = play_out(after, plan, ply);
			const double score = out.transient
				+ shape_score(after, options.build);
			if (score > best) {
				best = score;
			}
		}
	}
	return best;
}

// Whether a route does anything after its first drop, and what.
bool moves_after_drop (const std::vector<Move>& route, bool rotations) {
	bool dropped = false;
	for (const Move move : route) {
		if (move == Move::Drop) {
			dropped = true;
		} else if (dropped) {
			const bool rotation = move == Move::Cw || move == Move::Ccw
				|| move == Move::Flip;
			if (rotation == rotations) {
				return true;
			}
		}
	}
	return false;
}

} // namespace

std::vector<Plan> candidates (const Board& board, const Piece& from,
                              bool floor_kick, const Options& options) {
	std::vector<Plan> found;
	if (board.collides(from)) {
		return found;
	}
	// BFS: fewest moves first, so the first route to a placement is the one
	// kept. Parents rebuild the route; the arrival move marks rotations for
	// the spin verdict.
	std::vector<int> parent(4 * kXSpan * kYSpan * 2, -1);
	std::vector<Node> nodes;
	std::vector<Move> arrived;
	std::vector<bool> arrived_kicked;
	std::vector<int> order;
	const Node start{from.state, from.x, from.y, floor_kick};
	parent[node_index(start)] = -2;
	nodes.push_back(start);
	arrived.push_back(Move::Left);   // Unused for the root.
	arrived_kicked.push_back(false);
	order.push_back(0);
	std::map<std::array<int, kCells>, size_t> placements;

	for (size_t at = 0; at < order.size(); ++at) {
		const int here = order[at];
		const Node node = nodes[here];
		const Piece piece{from.form, node.state, node.x, node.y};
		// Resting here is a placement.
		Piece below = piece;
		below.y += 1;
		if (board.collides(below)) {
			const auto key = cell_key(piece);
			if (placements.find(key) == placements.end()) {
				placements[key] = static_cast<size_t>(here);
			}
		}
		const auto push = [&] (const Node& next, Move move, bool kicked) {
			if (!in_space(next)) {
				return;
			}
			const int slot = node_index(next);
			if (parent[slot] != -1) {
				return;
			}
			parent[slot] = here;
			nodes.push_back(next);
			arrived.push_back(move);
			arrived_kicked.push_back(kicked);
			order.push_back(static_cast<int>(nodes.size()) - 1);
		};
		// Taps.
		for (const int dir : {-1, 1}) {
			Piece moved = piece;
			moved.x += dir;
			if (!board.collides(moved)) {
				push(Node{node.state, moved.x, node.y, node.fk},
					dir < 0 ? Move::Left : Move::Right, false);
			}
		}
		// Rotations, through the game's own kicks.
		if (from.form != O) {
			for (const auto& [turns, move] : {
				std::pair<int, Move>{1, Move::Cw},
				std::pair<int, Move>{3, Move::Ccw},
				std::pair<int, Move>{2, Move::Flip}}) {
				const Rotation spun = rotate(
					board, piece, turns, options.kicks, node.fk);
				if (spun.turned) {
					push(Node{spun.piece.state, spun.piece.x, spun.piece.y,
						spun.floor_kick}, move, spun.kicked);
				}
			}
		}
		// The sonic drop.
		const Piece rest = board.dropped(piece);
		if (rest.y != piece.y) {
			push(Node{node.state, node.x, rest.y, node.fk},
				Move::Drop, false);
		}
	}

	for (const auto& [key, index] : placements) {
		(void) key;
		Plan plan;
		int walk = static_cast<int>(index);
		std::vector<Move> route;
		bool last_set = false;
		while (parent[node_index(nodes[walk])] != -2) {
			const Move move = arrived[walk];
			if (!last_set) {
				plan.rotated_last = move == Move::Cw || move == Move::Ccw
					|| move == Move::Flip;
				plan.kicked_last = plan.rotated_last && arrived_kicked[walk];
				last_set = true;
			}
			route.push_back(move);
			walk = parent[node_index(nodes[walk])];
		}
		std::reverse(route.begin(), route.end());
		const Node& landed = nodes[index];
		plan.landed = Piece{from.form, landed.state, landed.x, landed.y};
		plan.route = std::move(route);
		if (!options.tucks && moves_after_drop(plan.route, false)) {
			continue;
		}
		if (!options.spins && moves_after_drop(plan.route, true)) {
			continue;
		}
		// A route must end grounded: if the last move was not a drop and
		// the piece floats mid-route... it cannot - placements are only
		// collected at resting nodes. But a placement whose route is empty
		// (the spawn itself resting) still needs the hard drop only.
		found.push_back(std::move(plan));
	}
	return found;
}

std::vector<Plan> plan (const Board& board, const Piece& piece,
                        bool floor_kick, int hold,
                        const std::deque<int>& queue,
                        const Options& options) {
	std::vector<Plan> ranked;
	const auto consider = [&] (bool use_hold, int form, int next_form) {
		Piece from = piece;
		from.form = form;
		if (use_hold) {
			// A swap re-spawns the piece; holding costs its own key.
			from = Piece{form, 0, kSpawnX, kSpawnY};
		}
		for (Plan candidate : candidates(board, from, floor_kick, options)) {
			candidate.use_hold = use_hold;
			Board after = board;
			const Outcome out = play_out(after, candidate, options);
			candidate.cleared = out.cleared;
			candidate.spin = out.spin;
			double score = out.transient;
			if (options.depth >= 2 && next_form >= 0) {
				score += best_drop_score(
					after, next_form, options, out.b2b, out.combo, out.surge);
			} else {
				score += shape_score(after, options.build);
			}
			// Keep a T-spin slot standing when a T is on its way.
			const bool t_coming = hold == T
				|| (!queue.empty() && queue.front() == T);
			if (options.spins && t_coming && t_slot_standing(after)) {
				score += options.build ? 16. : 6.;
			}
			candidate.score = score;
			ranked.push_back(std::move(candidate));
		}
	};
	const int next = queue.empty() ? -1 : queue.front();
	consider(false, piece.form, next);
	if (hold >= 0) {
		if (hold != piece.form) {
			consider(true, hold, next);
		}
	} else if (next >= 0) {
		// Holding with an empty box plays the queue's head; the ply after
		// that is the next preview along.
		const int after_next = queue.size() > 1 ? queue[1] : -1;
		consider(true, next, after_next);
	}
	std::sort(ranked.begin(), ranked.end(),
		[] (const Plan& a, const Plan& b) { return a.score > b.score; });
	return ranked;
}

const std::vector<Rank>& ranks () {
	// Real Tetra League averages per rank; the upper half plays the way the
	// upper half actually plays.
	static const std::vector<Rank> table = {
		{"D", 0.71, 0.25, 1, false, false, false},
		{"C", 0.88, 0.18, 1, false, false, false},
		{"B", 1.06, 0.12, 1, true, false, false},
		{"A", 1.27, 0.08, 1, true, false, true},
		{"S", 1.59, 0.05, 2, true, true, true},
		{"SS", 1.97, 0.03, 2, true, true, true},
		{"U", 2.33, 0.015, 2, true, true, true},
		{"X", 2.83, 0.005, 2, true, true, true},
	};
	return table;
}

Driver::Driver (unsigned seed, const Rank& rank)
	: rng_(seed), rank_(rank) {}

void Driver::adopt (const Sim& sim) {
	Options options;
	options.depth = rank_.depth;
	options.tucks = rank_.tucks;
	options.spins = rank_.spins;
	options.kicks = sim.config().kicks;
	options.spin_rule = sim.config().spin_rule;
	options.b2b = sim.b2b();
	options.combo = sim.combo();
	options.surge_charge = sim.surge_charge();
	options.build = rank_.build;
	const auto ranked = plan(
		sim.board(), sim.piece(), sim.floor_kick(), sim.stored(),
		sim.queue(), options);
	script_.clear();
	cursor_ = 0;
	planned_locks_ = sim.locked().size();
	live_ = !ranked.empty();
	if (!live_) {
		return;
	}
	size_t pick = 0;
	if (rank_.blunder > 0.
		&& std::uniform_real_distribution<double>(0., 1.)(rng_)
			< rank_.blunder) {
		pick = 1 + std::uniform_int_distribution<size_t>(0, 2)(rng_);
		pick = std::min(pick, ranked.size() - 1);
	}
	current_ = ranked[pick];
	// The keystrokes: hold, then the route - taps as press and release,
	// rotations as a press, drops as a soft tap (the driver's sdf 40 makes
	// one press a sonic drop) - and the hard drop to finish.
	if (current_.use_hold) {
		script_.push_back(Event{Key::Hold, true});
		script_.push_back(Event{Key::Hold, false});
	}
	for (const Move move : current_.route) {
		switch (move) {
			case Move::Left:
				script_.push_back(Event{Key::Left, true});
				script_.push_back(Event{Key::Left, false});
				break;
			case Move::Right:
				script_.push_back(Event{Key::Right, true});
				script_.push_back(Event{Key::Right, false});
				break;
			case Move::Cw:
				script_.push_back(Event{Key::Cw, true});
				script_.push_back(Event{Key::Cw, false});
				break;
			case Move::Ccw:
				script_.push_back(Event{Key::Ccw, true});
				script_.push_back(Event{Key::Ccw, false});
				break;
			case Move::Flip:
				script_.push_back(Event{Key::Flip, true});
				script_.push_back(Event{Key::Flip, false});
				break;
			case Move::Drop:
				script_.push_back(Event{Key::Soft, true});
				script_.push_back(Event{Key::Soft, false});
				break;
		}
	}
	script_.push_back(Event{Key::Hard, true});
	// The pace: think first, then type briskly - the tail of a route can sit
	// on a resting piece whose lock grace is already running, so the typing
	// must never dawdle. The 0.93 pays for the clear animations.
	const long frames = std::lround(0.93 * 50. / rank_.pps);
	const long typing = static_cast<long>(script_.size());
	due_frame_ = sim.frame() + std::max(1L, frames - typing);
}

std::optional<Event> Driver::next (const Sim& sim) {
	if (!sim.entry()) {
		return std::nullopt;
	}
	if (!live_ || planned_locks_ != sim.locked().size()
		|| cursor_ >= script_.size()) {
		adopt(sim);
	}
	if (!live_ || cursor_ >= script_.size()) {
		return std::nullopt;
	}
	if (sim.frame() < due_frame_) {
		return std::nullopt;
	}
	return script_[cursor_++];
}

} // namespace bot
} // namespace forcetris
