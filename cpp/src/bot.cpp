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
	// The two terms below are gated on the beam (depth >= 3) on purpose:
	// the depth <= 2 evaluation is pinned by botcheck's builder checks and
	// calibrated floors, and enabling these there means re-calibrating all
	// of that. Widen the gate only with the pins in hand.
	if (options.depth >= 3) {
		// Banking a surge row is worth chasing at depth - this is what
		// makes a chain of small spin clears read as the battery it is.
		if (shaping && out.surge > options.surge_charge) {
			out.transient += 8.;
		}
		// A stack shadowing the spawn is a round about to end: dwarf every
		// other consideration without going infinite, so among doomed
		// continuations the least-buried still orders.
		bool blocked = false;
		for (int x = kSpawnX - 2; x <= kSpawnX + 2 && !blocked; ++x) {
			for (int y = 0; y <= kSpawnY + 2; ++y) {
				if (board.at(x, y) >= 0) {
					blocked = true;
					break;
				}
			}
		}
		if (blocked) {
			out.transient -= 1000.;
		}
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

// The reachability search itself, shared by the routed planner and the
// beam's route-free one. The arena is reused call to call - a beam plan
// runs this dozens of times a piece, and the allocations were the cost.
struct Reach {
	std::vector<int> parent;
	std::vector<Node> nodes;
	std::vector<Move> arrived;
	std::vector<bool> arrived_kicked;
	std::vector<int> order;
	std::map<std::array<int, kCells>, size_t> placements;
};

Reach& reach_scratch () {
	static Reach reach;
	return reach;
}

// BFS: fewest moves first, so the first route to a placement is the one
// kept. Parents rebuild the route; the arrival move marks rotations for
// the spin verdict. Returns false when the start itself collides.
bool search_reach (const Board& board, const Piece& from, bool floor_kick,
                   const Options& options, Reach& reach) {
	reach.parent.assign(4 * kXSpan * kYSpan * 2, -1);
	reach.nodes.clear();
	reach.arrived.clear();
	reach.arrived_kicked.clear();
	reach.order.clear();
	reach.placements.clear();
	if (board.collides(from)) {
		return false;
	}
	const Node start{from.state, from.x, from.y, floor_kick};
	reach.parent[node_index(start)] = -2;
	reach.nodes.push_back(start);
	reach.arrived.push_back(Move::Left);   // Unused for the root.
	reach.arrived_kicked.push_back(false);
	reach.order.push_back(0);

	for (size_t at = 0; at < reach.order.size(); ++at) {
		const int here = reach.order[at];
		const Node node = reach.nodes[here];
		const Piece piece{from.form, node.state, node.x, node.y};
		// Resting here is a placement.
		Piece below = piece;
		below.y += 1;
		if (board.collides(below)) {
			const auto key = cell_key(piece);
			if (reach.placements.find(key) == reach.placements.end()) {
				reach.placements[key] = static_cast<size_t>(here);
			}
		}
		const auto push = [&] (const Node& next, Move move, bool kicked) {
			if (!in_space(next)) {
				return;
			}
			const int slot = node_index(next);
			if (reach.parent[slot] != -1) {
				return;
			}
			reach.parent[slot] = here;
			reach.nodes.push_back(next);
			reach.arrived.push_back(move);
			reach.arrived_kicked.push_back(kicked);
			reach.order.push_back(
				static_cast<int>(reach.nodes.size()) - 1);
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
	return true;
}

// A reachable resting place without its route: what the beam's inner plies
// need. No route walk, no per-placement allocation - the arrival move alone
// carries the spin flags. Route-gated rank limits (tucks, spins) are not
// applied here; the beam ranks all have both.
struct Landing {
	Piece landed;
	bool rotated_last = false;
	bool kicked_last = false;
};

std::vector<Landing> landings (const Board& board, const Piece& from,
                               bool floor_kick, const Options& options) {
	std::vector<Landing> found;
	Reach& reach = reach_scratch();
	if (!search_reach(board, from, floor_kick, options, reach)) {
		return found;
	}
	found.reserve(reach.placements.size());
	for (const auto& [key, index] : reach.placements) {
		(void) key;
		Landing landing;
		const Node& landed = reach.nodes[index];
		landing.landed = Piece{from.form, landed.state, landed.x, landed.y};
		const Move move = reach.arrived[index];
		landing.rotated_last = index != 0
			&& (move == Move::Cw || move == Move::Ccw || move == Move::Flip);
		landing.kicked_last = landing.rotated_last
			&& reach.arrived_kicked[index];
		found.push_back(landing);
	}
	return found;
}

std::vector<Plan> candidates (const Board& board, const Piece& from,
                              bool floor_kick, const Options& options) {
	std::vector<Plan> found;
	Reach& reach = reach_scratch();
	if (!search_reach(board, from, floor_kick, options, reach)) {
		return found;
	}

	for (const auto& [key, index] : reach.placements) {
		(void) key;
		Plan plan;
		int walk = static_cast<int>(index);
		std::vector<Move> route;
		bool last_set = false;
		while (reach.parent[node_index(reach.nodes[walk])] != -2) {
			const Move move = reach.arrived[walk];
			if (!last_set) {
				plan.rotated_last = move == Move::Cw || move == Move::Ccw
					|| move == Move::Flip;
				plan.kicked_last = plan.rotated_last
					&& reach.arrived_kicked[walk];
				last_set = true;
			}
			route.push_back(move);
			walk = reach.parent[node_index(reach.nodes[walk])];
		}
		std::reverse(route.begin(), route.end());
		const Node& landed = reach.nodes[index];
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

namespace {

// Later plies are less certain - garbage lands, the plan is remade every
// lock, the queue past the previews is unknown - so their worth decays:
// a real chain still dominates, but the same attack sooner wins ties.
constexpr double kDamp = 0.95;

// One line of play the beam is carrying: the board as it stands after the
// path so far, the chain counters, the piece supply, and which root
// placement the whole line hangs from.
struct BeamNode {
	Board board;
	double sum = 0.;      // Damped transient total along the path.
	double score = 0.;    // sum + damped shape: the selection key.
	int b2b = 0;
	int combo = 0;
	int surge = 0;
	int hold = -1;
	int next_at = 0;      // The queue index the next draw comes from.
	int root = -1;        // Index into the ply-one ranked vector.
};

// The beam: full reachability at every ply - so a spin set up now and hit
// two pieces later is seen, which no hard-drop lookahead can do - with the
// hold considered at every step, pruned to a fixed width. Deterministic:
// no clocks, no dice, stable ordering throughout.
std::vector<Plan> beam_plan (const Board& board, const Piece& piece,
                             bool floor_kick, int hold,
                             const std::deque<int>& queue,
                             const Options& options) {
	std::vector<Plan> ranked;
	std::vector<BeamNode> beam;
	// Ply one is the routed planner's own expansion - these routes are the
	// ones the driver will type.
	const auto consider = [&] (bool use_hold, int form, int held_after,
	                           int next_at) {
		Piece from = piece;
		from.form = form;
		if (use_hold) {
			from = Piece{form, 0, kSpawnX, kSpawnY};
		}
		for (Plan candidate : candidates(board, from, floor_kick, options)) {
			candidate.use_hold = use_hold;
			Board after = board;
			const Outcome out = play_out(after, candidate, options);
			candidate.cleared = out.cleared;
			candidate.spin = out.spin;
			BeamNode node;
			node.sum = out.transient;
			node.score = node.sum
				+ kDamp * shape_score(after, options.build);
			node.board = after;
			node.b2b = out.b2b;
			node.combo = out.combo;
			node.surge = out.surge;
			node.hold = held_after;
			node.next_at = next_at;
			node.root = static_cast<int>(ranked.size());
			candidate.score = node.score;
			ranked.push_back(std::move(candidate));
			beam.push_back(std::move(node));
		}
	};
	const int next = queue.empty() ? -1 : queue.front();
	consider(false, piece.form, hold, 0);
	if (hold >= 0) {
		if (hold != piece.form) {
			consider(true, hold, piece.form, 0);
		}
	} else if (next >= 0) {
		consider(true, next, piece.form, 1);
	}
	if (ranked.empty()) {
		return ranked;
	}

	// Every root starts a long way down; a beam-explored line lifts its
	// root above all unexplored ones while their relative order holds, so
	// the blunder picks stay sane even when few roots survive the width.
	std::vector<double> best(ranked.size());
	for (size_t i = 0; i < ranked.size(); ++i) {
		best[i] = ranked[i].score - 1e6;
	}
	const auto by_score = [] (const BeamNode& a, const BeamNode& b) {
		if (a.score != b.score) {
			return a.score > b.score;
		}
		return a.root < b.root;
	};
	const int width = std::max(1, options.width);
	std::stable_sort(beam.begin(), beam.end(), by_score);
	if (static_cast<int>(beam.size()) > width) {
		beam.resize(width);
	}

	// A line's final word. The T-slot bonus lands only here, and only when
	// the T is beyond the horizon - within it the beam sees the actual
	// spin, and paying twice would double-count.
	const auto finalize = [&] (const BeamNode& node, int ply) {
		double score = node.score;
		bool t_beyond = node.hold == T;
		for (size_t at = node.next_at;
			at < queue.size() && !t_beyond; ++at) {
			t_beyond = queue[at] == T;
		}
		if (options.spins && t_beyond && t_slot_standing(node.board)) {
			score += std::pow(kDamp, ply) * 16.;
		}
		if (score > best[node.root]) {
			best[node.root] = score;
		}
	};

	std::vector<BeamNode> children;
	for (int ply = 2; ply <= options.depth && !beam.empty(); ++ply) {
		children.clear();
		const double damp_sum = std::pow(kDamp, ply - 1);
		const double damp_shape = std::pow(kDamp, ply);
		for (const BeamNode& node : beam) {
			if (node.next_at >= static_cast<int>(queue.size())) {
				finalize(node, ply - 1);   // The queue ran dry: a leaf.
				continue;
			}
			const int form = queue[node.next_at];
			const size_t had = children.size();
			const auto expand = [&] (int play_form, int held_after,
			                         int next_at) {
				const Piece from{play_form, 0, kSpawnX, kSpawnY};
				for (const Landing& landing
					: landings(node.board, from, true, options)) {
					Plan shim;
					shim.landed = landing.landed;
					shim.rotated_last = landing.rotated_last;
					shim.kicked_last = landing.kicked_last;
					Board after = node.board;
					Options counters = options;
					counters.b2b = node.b2b;
					counters.combo = node.combo;
					counters.surge_charge = node.surge;
					const Outcome out = play_out(after, shim, counters);
					BeamNode child;
					child.sum = node.sum + damp_sum * out.transient;
					child.score = child.sum
						+ damp_shape * shape_score(after, options.build);
					child.board = after;
					child.b2b = out.b2b;
					child.combo = out.combo;
					child.surge = out.surge;
					child.hold = held_after;
					child.next_at = next_at;
					child.root = node.root;
					children.push_back(std::move(child));
				}
			};
			expand(form, node.hold, node.next_at + 1);
			if (node.hold >= 0) {
				if (node.hold != form) {
					expand(node.hold, form, node.next_at + 1);
				}
			} else if (node.next_at + 1 < static_cast<int>(queue.size())
				&& queue[node.next_at + 1] != form) {
				expand(queue[node.next_at + 1], form, node.next_at + 2);
			}
			if (children.size() == had) {
				// Nowhere to put anything: the spawn is buried. The line
				// still reports, as the disaster it is.
				if (node.sum - 1e9 > best[node.root]) {
					best[node.root] = node.sum - 1e9;
				}
			}
		}
		if (ply == options.depth) {
			for (const BeamNode& child : children) {
				finalize(child, ply);
			}
			beam.clear();
		} else {
			std::stable_sort(children.begin(), children.end(), by_score);
			if (static_cast<int>(children.size()) > width) {
				children.resize(width);
			}
			beam.swap(children);
		}
	}

	for (size_t i = 0; i < ranked.size(); ++i) {
		ranked[i].score = best[i];
	}
	std::stable_sort(ranked.begin(), ranked.end(),
		[] (const Plan& a, const Plan& b) { return a.score > b.score; });
	return ranked;
}

} // namespace

std::vector<Plan> plan (const Board& board, const Piece& piece,
                        bool floor_kick, int hold,
                        const std::deque<int>& queue,
                        const Options& options) {
	if (options.depth >= 3) {
		return beam_plan(board, piece, floor_kick, hold, queue, options);
	}
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
	// Real Tetra League averages per rank from D up; the upper half plays
	// the way the upper half actually plays.
	//
	// F and E are below the league. D is a real TL average and a real TL
	// average is already someone who plays tetris - it out-paces a person
	// meeting the game this week, which left the gentlest fire with no foe
	// a beginner could actually beat. These two are that foe: slow hands
	// above all, because blunder only demotes the bot to its second-best
	// placement and a second-best placement still stacks fine. Pace is the
	// lever that decides whether a new player gets to finish a thought.
	static const std::vector<Rank> table = {
		{"F", 0.45, 0.44, 1, false, false, false},
		{"E", 0.58, 0.34, 1, false, false, false},
		{"D", 0.71, 0.25, 1, false, false, false},
		{"C", 0.88, 0.18, 1, false, false, false},
		{"B", 1.06, 0.12, 1, true, false, false},
		{"A", 1.27, 0.08, 1, true, false, true},
		{"S", 1.59, 0.05, 2, true, true, true, 0},
		{"SS", 1.97, 0.03, 3, true, true, true, 12},
		{"U", 2.33, 0.015, 3, true, true, true, 16},
		{"X", 2.83, 0.005, 3, true, true, true, 24},
	};
	return table;
}

const char* might_of (int rank_index) {
	// One word a rung, climbing the way the ladder does: the two below the
	// league are unfinished, the middle is worked metal, and the top is
	// the stuff of stories.
	static const char* words[] = {
		"Half-forged", "Green", "Rough", "Tempered", "Keen",
		"Honed", "Master", "Grandmaster", "Peerless", "Godmetal",
	};
	const int last = static_cast<int>(ranks().size()) - 1;
	const int at = std::clamp(rank_index, 0, last);
	// The table is written against the ladder; if the ladder ever grows
	// past it, the last word carries rather than reading off the end.
	const int words_last
		= static_cast<int>(sizeof words / sizeof words[0]) - 1;
	return words[std::min(at, words_last)];
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
	if (rank_.width > 0) {
		options.width = rank_.width;
	}
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
	// must never dawdle. The 0.93 pays for the clear animations; with the
	// clear delay off there is nothing to pay for, and without the refund
	// the bot would run visibly above its rank's dial.
	const double pay = sim.config().clear_delay ? 0.93 : 1.0;
	const long frames = std::lround(pay * 50. / rank_.pps);
	const long typing = static_cast<long>(script_.size());
	due_frame_ = sim.frame() + std::max(1L, frames - typing);
	// Under the fuse the thinking must fit inside the burn: a rank slower
	// than the fuse types early instead of being slammed mid-plan, the way
	// a rushed player abandons their pace rather than their piece. Two
	// frames of slack cover the lock itself.
	if (sim.config().fuse && sim.fuse_total() > 0.
		&& !sim.overdrive()) {
		const double spent = sim.piece_elapsed().value_or(0.);
		const long left = std::lround((sim.fuse_total() - spent) * 50.);
		due_frame_ = std::min(due_frame_,
			sim.frame() + std::max(1L, left - typing - 2));
	}
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
