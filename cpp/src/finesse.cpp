#include "forcetris/finesse.hpp"

#include <algorithm>
#include <deque>
#include <unordered_map>

namespace forcetris {
namespace finesse {
namespace {

constexpr int kSpawnState = 0;

int turn_of (Move move) {
	switch (move) {
		case Move::Cw: return 1;
		case Move::Flip: return 2;
		case Move::Ccw: return 3;
		default: return 0;
	}
}

struct Step {
	Move move;
	int state;
	int x;
};

// Every state one press can reach from this one, and the name of the press.
std::vector<Step> moves (int form, int state, int x) {
	std::vector<Step> found;
	const std::pair<int, std::pair<Move, Move>> sides[] = {
		{-1, {Move::Left, Move::DasLeft}},
		{ 1, {Move::Right, Move::DasRight}},
	};
	for (const auto& [step, names] : sides) {
		// A held key, which auto-shift walks into the wall off the one press.
		int wall = x;
		while (fits(form, state, wall + step)) {
			wall += step;
		}
		if (wall != x) {
			found.push_back({names.second, state, wall});
		}
		if (fits(form, state, x + step) && x + step != wall) {
			// A tap. Skipped when one column is the whole way to the wall, since the
			// two presses would be the same move under two names.
			found.push_back({names.first, state, x + step});
		}
	}
	for (const Move move : {Move::Cw, Move::Ccw, Move::Flip}) {
		const int spun = turned(state, turn_of(move));
		if (fits(form, spun, x)) {
			found.push_back({move, spun, x});
		}
	}
	return found;
}

std::map<Placement, Route> build (int form) {
	// Breadth first from spawn, so the first route to reach a state is a shortest
	// one and ties fall to whichever move `moves` offers first.
	std::unordered_map<int, Route> routes;
	const auto key = [] (int state, int x) { return state * 64 + (x + 8); };
	std::deque<std::pair<int, int>> queue;
	routes[key(kSpawnState, kSpawnX)] = {};
	queue.push_back({kSpawnState, kSpawnX});
	std::vector<std::pair<int, std::pair<int, int>>> reached;
	reached.push_back({0, {kSpawnState, kSpawnX}});
	while (!queue.empty()) {
		const auto [state, x] = queue.front();
		queue.pop_front();
		const Route here = routes[key(state, x)];
		for (const Step& step : moves(form, state, x)) {
			const int at = key(step.state, step.x);
			if (routes.count(at) != 0) {
				continue;
			}
			Route next = here;
			next.push_back(step.move);
			reached.push_back({static_cast<int>(next.size()), {step.state, step.x}});
			routes[at] = std::move(next);
			queue.push_back({step.state, step.x});
		}
	}
	// Several ways of holding the piece land on the same placement, and the
	// cheapest of them is the one the player is held to. Sorted by length so the
	// shortest route to a placement is the one kept.
	std::stable_sort(reached.begin(), reached.end(),
		[] (const auto& a, const auto& b) { return a.first < b.first; });
	std::map<Placement, Route> best;
	for (const auto& [cost, at] : reached) {
		(void) cost;
		const Placement where = placement(form, at.first, at.second);
		best.emplace(where, routes[key(at.first, at.second)]);
	}
	return best;
}

} // namespace

const char* name (Move move) {
	switch (move) {
		case Move::DasLeft: return "das_left";
		case Move::DasRight: return "das_right";
		case Move::Left: return "left";
		case Move::Right: return "right";
		case Move::Cw: return "cw";
		case Move::Ccw: return "ccw";
		case Move::Flip: return "flip";
	}
	return "?";
}

const char* label (Move move) {
	switch (move) {
		case Move::DasLeft: return "Hold Left";
		case Move::DasRight: return "Hold Right";
		case Move::Left: return "Tap Left";
		case Move::Right: return "Tap Right";
		case Move::Cw: return "Rotate CW";
		case Move::Ccw: return "Rotate CCW";
		case Move::Flip: return "Rotate 180";
	}
	return "?";
}

std::optional<Move> from_name (const std::string& text) {
	for (const Move move : {Move::DasLeft, Move::DasRight, Move::Left, Move::Right,
	                        Move::Cw, Move::Ccw, Move::Flip}) {
		if (text == name(move)) {
			return move;
		}
	}
	return std::nullopt;
}

bool fits (int form, int state, int x) {
	for (const Offset cell : offsets(form, state)) {
		const int at = x + cell.x;
		if (at < 0 || at >= kWidth) {
			return false;
		}
	}
	return true;
}

Placement placement (int form, int state, int x) {
	const Cells& cells = offsets(form, state);
	int floor = cells[0].y;
	for (const Offset cell : cells) {
		floor = std::min(floor, cell.y);
	}
	Placement where;
	where.reserve(kCells);
	for (const Offset cell : cells) {
		where.push_back(Offset{x + cell.x, cell.y - floor});
	}
	std::sort(where.begin(), where.end());
	return where;
}

const std::map<Placement, Route>& table (int form) {
	// Built on first use, one per piece.
	static std::array<std::map<Placement, Route>, kForms> tables;
	static std::array<bool, kForms> ready{};
	static const std::map<Placement, Route> none;
	if (form < 0 || form >= kForms) {
		return none;
	}
	if (!ready[form]) {
		tables[form] = build(form);
		ready[form] = true;
	}
	return tables[form];
}

std::optional<Route> route (int form, int state, int x) {
	if (form < 0 || form >= kForms) {
		return std::nullopt;
	}
	const auto& built = table(form);
	const auto found = built.find(placement(form, state, x));
	if (found == built.end()) {
		return std::nullopt;
	}
	return found->second;
}

std::optional<int> optimal (int form, int state, int x) {
	const auto best = route(form, state, x);
	if (!best.has_value()) {
		return std::nullopt;
	}
	return static_cast<int>(best->size());
}

std::vector<std::pair<int, int>> follow (int form, const Route& presses) {
	int state = kSpawnState;
	int x = kSpawnX;
	std::vector<std::pair<int, int>> stops;
	stops.reserve(presses.size());
	for (const Move move : presses) {
		if (const int turn = turn_of(move); turn != 0) {
			if (const int spun = turned(state, turn); fits(form, spun, x)) {
				state = spun;
			}
		} else if (move == Move::Left || move == Move::Right) {
			const int step = move == Move::Left ? -1 : 1;
			if (fits(form, state, x + step)) {
				x += step;
			}
		} else {
			const int step = move == Move::DasLeft ? -1 : 1;
			while (fits(form, state, x + step)) {
				x += step;
			}
		}
		stops.push_back({state, x});
	}
	return stops;
}

std::string describe (const Route& presses) {
	if (presses.empty()) {
		return "nothing";
	}
	std::string text;
	for (size_t i = 0; i < presses.size(); ++i) {
		if (i != 0) {
			text += " + ";
		}
		text += label(presses[i]);
	}
	return text;
}

} // namespace finesse
} // namespace forcetris
