// The fuse ruleset, pinned: the schedule shrinks with the level and never
// past its floor, the bank only pays out once the schedule squeezes, the
// Flash window charges Flow and a burned fuse bleeds it, Overdrive freezes
// the burn and multiplies what goes out, a hold does not reset the clock -
// and with the flag off, none of it exists.
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/replay.hpp"
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

SimConfig fused () {
	SimConfig config;
	config.forced_delay = 0.;   // The fuse replaces it; both on would be odd.
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.fall_delay = 1000;   // Gravity out of the way; the fuse is the clock.
	config.fuse = true;
	return config;
}

std::vector<int> bags () {
	std::vector<int> dealt;
	for (int i = 0; i < 20; ++i) {
		for (int form = 0; form < 7; ++form) {
			dealt.push_back(form);
		}
	}
	return dealt;
}

void wait_spawn (Sim& sim, std::set<std::string>* heard = nullptr) {
	for (int i = 0; i < 100 && !sim.entry(); ++i) {
		sim.step(std::nullopt);
		if (heard != nullptr) {
			heard->insert(sim.cues().begin(), sim.cues().end());
		}
	}
}

// Steps until the current piece is gone (locked), collecting cues.
void run_out (Sim& sim, std::set<std::string>& heard, int limit = 400) {
	const size_t locks = sim.locked().size();
	for (int i = 0; i < limit && sim.locked().size() == locks; ++i) {
		if (!sim.step(std::nullopt)) {
			break;
		}
		heard.insert(sim.cues().begin(), sim.cues().end());
	}
}

void tap (Sim& sim, Key key, std::set<std::string>* heard = nullptr) {
	sim.step(std::vector<Event>{{key, true}, {key, false}});
	if (heard != nullptr) {
		heard->insert(sim.cues().begin(), sim.cues().end());
	}
}

// A board one clean single away: the bottom row full but for the spawn
// column region where a vertical I lands.
Board welled (int depth) {
	Board board;
	for (int y = kHeight - depth; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (x != kSpawnX + 1) {
				board.set(x, y, S);
			}
		}
	}
	return board;
}

} // namespace

int main () {
	// The schedule: base at level zero, shaved per level, floored.
	{
		SimConfig config = fused();
		Sim fresh(config, bags());
		wait_spawn(fresh);
		check("level zero burns the base fuse", fresh.fuse_total() == 3.0,
			std::to_string(fresh.fuse_total()));

		config.start_lines = 100;   // Level ten.
		Sim squeezed(config, bags());
		wait_spawn(squeezed);
		check("level ten shaves the schedule",
			squeezed.fuse_total() == 3.0 - 10 * 0.15,
			std::to_string(squeezed.fuse_total()));

		config.start_lines = 1000;  // Far past the floor.
		Sim floored(config, bags());
		wait_spawn(floored);
		check("the schedule never sinks past its floor",
			floored.fuse_total() == 0.8, std::to_string(floored.fuse_total()));
	}

	// The burn: a piece left alone is slammed down when the fuse runs out,
	// the warning cue sounding on the way; the forced drop bleeds Flow.
	{
		SimConfig config = fused();
		config.fuse_base = 0.4;    // Twenty frames.
		config.fuse_min = 0.4;
		Sim sim(config, bags());
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		run_out(sim, heard);
		check("the fuse forces the drop", !sim.locked().empty()
			&& sim.locked().back().forced);
		check("the warning sounded first", heard.count("fusewarn") == 1);
		check("a burned fuse leaves the gauge empty", sim.flow() == 0.);
	}

	// Flow: an instant lock lands inside the Flash window and charges the
	// gauge by the lock gain (scaled by fuse left, near full) plus the
	// Flash bonus.
	{
		Sim sim(fused(), bags());
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		tap(sim, Key::Hard, &heard);
		check("a flash lock charges lock gain plus flash",
			sim.flow() > 12. && sim.flow() <= 20.,
			std::to_string(sim.flow()));
		check("and the flash cue fired", heard.count("flash") == 1);
	}

	// The bank: at level zero a clear banks refuel but the schedule is
	// already the base, so nothing is drawn; at level ten the same bank
	// tops the next fuse up - by no more than the draw cap. The hard tap
	// itself locks the piece; waiting for the next spawn walks through the
	// clear's resolution, and that next piece is the one measured.
	{
		SimConfig config = fused();
		for (const int start : {0, 100}) {
			config.start_lines = start;
			Sim sim(config, bags());
			std::set<std::string> heard;
			wait_spawn(sim, &heard);
			sim.seed(welled(1));
			tap(sim, Key::Cw);
			tap(sim, Key::Hard);
			wait_spawn(sim, &heard);
			// A vertical I in the well clears one line, no attack: the bank
			// gains refuel_line * 1 = 0.4 seconds.
			if (start == 0) {
				check("level zero: refuel banks but cannot draw",
					sim.fuse_total() == 3.0 && sim.fuse_bank() > 0.,
					std::to_string(sim.fuse_total()) + " "
						+ std::to_string(sim.fuse_bank()));
			} else {
				const double schedule = 3.0 - 10 * 0.15;
				check("level ten: the bank tops the next fuse up",
					sim.fuse_total() > schedule
						&& sim.fuse_total() <= schedule + 1.0,
					std::to_string(sim.fuse_total()));
			}
		}
	}

	// The hold: under the fuse the clock rides through the swap.
	{
		Sim sim(fused(), bags());
		wait_spawn(sim);
		tap(sim, Key::Hold);       // First hold: next piece, fresh fuse.
		for (int i = 0; i < 10; ++i) {
			sim.step(std::nullopt);
		}
		const double before = sim.piece_elapsed().value_or(-1.);
		tap(sim, Key::Hold);       // The swap.
		check("a hold does not reset the fuse clock",
			sim.piece_elapsed().value_or(-1.) >= before && before > 0.,
			std::to_string(before));
	}
	{
		SimConfig config = fused();
		config.fuse = false;
		config.forced_delay = 5.;
		Sim sim(config, bags());
		wait_spawn(sim);
		tap(sim, Key::Hold);
		for (int i = 0; i < 10; ++i) {
			sim.step(std::nullopt);
		}
		tap(sim, Key::Hold);
		check("without the fuse the swap still resets it",
			sim.piece_elapsed().value_or(-1.) < 0.1,
			std::to_string(sim.piece_elapsed().value_or(-1.)));
	}

	// Overdrive: cranked gains fill the gauge in one flash lock; the fuse
	// freezes while it burns, attack is multiplied, and when it ends the
	// gauge starts over.
	{
		SimConfig config = fused();
		config.flow_lock_gain = 60.;
		config.flow_flash_gain = 60.;
		config.overdrive_secs = 0.5;   // 25 frames.
		config.clear_delay = false;    // The quad must resolve inside it.
		Sim sim(config, std::vector<int>(40, I));   // An all-I deck: quads.
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		tap(sim, Key::Hard, &heard);
		check("a full gauge ignites overdrive",
			sim.overdrive() && heard.count("overdrive") == 1);
		wait_spawn(sim, &heard);
		const double frozen = sim.piece_elapsed().value_or(-1.);
		for (int i = 0; i < 5; ++i) {
			sim.step(std::nullopt);
		}
		check("the fuse is frozen while overdrive burns",
			sim.overdrive()
				&& sim.piece_elapsed().value_or(-1.) == frozen);
		// A quad sent during overdrive carries the multiplier. The seeded
		// well is the whole board, so the quad is also a perfect clear:
		// four for the quad plus ten for the PC, then times 1.5 is 21.
		// With the clear delay off it resolves on the lock frame, well
		// inside the burn.
		sim.seed(welled(4));
		tap(sim, Key::Cw, &heard);
		tap(sim, Key::Hard, &heard);
		check("overdrive multiplies the attack",
			sim.locked().back().scored && sim.locked().back().attack == 21,
			std::to_string(sim.locked().back().attack));
		for (int i = 0; i < 40 && sim.overdrive(); ++i) {
			sim.step(std::nullopt);
			heard.insert(sim.cues().begin(), sim.cues().end());
		}
		check("overdrive ends and the gauge starts over",
			!sim.overdrive() && sim.flow() == 0.
				&& heard.count("overdrive_end") == 1);
	}

	// The record: a fuse-rules replay carries every tunable through a save
	// and load, and a file without the keys reads back as trainer rules.
	{
		replay::Replay game;
		game.meta.played = "2026-08-24T12:00:00";
		game.meta.fuse = true;
		game.meta.fuse_base = 3.0;
		game.meta.fuse_min = 0.8;
		game.meta.fuse_decay = 0.15;
		game.meta.fuse_bank_cap = 6.0;
		game.meta.fuse_draw_cap = 1.0;
		game.meta.fuse_refuel_line = 0.4;
		game.meta.fuse_refuel_attack = 0.5;
		game.meta.flash_frac = 0.30;
		game.meta.flash_floor = 0.25;
		game.meta.flow_lock_gain = 8.;
		game.meta.flow_flash_gain = 12.;
		game.meta.flow_burn_loss = 18.;
		game.meta.overdrive_secs = 8.;
		game.meta.overdrive_mult = 1.5;
		replay::Placement one;
		one.form = I;
		one.presses = {"HARD"};
		game.placements.push_back(one);
		check("the fuse replay saves", replay::save(game, "fusecheck-replays"));
		const auto back = replay::load(game.path);
		check("and loads with its ruleset whole", back.has_value()
			&& back->meta.fuse
			&& back->meta.fuse_base == 3.0
			&& back->meta.fuse_decay == 0.15
			&& back->meta.flash_floor == 0.25
			&& back->meta.overdrive_mult == 1.5);
		replay::Replay plain;
		plain.meta.played = "2026-08-24T12:00:01";
		plain.placements.push_back(one);
		check("a trainer replay saves without fuse keys",
			replay::save(plain, "fusecheck-replays"));
		const auto legacy = replay::load(plain.path);
		check("and reads back as trainer rules",
			legacy.has_value() && !legacy->meta.fuse);
	}

	// The variant's own score file: a duel entry round-trips through
	// fusescore.dat, the trainer's hiscore.dat is never created, and a
	// name from neither ruleset is refused.
	{
		const std::string folder = "fusecheck-scores";
		std::filesystem::remove_all(folder);
		hiscore::Entry entry;
		const char* name = "Fusebox ";
		std::copy(name, name + 8, entry.name.begin());
		entry.score = 4200;
		entry.lines = 17;
		entry.timer = 9000;
		check("a variant score submits", hiscore::submit_fuse(folder, "duel", entry));
		const hiscore::FuseTables tables = hiscore::load_fuse(folder);
		check("and tops its own table",
			hiscore::shown_name(tables[5][0]) == "Fusebox"
				&& tables[5][0].score == 4200);
		check("the trainer file was never touched",
			!std::filesystem::exists(
				std::filesystem::path(folder) / "hiscore.dat"));
		check("an unknown name is refused",
			!hiscore::submit_fuse(folder, "free", entry)
				&& hiscore::place_fuse(tables, "free", entry)
					== hiscore::kPerTable);
	}

	// Flags off: the ruleset does not exist.
	{
		SimConfig config;
		config.forced_delay = 0.;
		config.finesse_rule = 0;
		Sim sim(config, bags());
		wait_spawn(sim);
		tap(sim, Key::Hard);
		check("with the flag off nothing stirs",
			sim.fuse_total() == 0. && sim.flow() == 0. && !sim.overdrive());
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
