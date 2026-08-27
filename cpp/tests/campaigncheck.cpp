// The Forge Road, graded: the stage table's integrity, every recipe knob
// landing in the rules it claims to set, the boss sides kept honest, the
// unlock chain, the economy's arithmetic, and the save file's round trip.
//
// The campaign is data plus arithmetic - the screens only display what this
// module decides - so everything a stage can do to a game is checkable here
// without a window, and a rebalance that breaks the road's shape fails
// before anyone plays it.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/bot.hpp"
#include "forcetris/campaign.hpp"
#include "forcetris/sim.hpp"
#include "forcetris/temper.hpp"

using namespace forcetris;
using campaign::Stage;

namespace {

int failures = 0;

void check (const char* name, bool ok, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		!ok && !detail.empty() ? " -- " : "", !ok ? detail.c_str() : "");
	if (!ok) {
		++failures;
	}
}

SimConfig base () {
	SimConfig config;
	config.fuse = true;
	return config;
}

} // namespace

int main () {
	// --- The table's integrity. ---------------------------------------------
	{
		std::set<std::string> ids;
		bool unique = true;
		bool shaped = true;
		bool named = true;
		std::string detail;
		for (const Stage& stage : campaign::stages()) {
			if (!ids.insert(stage.id).second) {
				unique = false;
				detail += std::string(stage.id) + " repeats; ";
			}
			named = named && stage.name != nullptr && stage.blurb != nullptr;
			const bool boss = stage.mode == 5;
			if (boss) {
				shaped = shaped && stage.rank >= 0
					&& stage.rank < static_cast<int>(bot::ranks().size())
					&& stage.first_to >= 1;
			} else {
				shaped = shaped && (stage.mode == 0 || stage.mode == 3
						|| stage.mode == 4)
					&& stage.quota > 0 && stage.par_seconds > 0;
			}
			shaped = shaped && stage.slag_first > stage.slag_repeat
				&& stage.slag_repeat > 0;
			if (!shaped && detail.empty()) {
				detail += std::string(stage.id) + " misshapen; ";
			}
		}
		check("every stage id is unique", unique, detail);
		check("every stage is named and shaped for its mode", shaped && named,
			detail);
		// The chapter table is the road's other half: the two must agree,
		// every chapter must be a real named stretch, and each must end in
		// a boss - the shape every future chapter is held to.
		int counted = 0;
		bool chapters_named = true;
		bool bossed = true;
		std::set<std::string> chapter_ids;
		bool chapters_unique = true;
		for (const campaign::Chapter& chapter : campaign::chapters()) {
			chapters_named = chapters_named && chapter.id != nullptr
				&& chapter.id[0] != '\0' && chapter.name != nullptr
				&& chapter.name[0] != '\0' && chapter.blurb != nullptr
				&& chapter.stages > 0;
			chapters_unique = chapters_unique
				&& chapter_ids.insert(chapter.id ? chapter.id : "").second;
			counted += chapter.stages;
			if (counted <= static_cast<int>(campaign::stages().size())) {
				bossed = bossed
					&& campaign::stages()[counted - 1].mode == 5;
			}
		}
		check("the chapter table covers the road exactly",
			counted == static_cast<int>(campaign::stages().size()));
		check("every chapter is named, non-empty and unique",
			chapters_named && chapters_unique);
		check("every chapter ends in a boss", bossed);
		// spot_of is the screens' map from flat index to chapter frame:
		// walk the road and hold it to the running count.
		bool spotted = true;
		int chapter_at = 0;
		int within = 0;
		for (size_t at = 0; at < campaign::stages().size(); ++at) {
			if (within == campaign::chapters()[chapter_at].stages) {
				++chapter_at;
				within = 0;
			}
			const campaign::Spot spot = campaign::spot_of(at);
			spotted = spotted && spot.chapter == chapter_at
				&& spot.stage == within;
			++within;
		}
		check("spot_of agrees with the chapter table", spotted);
	}

	// --- Every override lands, and nothing else moves. ----------------------
	{
		// A synthetic stage that sets every knob at once, so a knob the
		// builder forgets shows up as the default it left behind.
		Stage all{};
		all.id = "test"; all.name = "t"; all.blurb = "t";
		all.mode = 3; all.quota = 12;
		all.fuse_scale = 0.5;
		all.fall_delay = 15;
		all.cheese_holes = 3;
		all.cheese_messiness = 70;
		all.cheese_period = 200;
		all.no_kicks = true;
		all.cleartype = 2;
		all.spin_rule = 1;
		all.tempers = "thick_wick,bellows";
		const SimConfig raw = base();
		const SimConfig built = campaign::stage_config(all, raw, {});
		check("the stage's overrides all land",
			std::abs(built.fuse_base - (raw.fuse_base * 0.5 + 0.5)) < 1e-9
				&& built.fall_delay == 15 && built.cheese_holes == 3
				&& built.cheese_messiness == 70 && built.cheese_period == 200
				&& !built.kicks && built.cleartype == 2 && built.spin_rule == 1
				&& built.cheese_total == 12
				&& std::abs(built.overdrive_secs - (raw.overdrive_secs + 3.))
					< 1e-9);
		check("a dig stage's quota is cheese, not a line quota",
			built.line_quota == 0);

		Stage quota{};
		quota.id = "q"; quota.name = "q"; quota.blurb = "q";
		quota.mode = 0; quota.quota = 20;
		check("a quota stage's quota is the finish line",
			campaign::stage_config(quota, raw, {}).line_quota == 20);

		SimConfig off = raw;
		off.fuse = false;
		check("a stage is a fuse game whatever the Rules tab says",
			campaign::stage_config(quota, off, {}).fuse);

		check("the fuse never scales below its own floor",
			[&] {
				Stage hot{};
				hot.id = "h"; hot.name = "h"; hot.blurb = "h";
				hot.mode = 0; hot.quota = 10;
				hot.fuse_scale = 0.01;
				const SimConfig built_hot
					= campaign::stage_config(hot, raw, {});
				return built_hot.fuse_base >= built_hot.fuse_min - 1e-9;
			}());
	}

	// --- The Anvil lands on the player and never on the boss. ---------------
	{
		std::map<std::string, int> forge
			= {{"wick", 3}, {"bank", 2}, {"bellows", 2}, {"sense", 2},
				{"preheat", 1}};
		Stage quota{};
		quota.id = "q"; quota.name = "q"; quota.blurb = "q";
		quota.mode = 0; quota.quota = 10;
		const SimConfig raw = base();
		const SimConfig mine = campaign::stage_config(quota, raw, forge);
		check("the Anvil's metal lands on the player's rules",
			std::abs(mine.fuse_base - (raw.fuse_base + 0.3)) < 1e-9
				&& std::abs(mine.fuse_refuel_line - (raw.fuse_refuel_line + 0.1))
					< 1e-9
				&& std::abs(mine.overdrive_secs - (raw.overdrive_secs + 1.))
					< 1e-9);
		check("the GUI-side upgrades read back",
			campaign::ember_bonus_percent(forge) == 50
				&& campaign::free_drafts(forge) == 1
				&& campaign::ember_bonus_percent({}) == 0
				&& campaign::free_drafts({}) == 0);

		// The boss builds from the same base and must see none of it.
		const Stage& boss
			= campaign::stages()[campaign::chapters()[0].stages - 1];
		const SimConfig theirs = campaign::bot_config(boss, raw);
		check("the boss's rules carry no player metal",
			theirs.fuse_base == raw.fuse_base * boss.fuse_scale
				&& theirs.fuse_refuel_line == raw.fuse_refuel_line
				&& theirs.overdrive_secs == raw.overdrive_secs);
	}

	// --- The blades of the road. --------------------------------------------
	{
		bool sane = true;
		std::string detail;
		for (const Stage& stage : campaign::stages()) {
			if (stage.mode != 5) {
				continue;
			}
			const std::vector<std::string> blade = campaign::blade_of(stage);
			if (blade.empty()) {
				sane = false;
				detail += std::string(stage.id) + " unarmed; ";
			}
			for (const std::string& id : blade) {
				if (id == "collapse" || temper::find(id) == nullptr) {
					sane = false;
					detail += id + " on " + stage.id + "; ";
				}
			}
		}
		check("every boss carries a real blade, never collapse", sane, detail);
	}

	// --- Preset boards parse. -----------------------------------------------
	{
		bool sane = true;
		std::string detail;
		for (const Stage& stage : campaign::stages()) {
			for (const std::string& row : campaign::board_rows(stage)) {
				if (row.size() != static_cast<size_t>(kWidth)) {
					sane = false;
					detail += std::string(stage.id) + " row width "
						+ std::to_string(row.size()) + "; ";
				}
				// Every preset row must have a hole, or it would clear on
				// the first frame the sim looks at it.
				if (row.find('.') == std::string::npos) {
					sane = false;
					detail += std::string(stage.id) + " has a full row; ";
				}
			}
		}
		check("every preset board is ten wide with a hole per row", sane,
			detail);
		check("a preset board builds",
			!Board::from_rows(campaign::board_rows(
				campaign::stages()[1])).rows().empty());
	}

	// --- The unlock chain. --------------------------------------------------
	{
		campaign::State fresh;
		check("a fresh road opens only its first stage",
			campaign::open(fresh, 0) && !campaign::open(fresh, 1)
				&& !campaign::open(fresh, campaign::stages().size() - 1));
		campaign::State walked;
		for (size_t at = 0; at + 1 < campaign::stages().size(); ++at) {
			walked.stars[campaign::stages()[at].id] = 1;
		}
		check("a walked road opens to its end",
			campaign::open(walked, campaign::stages().size() - 1)
				&& !campaign::open(walked, campaign::stages().size()));
	}

	// --- Stars and slag. ----------------------------------------------------
	{
		check("solo stars: clear, par, flawless",
			campaign::solo_stars(false, 10., 100, 0) == 0
				&& campaign::solo_stars(true, 150., 100, 3) == 1
				&& campaign::solo_stars(true, 90., 100, 3) == 2
				&& campaign::solo_stars(true, 150., 100, 0) == 2
				&& campaign::solo_stars(true, 90., 100, 0) == 3);
		check("boss stars: win, sweep, ignited sweep",
			campaign::boss_stars(false, true, true) == 0
				&& campaign::boss_stars(true, false, true) == 1
				&& campaign::boss_stars(true, true, false) == 2
				&& campaign::boss_stars(true, true, true) == 3);
		const Stage& first = campaign::stages().front();
		check("a win pays the bounty plus the stars",
			campaign::slag_award(first, true, true, 3, 0)
					== first.slag_first + 15
				&& campaign::slag_award(first, false, true, 1, 0)
					== first.slag_repeat + 5);
		check("a death renders the unspent embers down",
			campaign::slag_award(first, true, false, 0, 50) == 10
				&& campaign::slag_award(first, true, false, 0, 0) == 0);
		const campaign::Upgrade* wick = campaign::upgrade("wick");
		check("the Anvil's ledger is sane",
			wick != nullptr && campaign::upgrade_cost(*wick, 2)
				== wick->cost_base * 2
				&& campaign::upgrade("no_such") == nullptr);
	}

	// --- The file. ----------------------------------------------------------
	{
		namespace fs = std::filesystem;
		std::error_code ignored;
		const fs::path folder = fs::temp_directory_path() / "forcetris-camp";
		fs::remove_all(folder, ignored);
		fs::create_directories(folder, ignored);
		const std::string file = (folder / "campaign.dat").string();

		campaign::State state;
		state.slag = 123;
		state.stars["c1s1"] = 3;
		state.stars["c1s2"] = 1;
		state.forge["wick"] = 2;
		state.unknown.push_back("futurething 42");
		check("the road saves", campaign::save(file, state));
		const campaign::State back = campaign::load(file);
		check("and loads back whole",
			back.slag == 123 && back.stars.at("c1s1") == 3
				&& back.stars.at("c1s2") == 1 && back.forge.at("wick") == 2);
		check("a key this build does not know survives the round trip",
			back.unknown.size() == 1 && back.unknown[0] == "futurething 42"
				&& campaign::save(file, back)
				&& campaign::load(file).unknown.size() == 1);
		check("a missing file is an empty road, not an error",
			campaign::load((folder / "nothere.dat").string()).slag == 0);

		// Hand-damage: out-of-range values clamp instead of poisoning.
		{
			std::ofstream out(file, std::ios::trunc);
			out << "slag -50\nstage c1s1 99\nforge wick 99\n";
		}
		const campaign::State fixed = campaign::load(file);
		check("a damaged file is clamped into sense",
			fixed.slag == 0 && fixed.stars.at("c1s1") == 3
				&& fixed.forge.at("wick") == 3);
		fs::remove_all(folder, ignored);
	}

	std::printf("%s\n",
		failures == 0 ? "all campaign checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
