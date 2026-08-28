// The Forge Road, graded: the stage table's integrity, every recipe knob
// landing in the rules it claims to set, the boss sides kept honest, the
// unlock chain, the economy's arithmetic, and the save file's round trip.
//
// The campaign is data plus arithmetic - the screens only display what this
// module decides - so everything a stage can do to a game is checkable here
// without a window, and a rebalance that breaks the road's shape fails
// before anyone plays it.
#include <algorithm>
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
			} else if (stage.survive_seconds > 0) {
				// A watch: the clock is the finish line, the quota is the
				// star bar, and nothing else competes with either.
				shaped = shaped && stage.mode == 4
					&& stage.quota > 0 && stage.score_quota == 0;
			} else {
				// A solo room has exactly one finish line: rows, or points.
				shaped = shaped && (stage.mode == 0 || stage.mode == 3
						|| stage.mode == 4)
					&& (stage.quota > 0) != (stage.score_quota > 0)
					&& stage.par_seconds > 0;
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
		// The seen-not-simmed gimmicks exist on the road: at least one
		// lantern stage and one smoked queue, and neither on a boss.
		bool dimmed = false;
		bool fogged = false;
		bool clean_bosses = true;
		for (const Stage& stage : campaign::stages()) {
			dimmed = dimmed || stage.dim;
			fogged = fogged || stage.fog;
			if (stage.mode == 5) {
				clean_bosses = clean_bosses && !stage.dim && !stage.fog;
			}
		}
		check("the road carries a dim stage and a fog stage, bosses clear",
			dimmed && fogged && clean_bosses);
	}
	{
		// The V2.1 simmed gimmicks each have exactly one home on the road,
		// and the bosses stay plain duels.
		int sealed_stages = 0;
		int cold_stages = 0;
		bool plain_bosses = true;
		for (const Stage& stage : campaign::stages()) {
			if (stage.sealed != 0) {
				++sealed_stages;
			}
			if (stage.cold_iron) {
				++cold_stages;
			}
			if (stage.mode == 5) {
				plain_bosses = plain_bosses
					&& stage.sealed == 0 && !stage.cold_iron;
			}
		}
		check("one sealed stage, one cold iron stage, bosses plain",
			sealed_stages == 1 && cold_stages == 1 && plain_bosses);
		int score_stages = 0;
		bool score_sane = true;
		for (const Stage& stage : campaign::stages()) {
			if (stage.score_quota > 0) {
				++score_stages;
				// A score room is a mode-0 room with no line quota: two
				// finish lines at once would race each other.
				score_sane = score_sane
					&& stage.mode == 0 && stage.quota == 0;
			}
		}
		check("each chapter fields one score stage, sanely",
			score_stages == 2 && score_sane);
		int watches = 0;
		int raids = 0;
		int skirmishes = 0;
		{
			// A skirmish is a mode-5 recipe ahead of its chapter's
			// trailing duel block.
			int at = 0;
			for (const campaign::Chapter& chapter : campaign::chapters()) {
				int duels = 0;
				while (duels < chapter.stages && campaign::stages()[
					static_cast<size_t>(at + chapter.stages - 1 - duels)]
						.mode == 5) {
					++duels;
				}
				for (int i = 0; i < chapter.stages - duels; ++i) {
					const Stage& stage
						= campaign::stages()[static_cast<size_t>(at + i)];
					if (stage.mode == 5) {
						++(stage.raid != nullptr ? raids : skirmishes);
					}
				}
				at += chapter.stages;
			}
			for (const Stage& stage : campaign::stages()) {
				if (stage.survive_seconds > 0) {
					++watches;
				}
			}
		}
		check("two watches, two skirmishes, one raid stand on the road",
			watches == 2 && skirmishes == 2 && raids == 1);
		check("watch stars climb the line bar",
			campaign::survive_stars(false, 30, 8) == 0
				&& campaign::survive_stars(true, 3, 8) == 1
				&& campaign::survive_stars(true, 9, 8) == 2
				&& campaign::survive_stars(true, 16, 8) == 3);
		check("a watch is won by outlasting the clock",
			[] {
				SimConfig config;
				config.forced_delay = 0.;
				config.survive_ms = 1000;
				Sim sim(config, std::vector<int>{0, 1, 2, 3});
				for (int i = 0; i < 60 && !sim.won(); ++i) {
					sim.step(std::nullopt);
				}
				return sim.won();
			}());
		check("and no stage still hides behind bare no_kicks",
			std::none_of(campaign::stages().begin(), campaign::stages().end(),
				[] (const Stage& stage) { return stage.no_kicks; }));
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
		all.sealed = (1 << 0) | (1 << 9);
		all.cold_iron = true;
		all.tempers = "thick_wick,bellows";
		const SimConfig raw = base();
		const SimConfig built = campaign::stage_config(all, raw, {});
		check("the stage's overrides all land",
			std::abs(built.fuse_base - (raw.fuse_base * 0.5 + 0.5)) < 1e-9
				&& built.fall_delay == 15 && built.cheese_holes == 3
				&& built.cheese_messiness == 70 && built.cheese_period == 200
				&& !built.kicks && built.cleartype == 2 && built.spin_rule == 1
				&& built.sealed == ((1 << 0) | (1 << 9)) && built.cold_iron
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

		// The fuse is a stage gimmick now, whatever the Rules tab says: a
		// plain room never burns, a recipe that names the burn always
		// does, and a duel always does - the fuse is the duel's tension.
		SimConfig lit = raw;
		lit.fuse = true;
		check("a plain stage never burns, even with Rules fuse on",
			!campaign::stage_config(quota, lit, {}).fuse);
		SimConfig off = raw;
		off.fuse = false;
		Stage burning = quota;
		burning.id = "b"; burning.fuse = true;
		check("a burn recipe burns, even with Rules fuse off",
			campaign::stage_config(burning, off, {}).fuse);
		Stage duel{};
		duel.id = "d"; duel.name = "d"; duel.blurb = "d";
		duel.mode = 5; duel.rank = 0;
		check("a duel always burns",
			campaign::stage_config(duel, off, {}).fuse);
		check("the road's burn rooms are exactly the ones that say so",
			[] {
				int burns = 0;
				for (const campaign::Stage& stage : campaign::stages()) {
					if (stage.fuse) {
						++burns;
					}
					// Pressure without the fuse would be a dial with no
					// meter: every pressure room must burn.
					if (stage.pressure && !stage.fuse) {
						return false;
					}
				}
				return burns == 2;
			}());

		Stage points{};
		points.id = "p"; points.name = "p"; points.blurb = "p";
		points.mode = 0; points.quota = 0; points.score_quota = 9000;
		check("a score stage's finish line is points",
			campaign::stage_config(points, raw, {}).score_quota == 9000
				&& campaign::stage_config(points, raw, {}).line_quota == 0);

		check("a score finish line is crossed by scoring",
			[] {
				SimConfig config;
				config.forced_delay = 0.;
				config.finesse_rule = 0;
				config.sdf = 40;
				config.clear_delay = false;
				config.score_quota = 400;
				Sim sim(config, std::vector<int>{0, 0, 0, 0});
				Board well;
				for (int y = kHeight - 1; y < kHeight; ++y) {
					for (int x = 0; x < kWidth; ++x) {
						if (x != kSpawnX + 1) {
							well.set(x, y, 3);
						}
					}
				}
				sim.seed(well);
				for (int i = 0; i < 100 && !sim.entry(); ++i) {
					sim.step(std::nullopt);
				}
				sim.step(Event{Key::Cw, true});
				sim.step(Event{Key::Cw, false});
				sim.step(Event{Key::Hard, true});
				sim.step(Event{Key::Hard, false});
				for (int i = 0; i < 20; ++i) {
					sim.step(std::nullopt);
				}
				return sim.won() && sim.score() >= 400;
			}());

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
			campaign::solo_stars(false, 10., 100, 0, true) == 0
				&& campaign::solo_stars(true, 150., 100, 3, true) == 1
				&& campaign::solo_stars(true, 90., 100, 3, true) == 2
				&& campaign::solo_stars(true, 150., 100, 0, true) == 2
				&& campaign::solo_stars(true, 90., 100, 0, true) == 3);
		check("pure-room stars: par is the second, three-quarter par the third",
			campaign::solo_stars(true, 150., 100, 0, false) == 1
				&& campaign::solo_stars(true, 90., 100, 0, false) == 2
				&& campaign::solo_stars(true, 70., 100, 0, false) == 3);
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
		climbing.run.oils = {"hot", "frost"};
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
				&& resumed.run.oils == climbing.run.oils
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
			// The chapter's trailing mode-5 block: the boss, and the
			// miniboss just before it when the chapter fields one.
			const int mini_stage = boss_stage - 1;
			const bool has_mini = campaign::stages()[
				static_cast<size_t>(mini_stage)].mode == 5;
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
				// Every node's kind and stage sit in its chapter's range:
				// battles draw from the chapter's battle window - never a
				// duel recipe - the boss is the boss, the miniboss is the
				// chapter's own on the row under the boss, and the map's
				// stops - exactly one forge, one or two events - live on
				// middle rows with no stage at all.
				int forges = 0;
				int events = 0;
				int minis = 0;
				for (const MapNode& node : map) {
					const bool boss_row
						= node.depth == campaign::kMapDepth - 1;
					if (node.kind == 1) {
						ranged = ranged && boss_row
							&& node.stage == boss_stage;
					} else if (node.kind == 0) {
						// The battle window may hold skirmishes and raids
						// (mode-5 recipes ahead of the trailing block) but
						// never the miniboss or the boss themselves.
						ranged = ranged && !boss_row
							&& node.stage >= battle_lo
							&& node.stage
								< (has_mini ? mini_stage : boss_stage);
					} else if (node.kind == 2 || node.kind == 3) {
						++(node.kind == 2 ? forges : events);
						ranged = ranged && node.depth > 0 && !boss_row
							&& node.stage == -1;
					} else if (node.kind == 4) {
						++minis;
						ranged = ranged && has_mini
							&& node.depth == campaign::kMapDepth - 2
							&& node.stage == mini_stage;
					} else {
						ranged = false;
					}
				}
				ranged = ranged && forges == 1
					&& events >= 1 && events <= 2
					&& minis == (has_mini ? 1 : 0);
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
