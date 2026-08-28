// The map generator: a chapter turned into a seeded climb.
//
// The whole graph is a pure function of (chapter, seed) - no state, no
// clock - so the save file only ever stores those two numbers and the
// path picked, and campaigncheck can grade the generator's promises
// (shape, connectivity, non-crossing edges) across many seeds without a
// window. The randomness is a local xorshift the way temper.cpp mixes
// its offers, never a std engine whose sequence this file doesn't own.
#include "forcetris/campaign.hpp"

#include <algorithm>

namespace forcetris {
namespace campaign {

namespace {

// A small deterministic generator, seeded from the run's seed and the
// chapter so two chapters under one seed still climb different maps.
struct Roll {
	unsigned state;
	explicit Roll (unsigned seed, int chapter)
		: state((seed ^ 0x9e3779b9u) + 0x85ebca6bu
			* static_cast<unsigned>(chapter + 1)) {
		if (state == 0) {
			state = 0x2545f491u;
		}
	}
	unsigned next () {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	}
	// Uniform in [0, count).
	int below (int count) { return static_cast<int>(next() % count); }
};

} // namespace

std::vector<MapNode> build_map (int chapter, unsigned seed) {
	std::vector<MapNode> nodes;
	const int chapter_count = static_cast<int>(chapters().size());
	if (chapter < 0 || chapter >= chapter_count) {
		return nodes;
	}
	Roll roll(seed, chapter);

	// Where this chapter's recipes live in the flat table, and how many
	// of them are battles - everything but the boss at the end.
	int base = 0;
	for (int c = 0; c < chapter; ++c) {
		base += chapters()[static_cast<size_t>(c)].stages;
	}
	const int count = chapters()[static_cast<size_t>(chapter)].stages;
	const int battles = std::max(1, count - 1);
	const int boss = base + count - 1;

	// The rows: two doors at the entrance, two or three lanes through the
	// middle, the boss alone at the top.
	int widths[kMapDepth];
	widths[0] = 2;
	widths[kMapDepth - 1] = 1;
	for (int r = 1; r < kMapDepth - 1; ++r) {
		widths[r] = 2 + roll.below(2);
	}

	// The nodes, row by row. A battle row picks its recipes from a window
	// that slides up the chapter's table as the climb rises - the table's
	// order is its difficulty order - so the easy fires sit at the gate
	// and the hard ones under the boss. Repeats are allowed: the pool is
	// small and two doors to the same fire are still a choice of path.
	int row_at[kMapDepth];
	for (int r = 0; r < kMapDepth; ++r) {
		row_at[r] = static_cast<int>(nodes.size());
		for (int lane = 0; lane < widths[r]; ++lane) {
			MapNode node;
			node.depth = r;
			node.lane = lane;
			if (r == kMapDepth - 1) {
				node.kind = 1;
				node.stage = boss;
			} else {
				node.kind = 0;
				const int centre = battles > 1
					? r * (battles - 1) / (kMapDepth - 2) : 0;
				const int lo = std::max(0, centre - 1);
				const int hi = std::min(battles - 1, centre + 1);
				node.stage = base + lo + roll.below(hi - lo + 1);
			}
			nodes.push_back(node);
		}
	}

	// The stops that are not fights: exactly one forge and one or two
	// events, scattered over distinct middle-row nodes. The entrance rows
	// stay battles - the first thing a run does is play - and the top row
	// stays the boss's alone.
	{
		std::vector<int> middle;
		for (size_t at = 0; at < nodes.size(); ++at) {
			if (nodes[at].depth > 0 && nodes[at].depth < kMapDepth - 1) {
				middle.push_back(static_cast<int>(at));
			}
		}
		const int stops = 2 + roll.below(2);
		for (int s = 0; s < stops && !middle.empty(); ++s) {
			const int pick = roll.below(static_cast<int>(middle.size()));
			MapNode& node = nodes[static_cast<size_t>(middle[pick])];
			node.kind = s == 0 ? 2 : 3;
			node.stage = -1;
			middle.erase(middle.begin() + pick);
		}
	}

	// The edges, rows joined by proportional intervals: node i of a row
	// with `a` lanes reaches nodes floor(i*b/a) through ((i+1)*b-1)/a of
	// the `b`-lane row above. The intervals tile, so every node is
	// reachable from the entrance and every node reaches the boss, and
	// they never cross. An identity row (a == b) would climb in straight
	// parallel lanes, so one extra diagonal is thrown in to make the
	// paths actually braid.
	for (int r = 0; r + 1 < kMapDepth; ++r) {
		const int a = widths[r];
		const int b = widths[r + 1];
		for (int i = 0; i < a; ++i) {
			MapNode& node = nodes[static_cast<size_t>(row_at[r] + i)];
			const int lo = i * b / a;
			const int hi = ((i + 1) * b - 1) / a;
			for (int j = lo; j <= hi; ++j) {
				node.next.push_back(row_at[r + 1] + j);
			}
		}
		if (a == b && a > 1) {
			const int i = roll.below(a - 1);
			MapNode& node = nodes[static_cast<size_t>(row_at[r] + i)];
			node.next.push_back(row_at[r + 1] + i + 1);
			std::sort(node.next.begin(), node.next.end());
		}
	}
	return nodes;
}

} // namespace campaign
} // namespace forcetris
