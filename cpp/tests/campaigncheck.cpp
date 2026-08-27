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

		// --- The run rides in the same file. --------------------------------
		campaign::State climbing;
		climbing.slag = 40;
		climbing.run.active = true;
		climbing.run.chapter = 1;
		climbing.run.seed = 0xBEEFu;
		climbing.run.difficulty = campaign::kForged;
		climbing.run.depth = 2;
		climbing.run.path = {1, 3};
		climbing.run.tempers = {"thick_wick", "bellows", "thick_wick"};
		climbing.run.embers = 27;
		climbing.run.lives = 2;
		check("a run saves", campaign::save(file, climbing));
		const campaign::State resumed = campaign::load(file);
		check("and resumes exactly where it stood",
			resumed.run.active && resumed.run.chapter == 1
				&& resumed.run.seed == 0xBEEFu
				&& resumed.run.difficulty == campaign::kForged
				&& resumed.run.depth == 2
				&& resumed.run.path == climbing.run.path
				&& resumed.run.tempers == climbing.run.tempers
				&& resumed.run.embers == 27 && resumed.run.lives == 2);
		climbing.run.active = false;
		check("ending a run erases its keys",
			campaign::save(file, climbing)
				&& !campaign::load(file).run.active
				&& campaign::load(file).run.tempers.empty());
		{
			// Leftover run keys with no seed must not revive a ghost run,
			// and a depth taller than its path is pulled back down.
			std::ofstream out(file, std::ios::trunc);
			out << "run_depth 3\nrun_embers 9\n";
		}
		check("run keys without a seed are not a run",
			!campaign::load(file).run.active
				&& campaign::load(file).run.embers == 0);
		{
			std::ofstream out(file, std::ios::trunc);
			out << "run_seed 7\nrun_depth 5\nrun_path 0,1\n"
				<< "run_embers -4\nrun_lives 99\ndifficulty nonsense\n";
		}
		const campaign::State bent = campaign::load(file);
		check("a bent run is clamped into sense",
			bent.run.active && bent.run.depth == 2 && bent.run.embers == 0
				&& bent.run.lives == 9
				&& bent.run.difficulty == campaign::kMild);
		fs::remove_all(folder, ignored);
	}

	// --- The map. -----------------------------------------------------------
	{
		using campaign::MapNode;
		// Shape, connectivity and honesty of every map, across both
		// chapters and a spread of seeds - the generator's whole promise.
		bool shaped = true;
		bool connected = true;
		bool ranged = true;
		bool crossed = false;
		std::string detail;
		for (int chapter = 0;
			chapter < static_cast<int>(campaign::chapters().size());
			++chapter) {
			int battle_lo = 0;
			for (int c = 0; c < chapter; ++c) {
				battle_lo += campaign::chapters()[c].stages;
			}
			const int boss_stage = battle_lo
				+ campaign::chapters()[chapter].stages - 1;
			for (unsigned seed = 1; seed <= 40; ++seed) {
				const std::vector<MapNode> map
					= campaign::build_map(chapter, seed * 977u);
				// Rows: entrance of two, middle of two or three, one boss.
				int widths[campaign::kMapDepth] = {};
				for (const MapNode& node : map) {
					if (node.depth < 0 || node.depth >= campaign::kMapDepth) {
						shaped = false;
						continue;
					}
					++widths[node.depth];
				}
				shaped = shaped && widths[0] == 2
					&& widths[campaign::kMapDepth - 1] == 1;
				for (int r = 1; r < campaign::kMapDepth - 1; ++r) {
					shaped = shaped && widths[r] >= 2 && widths[r] <= 3;
				}
				// Every node's kind and stage sit in its chapter's range.
				for (const MapNode& node : map) {
					const bool boss = node.depth == campaign::kMapDepth - 1;
					ranged = ranged && node.kind == (boss ? 1 : 0)
						&& (boss ? node.stage == boss_stage
							: node.stage >= battle_lo
								&& node.stage < boss_stage);
				}
				// Edges point one row up and never cross; every node is
				// reachable from the entrance and reaches the boss.
				std::vector<int> reach(map.size(), 0);
				for (size_t at = 0; at < map.size(); ++at) {
					if (map[at].depth == 0) {
						reach[at] = 1;
					}
				}
				for (size_t at = 0; at < map.size(); ++at) {
					for (const int to : map[at].next) {
						if (to < 0 || to >= static_cast<int>(map.size())
							|| map[to].depth != map[at].depth + 1) {
							shaped = false;
							continue;
						}
						if (reach[at]) {
							reach[to] = 1;
						}
						for (size_t other = 0; other < map.size(); ++other) {
							if (map[other].depth != map[at].depth
								|| other == at) {
								continue;
							}
							for (const int their : map[other].next) {
								if ((map[at].lane - map[other].lane)
									* (map[to].lane - map[their].lane) < 0) {
									crossed = true;
								}
							}
						}
					}
				}
				std::vector<int> climbs(map.size(), 0);
				for (size_t at = map.size(); at-- > 0;) {
					if (map[at].depth == campaign::kMapDepth - 1) {
						climbs[at] = 1;
						continue;
					}
					for (const int to : map[at].next) {
						if (to >= 0 && to < static_cast<int>(map.size())
							&& climbs[to]) {
							climbs[at] = 1;
						}
					}
				}
				for (size_t at = 0; at < map.size(); ++at) {
					connected = connected && reach[at] && climbs[at];
				}
				if ((!shaped || !connected || !ranged || crossed)
					&& detail.empty()) {
					detail = "chapter " + std::to_string(chapter) + " seed "
						+ std::to_string(seed * 977u);
				}
			}
		}
		check("every map is shaped 2 / 2..3 / 1 with honest kinds", shaped,
			detail);
		check("every battle draws from its chapter, every boss is the boss",
			ranged, detail);
		check("every node is reachable and every node reaches the boss",
			connected, detail);
		check("no two edges cross", !crossed, detail);
		check("the same seed builds the same map",
			[] {
				const auto a = campaign::build_map(0, 12345u);
				const auto b = campaign::build_map(0, 12345u);
				if (a.size() != b.size()) {
					return false;
				}
				for (size_t i = 0; i < a.size(); ++i) {
					if (a[i].depth != b[i].depth || a[i].lane != b[i].lane
						|| a[i].kind != b[i].kind || a[i].stage != b[i].stage
						|| a[i].next != b[i].next) {
						return false;
					}
				}
				return true;
			}());
		check("different seeds build different maps",
			[] {
				for (unsigned seed = 1; seed <= 8; ++seed) {
					const auto a = campaign::build_map(0, seed);
					const auto b = campaign::build_map(0, seed + 100u);
					for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
						if (a[i].stage != b[i].stage
							|| a[i].next != b[i].next) {
							return true;
						}
					}
				}
				return false;
			}());
		check("a chapter off the road builds no map",
			campaign::build_map(-1, 1u).empty()
				&& campaign::build_map(99, 1u).empty());
	}

	// --- Difficulty and the chapter gate. -----------------------------------
	{
		check("slag scales with the weight of death",
			campaign::slag_percent(campaign::kMild) == 100
				&& campaign::slag_percent(campaign::kForged) == 150
				&& campaign::slag_percent(campaign::kWhite) == 200);
		check("difficulty names round-trip",
			campaign::difficulty_from(
					campaign::difficulty_name(campaign::kWhite))
					== campaign::kWhite
				&& campaign::difficulty_from(
					campaign::difficulty_name(campaign::kForged))
					== campaign::kForged
				&& campaign::difficulty_from("gibberish") == campaign::kMild);
		campaign::State fresh;
		const int first_boss = campaign::chapters()[0].stages - 1;
		campaign::State keyed;
		keyed.stars[campaign::stages()[first_boss].id] = 1;
		check("the first chapter is open, the second waits for the boss",
			campaign::chapter_open(fresh, 0)
				&& !campaign::chapter_open(fresh, 1)
				&& campaign::chapter_open(keyed, 1)
				&& !campaign::chapter_open(keyed, 99));
	}

	std::printf("%s\n",
		failures == 0 ? "all campaign checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
