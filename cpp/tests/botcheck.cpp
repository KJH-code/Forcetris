// The opponent, pinned: it survives real games it plays through the real
// sim, lands where it planned, repeats itself under a seed, digs when shot
// at, and - the parts the finesse model never had - reaches a tuck-only
// cavity and spins a T into a real TSD slot.
#include <cstdio>
#include <deque>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "forcetris/attack.hpp"
#include "forcetris/board.hpp"
#include "forcetris/bot.hpp"
#include "forcetris/sim.hpp"

using namespace forcetris;

namespace {

int failures = 0;

void check (const char* name, bool ok, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		!ok && !detail.empty() ? " -- " : "", !ok ? detail.c_str() : "");
	if (!ok) {
		++failures;
	}
}

SimConfig bot_config () {
	SimConfig config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	return config;
}

// A seeded seven bag, the way the session deals one.
struct Bag {
	std::mt19937 rng;
	explicit Bag (unsigned seed) : rng(seed) {}
	void refill (Sim& sim) {
		while (sim.queue().size() < 8) {
			int forms[7] = {0, 1, 2, 3, 4, 5, 6};
			std::shuffle(forms, forms + 7, rng);
			for (const int form : forms) {
				sim.feed(form);
			}
		}
	}
};

using Plan = bot::Plan;

struct RunResult {
	int pieces = 0;
	int lines = 0;
	long frames = 0;
	bool lost = false;
	std::vector<std::array<int, 4>> locks;   // frame, form, x, y.
	bool plan_matched = true;
	int quads = 0;
	int attack = 0;
	int max_b2b = 0;
};

RunResult run_bot (const bot::Rank& rank, unsigned seed, int piece_limit,
                   int attack_every = 0, Sim* external = nullptr) {
	SimConfig config = bot_config();
	if (attack_every > 0) {
		config.gametype = 5;
	}
	Sim own(config, {});
	Sim& sim = external != nullptr ? *external : own;
	Bag bag(seed + 1);
	bot::Driver driver(seed, rank);
	std::mt19937 holes(seed + 2);
	RunResult result;
	size_t counted = 0;
	for (long frame = 0; frame < 100000; ++frame) {
		bag.refill(sim);
		while (sim.config().gametype >= 3 && sim.garbage_queued() < 10) {
			sim.feed_garbage(1 << (holes() % 10));
		}
		if (attack_every > 0 && frame > 0 && frame % attack_every == 0) {
			sim.receive_attack(2);
		}
		const Plan* planned = nullptr;
		const auto event = driver.next(sim);
		if (event.has_value() && event->key == Key::Hard && event->down) {
			planned = &driver.current();
		}
		const bool alive = sim.step(event);
		while (counted < sim.locked().size()) {
			const Locked& lock = sim.locked()[counted];
			result.locks.push_back({static_cast<int>(lock.frame),
				lock.form, lock.x, lock.y});
			if (planned != nullptr && counted == sim.locked().size() - 1) {
				// The lock this very frame is the planned drop; hold the
				// cells against the plan (states alias for I, S, Z).
				const Piece got{lock.form, lock.state, lock.x, lock.y};
				auto want_cells = cells_of(planned->landed);
				auto got_cells = cells_of(got);
				std::sort(want_cells.begin(), want_cells.end(),
					[] (Offset a, Offset b) {
						return a.y != b.y ? a.y < b.y : a.x < b.x; });
				std::sort(got_cells.begin(), got_cells.end(),
					[] (Offset a, Offset b) {
						return a.y != b.y ? a.y < b.y : a.x < b.x; });
				for (int i = 0; i < kCells; ++i) {
					if (want_cells[i].x != got_cells[i].x
						|| want_cells[i].y != got_cells[i].y) {
						result.plan_matched = false;
					}
				}
			}
			++counted;
		}
		if (!alive) {
			result.lost = true;
			break;
		}
		if (static_cast<int>(sim.locked().size()) >= piece_limit) {
			break;
		}
	}
	result.pieces = static_cast<int>(sim.locked().size());
	result.lines = sim.lines_cleared();
	result.frames = sim.frame();
	for (const Locked& lock : sim.locked()) {
		if (!lock.scored) {
			continue;
		}
		if (lock.lines == 4) {
			++result.quads;
		}
		result.attack += lock.attack;
		result.max_b2b = std::max(result.max_b2b, lock.b2b);
	}
	return result;
}

} // namespace

int main () {
	const auto& ladder = bot::ranks();
	const bot::Rank steady{"T", 2.0, 0., 1, true, true};

	// It survives and clears: three hundred pieces of a real game, no loss.
	{
		const RunResult run = run_bot(steady, 20260821, 300);
		check("the bot survives three hundred pieces", !run.lost,
			"lost after " + std::to_string(run.pieces));
		check("and clears lines while it is at it", run.lines > 60,
			std::to_string(run.lines));
		check("it lands where it planned", run.plan_matched);
	}

	// The same seed replays the same game, a different seed does not.
	{
		const RunResult one = run_bot(steady, 7, 120);
		const RunResult two = run_bot(steady, 7, 120);
		check("the same seed plays the same game", one.locks == two.locks);
		const bot::Rank wobbly{"W", 2.0, 0.3, 1, true, true};
		const RunResult three = run_bot(wobbly, 7, 120);
		const RunResult four = run_bot(wobbly, 7, 120);
		check("a blundering seed repeats itself too",
			three.locks == four.locks);
		check("and blunders somewhere", one.locks != three.locks);
	}

	// The pace lands near the rank's own.
	{
		const bot::Rank paced{"P", 1.5, 0., 1, true, true};
		const RunResult run = run_bot(paced, 99, 100);
		const double pps = run.pieces / (run.frames / 50.);
		check("the pace lands near the dial",
			pps > 1.5 * 0.8 && pps < 1.5 * 1.15,
			std::to_string(pps));
	}

	// Shot at - two rows every four seconds, half of what a two-PPS digger
	// can keep up with at the theoretical limit - it stays alive.
	{
		const RunResult run = run_bot(steady, 3, 150, 200);
		check("it survives under fire", !run.lost,
			"lost after " + std::to_string(run.pieces));
	}

	// A cavity only a tuck can fill: the roof over columns 0-1, the shaft at
	// 2-3. An O dropped beside it and slid under is the only way in.
	{
		Board board;
		board.set(0, 19, S);
		board.set(1, 19, S);
		for (int x = 4; x < kWidth; ++x) {
			board.set(x, 20, S);
			board.set(x, 21, S);
		}
		bot::Options options;
		options.tucks = true;
		options.spins = true;
		const auto ranked = bot::plan(
			board, Piece{O, 0, kSpawnX, kSpawnY}, true, -1, {}, options);
		check("the tuck cavity is found at all", !ranked.empty());
		bool cavity = false;
		bool tucked = false;
		for (const Plan& plan : ranked) {
			if (plan.use_hold) {
				continue;
			}
			int inside = 0;
			for (const Offset cell : cells_of(plan.landed)) {
				if (cell.x <= 1 && cell.y >= 20) {
					++inside;
				}
			}
			if (inside == kCells) {
				cavity = true;
				bool dropped = false;
				for (const bot::Move move : plan.route) {
					if (move == bot::Move::Drop) {
						dropped = true;
					} else if (dropped && (move == bot::Move::Left
						|| move == bot::Move::Right)) {
						tucked = true;
					}
				}
				break;
			}
		}
		check("the cavity placement is reachable", cavity);
		check("and its route slides under the roof", tucked);
		// The bot prefers it - filling four covered cells beats stacking.
		check("the planner picks the tuck",
			[&] {
				for (const Offset cell : cells_of(ranked.front().landed)) {
					if (cell.x > 1 || cell.y < 20) {
						return false;
					}
				}
				return !ranked.front().use_hold;
			}());
		// A rank without tucks never goes there.
		bot::Options plain = options;
		plain.tucks = false;
		plain.spins = false;
		const auto blunt = bot::plan(
			board, Piece{O, 0, kSpawnX, kSpawnY}, true, -1, {}, plain);
		bool reaches = false;
		for (const Plan& plan : blunt) {
			int inside = 0;
			for (const Offset cell : cells_of(plan.landed)) {
				if (cell.x <= 1 && cell.y >= 20) {
					++inside;
				}
			}
			reaches = reaches || inside == kCells;
		}
		check("a rank without tucks cannot reach it", !reaches);
	}

	// A real TSD slot: rows 20 and 21 one notch short, a roof making the
	// straight drop impossible. The planner must spin the T in, judge it a
	// spin, and - driven through the real sim - clear two as a T-spin.
	{
		Board board;
		for (int x = 0; x < kWidth; ++x) {
			if (x != 4) {
				board.set(x, 21, S);
			}
			if (x != 3 && x != 4 && x != 5) {
				board.set(x, 20, S);
			}
		}
		board.set(5, 19, S);   // The roof: no straight path into the notch.
		bot::Options options;
		options.tucks = true;
		options.spins = true;
		const auto ranked = bot::plan(
			board, Piece{T, 0, kSpawnX, kSpawnY}, true, -1, {}, options);
		check("a spin placement is on the list",
			[&] {
				for (const Plan& plan : ranked) {
					if (plan.spin != attack::NOT_SPIN && plan.cleared == 2) {
						return true;
					}
				}
				return false;
			}());
		check("and it is the best move",
			ranked.front().cleared == 2
			&& ranked.front().spin != attack::NOT_SPIN,
			"picked cleared=" + std::to_string(ranked.front().cleared));
		// End to end: the driver executes it through the real sim.
		SimConfig config = bot_config();
		Sim sim(config, std::vector<int>{T, 0, 1, 2, 3});
		sim.seed(board);
		bot::Driver driver(11, bot::Rank{"S", 5.0, 0., 1, true, true});
		bool cleared_two_spin = false;
		for (int frame = 0; frame < 400; ++frame) {
			const auto event = driver.next(sim);
			sim.step(event);
			if (!sim.locked().empty() && sim.locked().front().scored) {
				const Locked& lock = sim.locked().front();
				cleared_two_spin = lock.lines == 2 && lock.spin != 0;
				break;
			}
		}
		check("the driver spins it in through the real sim",
			cleared_two_spin);
	}

	// The builder: reserves a well, banks rows against it, spends its
	// clears on quads, keeps the chain long enough to charge surge - and
	// out-attacks the plain downstacker by a wide margin.
	{
		const bot::Rank builder{"Q", 2.0, 0., 2, true, true, true};
		const RunResult run = run_bot(builder, 20260821, 300);
		const RunResult plain = run_bot(steady, 20260821, 300);
		check("the builder survives three hundred pieces", !run.lost,
			"lost after " + std::to_string(run.pieces));
		check("and fires quads", run.quads >= 10, std::to_string(run.quads));
		check("and chains far enough to charge surge", run.max_b2b >= 4,
			std::to_string(run.max_b2b));
		const double rate = run.attack / std::max(1., double(run.pieces));
		const double plain_rate
			= plain.attack / std::max(1., double(plain.pieces));
		check("and out-attacks the plain downstacker",
			rate > plain_rate * 1.5,
			std::to_string(rate) + " vs " + std::to_string(plain_rate));
		// ~0.4 attack a piece is what this depth of search sustains; the
		// pin sits under the five-seed floor (0.407) so a regression back
		// to downstacking (~0.2) fails loudly without the seed being magic.
		check("with real output per piece", rate >= 0.36,
			std::to_string(rate));
	}

	// The well is sacred while it is banked: two rows standing complete
	// against it, an I in hand. A building rank stacks on; a plain rank
	// dumps it in for the cheap double.
	{
		Board board;
		for (int y = kHeight - 2; y < kHeight; ++y) {
			for (int x = 0; x < kWidth - 1; ++x) {
				board.set(x, y, S);
			}
		}
		bot::Options building;
		building.build = true;
		const auto kept = bot::plan(
			board, Piece{I, 0, kSpawnX, kSpawnY}, true, -1, {}, building);
		bool keeps = !kept.empty() && kept.front().cleared == 0;
		if (keeps) {
			for (const Offset cell : cells_of(kept.front().landed)) {
				keeps = keeps && cell.x != kWidth - 1;
			}
		}
		check("a building rank keeps its well", keeps);
		bot::Options plain;
		const auto spent = bot::plan(
			board, Piece{I, 0, kSpawnX, kSpawnY}, true, -1, {}, plain);
		check("where a plain rank spends it",
			!spent.empty() && spent.front().cleared > 0);
	}

	// And it pays out: four rows banked, the quad is the best move on the
	// board.
	{
		Board board;
		for (int y = kHeight - 4; y < kHeight; ++y) {
			for (int x = 0; x < kWidth - 1; ++x) {
				board.set(x, y, S);
			}
		}
		bot::Options building;
		building.build = true;
		const auto ranked = bot::plan(
			board, Piece{I, 0, kSpawnX, kSpawnY}, true, -1, {}, building);
		check("the banked well pays out in a quad",
			!ranked.empty() && ranked.front().cleared == 4,
			ranked.empty() ? "no plans"
				: "cleared=" + std::to_string(ranked.front().cleared));
	}

	// The ladder is sane: pace climbs, blunders fall, the top tucks and
	// spins and the bottom does not.
	{
		bool pace_climbs = true;
		bool blunder_falls = true;
		for (size_t i = 1; i < ladder.size(); ++i) {
			pace_climbs = pace_climbs && ladder[i].pps > ladder[i - 1].pps;
			blunder_falls = blunder_falls
				&& ladder[i].blunder < ladder[i - 1].blunder;
		}
		check("pace climbs the ladder", pace_climbs);
		check("blunders fall down it", blunder_falls);
		check("the bottom plays plainly",
			!ladder.front().tucks && !ladder.front().spins
			&& !ladder.front().build);
		check("the top plays everything",
			ladder.back().tucks && ladder.back().spins
			&& ladder.back().build && ladder.back().depth == 2);
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
