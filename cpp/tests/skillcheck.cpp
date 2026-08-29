// The boss skills, graded: what they are, when they land, what they do to
// the board, and the one rule that keeps a fat kit readable.
//
// Every decision a skill makes is a field on VersusMatch rather than a
// reach into the screen, which is what lets this run headlessly: the
// telegraph, the landing, the imposition and its lifting are all state,
// and only the pixels are not. Nothing here rolls a die - a skill's clock
// is its period, and the same frames give the same fight every time.
#include <cstdio>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "forcetris/replay.hpp"
#include "forcetris/sim.hpp"
#include "../gui/session.hpp"
#include "../gui/versus.hpp"

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

SimConfig duel () {
	SimConfig config;
	config.gametype = 5;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.fall_delay = 30;
	config.flow_rail = true;
	config.clear_delay = false;
	return config;
}

// A player session the skills can be ticked against, standing still: the
// point is the boss's clock, not the player's hands.
gui::Session board () {
	replay::Meta meta;
	return gui::Session(duel(), 4242u, meta);
}

// Every id arm_skills may hand out. A kit naming anything else would
// telegraph and then silently do nothing, which is the one failure the
// system cannot show on screen.
const std::set<std::string>& known () {
	static const std::set<std::string> ids = {"rustfall", "sealgate",
		"coldsnap", "heatwave", "pincer", "vaultdark", "smokescreen",
		"deadweight", "tongslock", "forgestrike"};
	return ids;
}

const std::vector<std::string>& armed_stages () {
	static const std::vector<std::string> ids = {"c1m1", "c1s8", "c2m1",
		"c2s8", "c3m1", "c3s9", "c1m2", "c1b2", "c2m2", "c2b2", "c3m2",
		"c3b2", "c1m3", "c1b3", "c2m3", "c2b3", "c3m3", "c3b3"};
	return ids;
}

// Keep only the named skill of a kit. A blow is graded on its own here;
// how a whole kit takes turns is its own check further down.
void only (gui::VersusMatch& match, const std::string& id) {
	std::vector<gui::VersusMatch::Skill> kept;
	for (const gui::VersusMatch::Skill& skill : match.skills) {
		if (skill.id == id) {
			kept.push_back(skill);
		}
	}
	match.skills = kept;
}

// Tick a match's skills for `frames`, reporting what was live at each.
struct Watch {
	long first_warn = -1;
	long first_fire = -1;
	long lifted = -1;
	int most_at_once = 0;
	int landings = 0;
	bool ever_dark = false;
	bool ever_fog = false;
	bool ever_bar = false;
	bool ever_heavy = false;
	bool ever_sealed = false;
	bool ever_cold = false;
};

Watch run_for (gui::VersusMatch& match, gui::Session& player, long frames) {
	Watch seen;
	for (long f = 0; f < frames; ++f) {
		const int before = match.imposed_sealed;
		const long landed = seen.landings;
		match.tick_skills(player);
		if (seen.first_warn < 0 && !match.skill_banner.empty()) {
			seen.first_warn = player.sim().frame();
		}
		for (const gui::VersusMatch::Skill& skill : match.skills) {
			if (skill.landed_at == player.sim().frame()) {
				++seen.landings;
				if (seen.first_fire < 0) {
					seen.first_fire = skill.landed_at;
				}
			}
		}
		(void)before;
		(void)landed;
		int live = 0;
		for (const gui::VersusMatch::Skill& skill : match.skills) {
			live += player.sim().frame() < skill.active_until
				&& skill.duration > 0 ? 1 : 0;
		}
		seen.most_at_once = std::max(seen.most_at_once, live);
		seen.ever_dark = seen.ever_dark || match.imposed_dark;
		seen.ever_fog = seen.ever_fog || match.imposed_fog;
		seen.ever_bar = seen.ever_bar || match.imposed_hold_bar;
		seen.ever_heavy = seen.ever_heavy || match.imposed_gravity != 0;
		seen.ever_sealed = seen.ever_sealed || match.imposed_sealed != 0;
		seen.ever_cold = seen.ever_cold || match.imposed_cold;
		if (seen.first_fire >= 0 && seen.lifted < 0
			&& match.imposed_sealed == 0 && !match.imposed_cold
			&& !match.imposed_dark && !match.imposed_fog
			&& !match.imposed_hold_bar && match.imposed_gravity == 0
			&& player.sim().frame() > seen.first_fire) {
			seen.lifted = player.sim().frame();
		}
		player.step();
	}
	return seen;
}

} // namespace

int main () {
	// --- The kits. ----------------------------------------------------------
	{
		bool all_known = true;
		bool all_armed = true;
		bool sane = true;
		std::string detail;
		for (const std::string& stage : armed_stages()) {
			gui::VersusMatch match(4, 1);
			match.arm_skills(stage);
			if (match.skills.empty()) {
				all_armed = false;
				detail += stage + " unarmed; ";
			}
			for (const gui::VersusMatch::Skill& skill : match.skills) {
				if (known().count(skill.id) == 0) {
					all_known = false;
					detail += stage + " arms " + skill.id + "; ";
				}
				// A period shorter than the wind-up would have the next
				// warning up before the last blow landed.
				if (skill.period <= skill.telegraph || skill.telegraph <= 0
					|| skill.warning[0] == '\0') {
					sane = false;
					detail += stage + "/" + skill.id + " mistimed; ";
				}
			}
		}
		check("every stage that should field a kit fields one", all_armed,
			detail);
		check("and every id in a kit is one the tick knows", all_known,
			detail);
		check("and no skill warns for longer than its own period", sane,
			detail);
		gui::VersusMatch stranger(4, 1);
		stranger.arm_skills("c1s1");
		check("a room arms nothing at all", stranger.skills.empty());
	}

	// --- The clock: nothing before its period, and the warning leads it. ----
	{
		gui::VersusMatch match(4, 1);
		match.arm_skills("c1m1");
		gui::Session player = board();
		const gui::VersusMatch::Skill first = match.skills.front();
		const Watch early = run_for(match, player, first.period
			- first.telegraph - 2);
		check("nothing warns before its wind-up begins",
			early.first_warn < 0 && early.first_fire < 0,
			std::to_string(early.first_warn));
	}
	{
		gui::VersusMatch match(4, 1);
		match.arm_skills("c1m1");
		gui::Session player = board();
		const long period = match.skills.front().period;
		const long lead = match.skills.front().telegraph;
		const Watch seen = run_for(match, player, period + 10);
		check("the warning leads the blow by exactly its wind-up",
			seen.first_fire - seen.first_warn == lead,
			std::to_string(seen.first_warn) + " -> "
				+ std::to_string(seen.first_fire) + ", want "
				+ std::to_string(lead));
	}

	// --- What each blow actually does. --------------------------------------
	{
		// Rust lands on the player and on nobody else.
		gui::VersusMatch match(4, 1);
		match.arm_skills("c1m1");
		gui::Session player = board();
		run_for(match, player, match.skills.front().period + 4);
		check("rust falls on the player's floor",
			player.sim().pending_garbage() == 3,
			std::to_string(player.sim().pending_garbage()));
	}
	{
		// The anvil's long wind-up buys a heavier blow.
		gui::VersusMatch match(4, 1);
		match.arm_skills("c1b2");
		gui::Session player = board();
		long strike = 0;
		for (const gui::VersusMatch::Skill& skill : match.skills) {
			if (skill.id == "forgestrike") {
				strike = skill.period;
			}
		}
		run_for(match, player, strike + 4);
		check("the anvil's blow is the heaviest a boss throws",
			player.sim().pending_garbage() >= 6,
			std::to_string(player.sim().pending_garbage()));
	}
	{
		// The heat wave takes the gauge - the one resource a fuse-less
		// duel is fought over.
		gui::VersusMatch match(4, 1);
		match.arm_skills("c3m3");
		only(match, "heatwave");
		gui::Session player = board();
		SimConfig charged = duel();
		charged.flow_gain_taken = 40.;
		charged.flow_ignite = 1000.;   // Bank it; never light.
		player.sim_mutable().retune(charged);
		player.sim_mutable().receive_attack(3);
		const double banked = player.sim().flow();
		long wave = 0;
		for (const gui::VersusMatch::Skill& skill : match.skills) {
			if (skill.id == "heatwave") {
				wave = skill.period;
			}
		}
		run_for(match, player, wave + 4);
		check("the heat wave takes the gauge you were banking",
			banked > 0. && player.sim().flow() < banked,
			std::to_string(banked) + " -> "
				+ std::to_string(player.sim().flow()));
	}
	{
		// The pincer shuts both edges; the gate shuts one.
		const auto widest = [] (const char* stage, const char* id) {
			gui::VersusMatch match(4, 1);
			match.arm_skills(stage);
			only(match, id);
			gui::Session player = board();
			int wide = 0;
			for (long f = 0; f < 40 * 50; ++f) {
				match.tick_skills(player);
				int columns = 0;
				for (int x = 0; x < kWidth; ++x) {
					columns += match.imposed_sealed >> x & 1;
				}
				wide = std::max(wide, columns);
				player.step();
			}
			return wide;
		};
		check("the gate shuts one column and the pincer shuts two",
			widest("c1s8", "sealgate") == 1
				&& widest("c2s8", "pincer") == 2,
			std::to_string(widest("c1s8", "sealgate")) + " vs "
				+ std::to_string(widest("c2s8", "pincer")));

		gui::VersusMatch chill(4, 1);
		chill.arm_skills("c2s8");
		only(chill, "coldsnap");
		gui::Session three = board();
		const Watch frozen = run_for(chill, three, 40 * 50);
		check("and the cold snap freezes the iron", frozen.ever_cold);
	}
	{
		// The screen-facing impositions reach the outputs the GUI reads.
		gui::VersusMatch dark(4, 1);
		dark.arm_skills("c1b3");
		only(dark, "vaultdark");
		gui::Session a = board();
		const Watch shadowed = run_for(dark, a, 60 * 50);
		gui::VersusMatch smoke(4, 1);
		smoke.arm_skills("c2m3");
		only(smoke, "smokescreen");
		gui::Session b = board();
		const Watch smoked = run_for(smoke, b, 60 * 50);
		gui::VersusMatch heavy(4, 1);
		heavy.arm_skills("c3m1");
		only(heavy, "deadweight");
		gui::Session c = board();
		const Watch weighed = run_for(heavy, c, 60 * 50);
		gui::VersusMatch tongs(4, 1);
		tongs.arm_skills("c1m1");
		only(tongs, "tongslock");
		gui::Session d = board();
		const Watch barred = run_for(tongs, d, 60 * 50);
		check("the lamps go out where a kit says so", shadowed.ever_dark);
		check("the smoke rolls in where a kit says so", smoked.ever_fog);
		check("the hammer falls where a kit says so", weighed.ever_heavy);
		check("the tongs are taken where a kit says so", barred.ever_bar);
	}

	// --- One imposition at a time. ------------------------------------------
	// Ten skills on twenty-second periods with seven-second windows would
	// otherwise settle into a permanent fog-dark-narrow-heavy soup, which
	// is not difficulty, it is mud.
	{
		bool alone = true;
		std::string detail;
		for (const std::string& stage : armed_stages()) {
			gui::VersusMatch match(4, 1);
			match.arm_skills(stage);
			gui::Session player = board();
			const Watch seen = run_for(match, player, 200 * 50);
			if (seen.most_at_once > 1) {
				alone = false;
				detail += stage + " held "
					+ std::to_string(seen.most_at_once) + "; ";
			}
		}
		check("no fight ever holds two spells at once", alone, detail);
	}

	// --- A spell lifts itself. ----------------------------------------------
	{
		gui::VersusMatch match(4, 1);
		match.arm_skills("c3m1");
		only(match, "sealgate");
		gui::Session player = board();
		const Watch seen = run_for(match, player, 60 * 50);
		check("and every spell lifts itself when its time is up",
			seen.lifted > seen.first_fire, std::to_string(seen.lifted));
	}

	// --- A round starts clean. ----------------------------------------------
	{
		gui::VersusMatch match(4, 2);
		match.arm_skills("c3b3");
		gui::Session player = board();
		run_for(match, player, 60 * 50);
		replay::Meta meta;
		match.begin_round(99u, meta, duel(), {});
		bool clean = match.imposed_sealed == 0 && !match.imposed_cold
			&& !match.imposed_dark && !match.imposed_fog
			&& !match.imposed_hold_bar && match.imposed_gravity == 0
			&& match.skill_banner.empty();
		for (const gui::VersusMatch::Skill& skill : match.skills) {
			clean = clean && skill.next_fire == skill.period
				&& skill.landed_at < 0 && !skill.telegraphing;
		}
		check("a new round re-arms every skill and lifts every spell",
			clean);
	}

	std::printf("%s\n",
		failures == 0 ? "all skill checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
