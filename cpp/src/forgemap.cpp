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

	// The chapter's rooms, in table order - the road's own difficulty
	// order - are what the battle rows draw from. Asking by role rather
	// than counting a trailing block means new duel recipes never move
	// the window, so a seed's map keeps the fights it always had.
	const std::vector<int> rooms = chapter_rooms(chapter);
	const int battles = static_cast<int>(rooms.size());
	if (battles == 0) {
		return nodes;
	}

	// Which watches stand at the top - plural, now.
	//
	// A chapter fields several concept pairs (a miniboss and a boss who
	// belong together) and a run used to climb to exactly one of them: the
	// top row was a single node and every path in the map funnelled into
	// it. That is what made a map feel closed. It ends at two or three
	// different watches instead, each from a different pair, so which
	// finale a run gets is decided by the road it walked rather than by
	// the seed alone - and a chapter has that many endings.
	//
	// Rolled from its own stream so the choice of faces never shifts the
	// skeleton the main stream draws, which keeps old seeds' shapes.
	const std::vector<int> pairs = chapter_pairs(chapter);
	std::vector<int> crowns;
	int mini = -1;
	if (!pairs.empty()) {
		Roll pick(seed ^ 0x5bf03635u, chapter);
		std::vector<int> spare = pairs;
		const int want = std::min(static_cast<int>(spare.size()),
			2 + pick.below(2));
		for (int at = 0; at < want && !spare.empty(); ++at) {
			const int which = pick.below(static_cast<int>(spare.size()));
			const int pair = spare[static_cast<size_t>(which)];
			spare.erase(spare.begin() + which);
			const int boss = pair_boss(chapter, pair);
			if (boss >= 0) {
				crowns.push_back(boss);
				if (mini < 0) {
					// The miniboss beneath belongs to the first crown
					// drawn: the risky branch is a branch toward one of
					// the finales, not a fourth thing on its own.
					mini = pair_miniboss(chapter, pair);
				}
			}
		}
	}
	if (crowns.empty()) {
		crowns.push_back(rooms.back());
	}

	// The rows: two or three doors at the entrance, three or four lanes
	// through the middle, and the watches across the top. Wider than it
	// was on every row - a two-lane middle offers one choice per row,
	// which is a corridor with a bulge in it rather than a map.
	int widths[kMapDepth];
	widths[0] = 2 + roll.below(2);
	widths[kMapDepth - 1] = static_cast<int>(crowns.size());
	for (int r = 1; r < kMapDepth - 1; ++r) {
		widths[r] = 3 + roll.below(2);
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
				node.stage = crowns[static_cast<size_t>(lane)];
			} else {
				node.kind = 0;
				const int centre = battles > 1
					? r * (battles - 1) / (kMapDepth - 2) : 0;
				const int lo = std::max(0, centre - 1);
				const int hi = std::min(battles - 1, centre + 1);
				node.stage = rooms[static_cast<size_t>(
					lo + roll.below(hi - lo + 1))];
			}
			nodes.push_back(node);
		}
	}

	// The miniboss, when the chapter fields one: a duel on exactly one
	// lane of the row under the boss - the risky branch, priced in slag by
	// its own recipe. Seated before the stops so a forge or an event never
	// lands on top of it.
	int mini_at = -1;
	if (mini >= 0 && widths[kMapDepth - 2] > 1) {
		const int lane = roll.below(widths[kMapDepth - 2]);
		mini_at = row_at[kMapDepth - 2] + lane;
		nodes[static_cast<size_t>(mini_at)].kind = 4;
		nodes[static_cast<size_t>(mini_at)].stage = mini;
	}

	// The stops that are not fights: exactly one forge and one or two
	// events, scattered over distinct middle-row nodes. The entrance rows
	// stay battles - the first thing a run does is play - and the top row
	// stays the boss's alone.
	{
		std::vector<int> middle;
		for (size_t at = 0; at < nodes.size(); ++at) {
			if (nodes[at].depth > 0 && nodes[at].depth < kMapDepth - 1
				&& static_cast<int>(at) != mini_at) {
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

std::vector<MapNode> build_endless_map (int ring, unsigned seed) {
	// One ring of the Endless Climb. The skeleton mirrors build_map -
	// the same widths, the same tiling edges, the same stop scattering -
	// but the battles draw from EVERY chapter's pool with the window
	// sliding up as the rings stack, and the top row belongs to the
	// gatekeeper rotation instead of one chapter's boss. Deliberately a
	// sibling rather than a parameter soup: campaigncheck holds the two
	// to the same promises.
	std::vector<MapNode> nodes;
	if (ring < 0) {
		return nodes;
	}
	// Its own stream per ring, so every ring of one climb is a fresh map.
	Roll roll(seed ^ (0x9e3779b9u * static_cast<unsigned>(ring + 1)), 61);

	// The battle pool: every chapter's rooms, in road order - the road's
	// own difficulty order - with the watch (miniboss, boss) left out.
	// Skirmishes and raids ride along: the climb fields every kind of
	// fight.
	std::vector<int> pool;
	for (int c = 0; c < static_cast<int>(chapters().size()); ++c) {
		for (const int at : chapter_rooms(c)) {
			pool.push_back(at);
		}
	}
	const int span = static_cast<int>(pool.size());
	if (span == 0) {
		return nodes;
	}

	// The gatekeeper rotation: the road's watch in order - chapter one's
	// miniboss, chapter one's boss, and on up - then the White Heart's
	// own two trading watches without end. Which concept pair supplies
	// the face is rolled from the ring's own stream, so a long climb
	// meets different keepers than the one before it.
	const int watch = ring < 6 ? ring : (ring % 2 == 0 ? 4 : 5);
	const int keeper_chapter = watch / 2;
	const bool is_boss = watch % 2 == 1;
	const std::vector<int> keeper_pairs = chapter_pairs(keeper_chapter);
	int keeper = pool.back();
	if (!keeper_pairs.empty()) {
		Roll pick(seed ^ 0x5bf03635u, 61 + ring);
		const int pair = keeper_pairs[static_cast<size_t>(
			pick.below(static_cast<int>(keeper_pairs.size())))];
		const int at = is_boss ? pair_boss(keeper_chapter, pair)
			: pair_miniboss(keeper_chapter, pair);
		if (at >= 0) {
			keeper = at;
		}
	}

	int widths[kMapDepth];
	widths[0] = 2;
	widths[kMapDepth - 1] = 1;
	for (int r = 1; r < kMapDepth - 1; ++r) {
		widths[r] = 2 + roll.below(2);
	}

	int row_at[kMapDepth];
	for (int r = 0; r < kMapDepth; ++r) {
		row_at[r] = static_cast<int>(nodes.size());
		for (int lane = 0; lane < widths[r]; ++lane) {
			MapNode node;
			node.depth = r;
			node.lane = lane;
			if (r == kMapDepth - 1) {
				node.kind = is_boss ? 1 : 4;
				node.stage = keeper;
			} else {
				node.kind = 0;
				// The window climbs with the ring and the row, and once
				// it reaches the pool's top it stays there: past the
				// taught road, every fight is a hard one.
				const int centre = std::min(span - 1, ring * 2 + r);
				const int lo = std::max(0, centre - 1);
				const int hi = std::min(span - 1, centre + 1);
				node.stage = pool[static_cast<size_t>(
					lo + roll.below(hi - lo + 1))];
			}
			nodes.push_back(node);
		}
	}

	// The stops, exactly as the chapters scatter them: one forge, one or
	// two events, on distinct middle-row nodes.
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

	// The edges: proportional intervals with the one braid diagonal on
	// identity rows, byte for byte the chapters' own wiring.
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
