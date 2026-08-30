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
		int chapter_seen = 0;
		for (const campaign::Chapter& chapter : campaign::chapters()) {
			chapters_named = chapters_named && chapter.id != nullptr
				&& chapter.id[0] != '\0' && chapter.name != nullptr
				&& chapter.name[0] != '\0' && chapter.blurb != nullptr
				&& chapter.stages > 0;
			chapters_unique = chapters_unique
				&& chapter_ids.insert(chapter.id ? chapter.id : "").second;
			counted += chapter.stages;
			bossed = bossed
				&& !campaign::chapter_bosses(chapter_seen).empty();
			++chapter_seen;
		}
		check("the chapter table covers the road exactly",
			counted == static_cast<int>(campaign::stages().size()));
		check("every chapter is named, non-empty and unique",
			chapters_named && chapters_unique);
		check("every chapter fields at least one boss", bossed);
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
		check("two sealed stages, three cold iron stages, bosses plain",
			sealed_stages == 2 && cold_stages == 3 && plain_bosses);
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
			score_stages == 3 && score_sane);
		int watches = 0;
		int raids = 0;
		int skirmishes = 0;
		{
			// A skirmish is a mode-5 recipe that is still a room: it is
			// fought on an ordinary node, not on the chapter's watch.
			for (int c = 0;
				c < static_cast<int>(campaign::chapters().size()); ++c) {
				for (const int at : campaign::chapter_rooms(c)) {
					const Stage& stage
						= campaign::stages()[static_cast<size_t>(at)];
					if (stage.mode == 5) {
						++(stage.raid != nullptr ? raids : skirmishes);
					}
				}
			}
			for (const Stage& stage : campaign::stages()) {
				if (stage.survive_seconds > 0) {
					++watches;
				}
			}
		}
		check("three watches, three skirmishes, two raids stand on the road",
			watches == 3 && skirmishes == 3 && raids == 2);
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
		// thick_wick feeds the fire rather than the wick now, so the
		// recipe's own wick scale stands alone in this sum.
		check("the stage's overrides all land",
			std::abs(built.fuse_base - raw.fuse_base * 0.5) < 1e-9
				&& std::abs(built.overdrive_refill - 0.5) < 1e-9
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
		// A duel is the one fight the road builds towards, and it plays
		// the board pure like everything else that does not name the
		// burn. Both sides: the bot's terms come through the same
		// override, so a fuse-less duel is fuse-less for the foe too.
		check("a duel never burns, on either side",
			!campaign::stage_config(duel, off, {}).fuse
				&& !campaign::bot_config(duel, off).fuse);
		SimConfig duel_lit = raw;
		duel_lit.fuse = true;
		check("and not even with the Rules fuse on",
			!campaign::stage_config(duel, duel_lit, {}).fuse);
		check("the road's burn rooms are exactly the ones that say so",
			[] {
				int burns = 0;
				for (const campaign::Stage& stage : campaign::stages()) {
					if (stage.fuse) {
						++burns;
					}
					// A duel's wick scale would be a dial wired to
					// nothing now that no duel burns.
					if (stage.mode == 5 && stage.fuse_scale != 1.) {
						return false;
					}
					// Pressure without the fuse would be a dial with no
					// meter: every pressure room must burn.
					if (stage.pressure && !stage.fuse) {
						return false;
					}
				}
				return burns == 3;
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
		// The wick and the bank were bought with slag by players who are
		// still playing, so the ids stayed and the metal moved with the
		// game: they feed the gauge now, which is live in every room.
		check("the Anvil's metal lands on the player's rules",
			std::abs(mine.flow_keep - (raw.flow_keep + 0.24)) < 1e-9
				&& std::abs(mine.flow_gain_dig - (raw.flow_gain_dig + 1.))
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

	// --- The ladder of foes. ------------------------------------------------
	// Every duel on the road is a rung of the bot ladder, and the rungs only
	// ever climb: within a chapter the skirmish is under the miniboss is
	// under the boss, and each chapter's boss stands over the last one's.
	// This is the whole difficulty curve of the campaign, so it is pinned
	// rather than left to whoever edits the table next.
	{
		bool climbs = true;
		bool banded = true;
		std::string detail;
		int previous_boss = -1;
		int base = 0;
		for (const auto& chapter : campaign::chapters()) {
			int softest = 99;   // The chapter's lightest duel...
			int boss = -1;      // ...and the ceiling its bosses stand on.
			for (int i = 0; i < chapter.stages; ++i) {
				const Stage& stage
					= campaign::stages()[static_cast<size_t>(base + i)];
				if (stage.mode != 5) {
					continue;
				}
				banded = banded && stage.rank >= 0 && stage.rank <= 7;
				// A raid's own foes ride no more than a rung over its own
				// entry rank: three fights with no second chance is priced
				// by the run of it, not by one of them.
				if (stage.raid != nullptr) {
					int foe = 0;
					for (const char* c = stage.raid; ; ++c) {
						if (*c >= '0' && *c <= '9') {
							foe = foe * 10 + (*c - '0');
							continue;
						}
						if (foe > stage.rank + 1 || foe > 7) {
							banded = false;
							detail += std::string(stage.id) + " raid foe "
								+ std::to_string(foe) + "; ";
						}
						foe = 0;
						if (*c == '\0') {
							break;
						}
					}
				}
				softest = std::min(softest, stage.rank);
				if (stage.role == campaign::kBoss) {
					boss = std::max(boss, stage.rank);
				}
			}
			if (boss < 0) {
				continue;
			}
			if (softest > boss || boss <= previous_boss) {
				climbs = false;
				detail += std::string(chapter.id) + " "
					+ std::to_string(softest) + ".." + std::to_string(boss)
					+ " after " + std::to_string(previous_boss) + "; ";
			}
			previous_boss = boss;
			base += chapter.stages;
		}
		check("the road's duels climb a rung at a time, chapter over chapter",
			climbs, detail);
		check("and every rung is on the ladder", banded, detail);
	}

	// --- The concept pairs. -------------------------------------------------
	// Every chapter fields three pairs - a miniboss and a boss who belong
	// together - and a run climbs to exactly one of them. The pairs stand on
	// the same rungs, so which face a seed rolls never changes how hard the
	// chapter is; what changes is the shape of the fight, and that is what
	// the blade weights say: the Tricksters trade metal for a fuller skill
	// kit, the Hammers buy metal by carrying none.
	{
		bool three = true;
		bool level = true;
		bool weighted = true;
		std::string detail;
		for (int chapter = 0;
			chapter < static_cast<int>(campaign::chapters().size());
			++chapter) {
			const std::vector<int> pairs = campaign::chapter_pairs(chapter);
			three = three && pairs.size() == 3;
			int mini_rank = -1;
			int boss_rank = -1;
			int mini_race = -1;
			int boss_race = -1;
			std::vector<int> blades;
			for (const int pair : pairs) {
				const int mini_at
					= campaign::pair_miniboss(chapter, pair);
				const int boss_at = campaign::pair_boss(chapter, pair);
				if (mini_at < 0 || boss_at < 0) {
					three = false;
					continue;
				}
				const Stage& mini
					= campaign::stages()[static_cast<size_t>(mini_at)];
				const Stage& boss
					= campaign::stages()[static_cast<size_t>(boss_at)];
				// Both halves fight the chapter's own kind of duel: the
				// first_to and the rank ladder are the chapter's, not the
				// pair's.
				if (mini_rank < 0) {
					mini_rank = mini.rank;
					boss_rank = boss.rank;
					mini_race = mini.first_to;
					boss_race = boss.first_to;
				}
				if (mini.rank != mini_rank || boss.rank != boss_rank
					|| mini.first_to != mini_race
					|| boss.first_to != boss_race) {
					level = false;
					detail += std::string(boss.id) + " off the ladder; ";
				}
				blades.push_back(
					static_cast<int>(campaign::blade_of(boss).size()));
			}
			// Three distinct weights: light, the Wardens' own, heavy.
			std::vector<int> sorted = blades;
			std::sort(sorted.begin(), sorted.end());
			if (sorted.size() != 3 || sorted[0] >= sorted[1]
				|| sorted[1] >= sorted[2]) {
				weighted = false;
				detail += std::string(campaign::chapters()[chapter].id)
					+ " blades level; ";
			}
		}
		check("every chapter fields three concept pairs", three, detail);
		check("the pairs stand on one ladder", level, detail);
		check("and they are told apart by the weight of their blades",
			weighted, detail);
		check("the pair a run meets is the seed's, and seeds disagree",
			[] {
				const auto boss_of = [] (const std::vector<
						campaign::MapNode>& map) {
					for (const campaign::MapNode& node : map) {
						if (node.kind == 1) {
							return node.stage;
						}
					}
					return -1;
				};
				if (boss_of(campaign::build_map(0, 4242u))
					!= boss_of(campaign::build_map(0, 4242u))) {
					return false;
				}
				std::set<int> faces;
				for (unsigned seed = 1; seed <= 60; ++seed) {
					faces.insert(boss_of(campaign::build_map(0, seed * 977u)));
				}
				return faces.size() == 3;
			}());
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
		bool paired = true;
		bool crossed = false;
		std::string detail;
		for (int chapter = 0;
			chapter < static_cast<int>(campaign::chapters().size());
			++chapter) {
			// The rooms are the battle window, asked by role; the watch
			// is whichever concept pair the seed rolls, so the map's own
			// boss and miniboss are read off the map and held to a pair.
			const std::vector<int> rooms
				= campaign::chapter_rooms(chapter);
			const std::vector<int> pairs
				= campaign::chapter_pairs(chapter);
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
				int boss_pair = -1;
				int mini_pair = -1;
				for (const MapNode& node : map) {
					const bool boss_row
						= node.depth == campaign::kMapDepth - 1;
					const Stage* stage = node.stage >= 0
						&& node.stage
							< static_cast<int>(campaign::stages().size())
						? &campaign::stages()[
							static_cast<size_t>(node.stage)]
						: nullptr;
					if (node.kind == 1) {
						ranged = ranged && boss_row && stage != nullptr
							&& stage->role == campaign::kBoss;
						if (stage != nullptr) {
							boss_pair = stage->pair;
						}
					} else if (node.kind == 0) {
						// The battle window may hold skirmishes and raids
						// - they are rooms too - but never the watch.
						ranged = ranged && !boss_row
							&& std::find(rooms.begin(), rooms.end(),
								node.stage) != rooms.end();
					} else if (node.kind == 2 || node.kind == 3) {
						++(node.kind == 2 ? forges : events);
						ranged = ranged && node.depth > 0 && !boss_row
							&& node.stage == -1;
					} else if (node.kind == 4) {
						++minis;
						ranged = ranged
							&& node.depth == campaign::kMapDepth - 2
							&& stage != nullptr
							&& stage->role == campaign::kMiniboss;
						if (stage != nullptr) {
							mini_pair = stage->pair;
						}
					} else {
						ranged = false;
					}
				}
				ranged = ranged && forges == 1
					&& events >= 1 && events <= 2
					&& minis <= 1 && !pairs.empty();
				// The watch is one concept: the miniboss the map seats
				// belongs to the same pair as the boss it climbs to.
				paired = paired
					&& (mini_pair < 0 || mini_pair == boss_pair);
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
				if ((!shaped || !connected || !ranged || !paired || crossed)
					&& detail.empty()) {
					detail = "chapter " + std::to_string(chapter) + " seed "
						+ std::to_string(seed * 977u);
				}
			}
		}
		check("every map is shaped 2 / 2..3 / 1 with honest kinds", shaped,
			detail);
		check("every battle draws from its chapter's rooms, every watch "
			"node holds the right role", ranged, detail);
		check("the miniboss the map seats shares the boss's pair", paired,
			detail);
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

	// --- The Endless Climb. -------------------------------------------------
	{
		using campaign::MapNode;
		// The pool the climb draws from, mirrored here independently of the
		// generator: every chapter's rooms in road order, the watch left
		// out.
		std::vector<int> pool;
		for (int c = 0;
			c < static_cast<int>(campaign::chapters().size()); ++c) {
			for (const int at : campaign::chapter_rooms(c)) {
				pool.push_back(at);
			}
		}
		const int span = static_cast<int>(pool.size());

		// Shape, connectivity, honesty and the gatekeeper rotation, over the
		// first rings and a spread of seeds - the same promises the chapter
		// maps are held to, plus the climb's own.
		bool shaped = true;
		bool connected = true;
		bool ranged = true;
		bool kept = true;
		bool crossed = false;
		std::string detail;
		for (int ring = 0; ring <= 9; ++ring) {
			// The rotation walks the road's watch a rung at a time -
			// chapter one's miniboss, chapter one's boss, and up - then
			// the White Heart's own two trade watches without end. Which
			// concept pair supplies the face is the ring's to roll, so
			// the pin holds the role and the chapter, not the id.
			const int watch = ring < 6 ? ring : (ring % 2 == 0 ? 4 : 5);
			const int keeper_chapter = watch / 2;
			const bool keeper_boss = watch % 2 == 1;
			for (unsigned seed = 1; seed <= 20; ++seed) {
				const std::vector<MapNode> map
					= campaign::build_endless_map(ring, seed * 977u);
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
				int forges = 0;
				int events = 0;
				for (const MapNode& node : map) {
					const bool top = node.depth == campaign::kMapDepth - 1;
					if (node.kind == 1 || node.kind == 4) {
						// The top row is the gatekeeper's alone: the ring's
						// own duel from the rotation, a boss kind for a boss
						// recipe and a miniboss kind for a miniboss.
						const Stage& keeper = campaign::stages()[
							static_cast<size_t>(node.stage)];
						kept = kept && top
							&& node.kind == (keeper_boss ? 1 : 4)
							&& keeper.role == (keeper_boss
								? campaign::kBoss : campaign::kMiniboss)
							&& campaign::spot_of(static_cast<size_t>(
								node.stage)).chapter == keeper_chapter;
					} else if (node.kind == 0) {
						// Every battle draws from the union pool - any
						// chapter's window recipe, never a duel-block one.
						ranged = ranged && !top
							&& std::find(pool.begin(), pool.end(), node.stage)
								!= pool.end();
					} else if (node.kind == 2 || node.kind == 3) {
						++(node.kind == 2 ? forges : events);
						ranged = ranged && node.depth > 0 && !top
							&& node.stage == -1;
					} else {
						ranged = false;
					}
				}
				ranged = ranged && forges == 1 && events >= 1 && events <= 2;
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
				if ((!shaped || !connected || !ranged || !kept || crossed)
					&& detail.empty()) {
					detail = "ring " + std::to_string(ring) + " seed "
						+ std::to_string(seed * 977u);
				}
			}
		}
		check("every ring is shaped 2 / 2..3 / 1 with honest kinds", shaped,
			detail);
		check("every climb battle draws from the union pool", ranged, detail);
		check("the gatekeeper rotation holds the top row", kept, detail);
		check("every ring node is reachable and reaches the gatekeeper",
			connected, detail);
		check("no two climb edges cross", !crossed, detail);
		check("past the taught road every battle is a hard one",
			[&] {
				// Deep in the climb the window sits at the pool's top: every
				// battle is one of the two hardest recipes on the road.
				for (unsigned seed = 1; seed <= 8; ++seed) {
					const auto map = campaign::build_endless_map(40, seed);
					for (const MapNode& node : map) {
						if (node.kind == 0
							&& node.stage != pool[static_cast<size_t>(span - 1)]
							&& node.stage
								!= pool[static_cast<size_t>(span - 2)]) {
							return false;
						}
					}
				}
				return true;
			}());
		check("the same ring and seed build the same layer",
			[] {
				const auto a = campaign::build_endless_map(3, 12345u);
				const auto b = campaign::build_endless_map(3, 12345u);
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
		check("each ring of one climb is a fresh layer",
			[] {
				for (unsigned seed = 1; seed <= 8; ++seed) {
					const auto a = campaign::build_endless_map(0, seed);
					const auto b = campaign::build_endless_map(1, seed);
					for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
						if (a[i].stage != b[i].stage || a[i].next != b[i].next) {
							return true;
						}
					}
				}
				return false;
			}());
		check("a ring below the ground builds no map",
			campaign::build_endless_map(-1, 1u).empty());

		// The tightening: rising rings never make a stage easier, and the
		// floors hold. A knob a stage left off stays off.
		{
			SimConfig base;
			base.fall_delay = 30;
			base.line_quota = 10;
			base.score_quota = 10000;
			base.survive_ms = 60000;
			base.cheese_period = 300;
			bool monotone = true;
			SimConfig before = campaign::endless_scaled(base, 0);
			for (int ring = 1; ring <= 30; ++ring) {
				const SimConfig now = campaign::endless_scaled(base, ring);
				monotone = monotone
					&& now.fall_delay <= before.fall_delay
					&& now.fall_delay >= 8
					&& now.line_quota >= before.line_quota
					&& now.score_quota >= before.score_quota
					&& now.survive_ms >= before.survive_ms
					&& now.cheese_period <= before.cheese_period
					&& now.cheese_period >= 120;
				before = now;
			}
			SimConfig off;
			off.line_quota = 0;
			off.score_quota = 0;
			off.survive_ms = 0;
			const SimConfig still = campaign::endless_scaled(off, 12);
			check("the climb only ever tightens, down to the floors",
				monotone
					&& campaign::endless_scaled(base, 0).fall_delay == 30
					&& campaign::endless_scaled(base, 1).fall_delay == 28
					&& campaign::endless_scaled(base, 1).line_quota == 12);
			check("a finish line a stage left off stays off",
				still.line_quota == 0 && still.score_quota == 0
					&& still.survive_ms == 0);
		}
		check("a duel foe climbs half a rank per ring, capped at the top",
			campaign::endless_rank(3, 0) == 3
				&& campaign::endless_rank(3, 2) == 4
				&& campaign::endless_rank(3, 4) == 5
				&& campaign::endless_rank(7, 1) == 7
				&& campaign::endless_rank(3, 40) == 7);

		// What the smith charges, as the climb goes on. A price at the door
		// is the door price; every rung after it costs more, and the
		// ceiling is what keeps the last chapter a shop rather than a
		// museum.
		{
			campaign::Run door;
			door.active = true;
			door.chapter = 0;
			door.depth = 0;
			check("a price at the door is the price on the card",
				campaign::priced(temper::kRerollCost, door)
					== temper::kRerollCost
					&& campaign::priced(temper::kLifeCost, door)
						== temper::kLifeCost);
			// A run that is not under way is not being charged for one.
			campaign::Run none;
			check("and no run is charged nothing extra",
				campaign::priced(temper::kRerollCost, none)
					== temper::kRerollCost);
			bool climbs = true;
			bool capped = true;
			int last = 0;
			std::string detail;
			for (int chapter = 0; chapter < 3; ++chapter) {
				for (int depth = 0; depth < campaign::kMapDepth; ++depth) {
					campaign::Run run;
					run.active = true;
					run.chapter = chapter;
					run.depth = depth;
					const int now = campaign::priced(temper::kLifeCost, run);
					if (now < last) {
						climbs = false;
						detail += std::to_string(now) + " after "
							+ std::to_string(last) + "; ";
					}
					capped = capped && now <= temper::kLifeCost * 3;
					last = now;
				}
			}
			check("the road's prices only ever climb", climbs, detail);
			check("and never past three times the door", capped);
			// The climb charges by the rows it has climbed, so a deep ring
			// is dearer than a shallow one - and it too stops at the cap.
			campaign::Run shallow;
			shallow.active = true;
			shallow.endless = true;
			shallow.ring = 0;
			campaign::Run deep;
			deep.active = true;
			deep.endless = true;
			deep.ring = 3;
			campaign::Run deeper;
			deeper.active = true;
			deeper.endless = true;
			deeper.ring = 40;
			check("the climb charges by the rings behind you",
				campaign::priced(temper::kRerollCost, shallow)
					< campaign::priced(temper::kRerollCost, deep)
					&& campaign::priced(temper::kRerollCost, deeper)
						== temper::kRerollCost * 3);
		}

		// The gate and the record's arithmetic.
		{
			campaign::State fresh;
			// Any of the Deep Forge's three masters opens the climb: a run
			// only ever meets one of them, so the gate cannot name a face.
			bool any_opens = !campaign::chapter_bosses(1).empty();
			bool none_opens_starless = true;
			for (const int at : campaign::chapter_bosses(1)) {
				const char* id = campaign::stages()[
					static_cast<size_t>(at)].id;
				campaign::State starless;
				starless.stars[id] = 0;
				campaign::State keyed;
				keyed.stars[id] = 1;
				none_opens_starless = none_opens_starless
					&& !campaign::endless_open(starless);
				any_opens = any_opens && campaign::endless_open(keyed);
			}
			check("the climb opens once a Deep Forge master has fallen",
				!campaign::endless_open(fresh) && none_opens_starless
					&& any_opens);
			campaign::Run run;
			run.ring = 2;
			run.depth = 3;
			check("the record counts full rings plus the rows in progress",
				campaign::endless_rows(run)
					== 2 * campaign::kMapDepth + 3);
		}

		// The climb rides the save file: the run's ring and the record board
		// round-trip, and a bent ring is clamped into sense.
		{
			namespace fs = std::filesystem;
			std::error_code ignored;
			const fs::path folder
				= fs::temp_directory_path() / "forcetris-camp-endless";
			fs::remove_all(folder, ignored);
			fs::create_directories(folder, ignored);
			const std::string file = (folder / "campaign.dat").string();
			campaign::State state;
			state.endless_best = 27;
			state.run.active = true;
			state.run.endless = true;
			state.run.ring = 4;
			state.run.seed = 0xF00Du;
			state.run.difficulty = campaign::kWhite;
			state.run.depth = 1;
			state.run.path = {0};
			// What the grade is made of rides the file too: a climb
			// resumed after a restart must not have forgotten what it
			// already cost.
			state.run.seconds = 934;
			state.run.deaths = 3;
			check("a climb saves", campaign::save(file, state));
			const campaign::State back = campaign::load(file);
			check("and resumes on its ring with its record",
				back.endless_best == 27 && back.run.active
					&& back.run.endless && back.run.ring == 4
					&& back.run.seed == 0xF00Du);
			check("and remembers what the climb has cost so far",
				back.run.seconds == 934 && back.run.deaths == 3,
				std::to_string(back.run.seconds) + "s, "
					+ std::to_string(back.run.deaths) + " deaths");
			state.run.active = false;
			check("the record outlives the run",
				campaign::save(file, state)
					&& campaign::load(file).endless_best == 27
					&& !campaign::load(file).run.active
					&& !campaign::load(file).run.endless);
			{
				std::ofstream out(file, std::ios::trunc);
				out << "run_seed 7\nrun_endless 1\nrun_ring 12345\n"
					<< "endless_best -9\n";
			}
			const campaign::State bent = campaign::load(file);
			check("a bent climb is clamped into sense",
				bent.run.active && bent.run.endless
					&& bent.run.ring == 999 && bent.endless_best == 0);
			fs::remove_all(folder, ignored);
		}
	}

	// --- What each stage says it wants. -------------------------------------
	// The goal line is read off the recipe rather than written by hand, so
	// the pin is that it always says something, always carries the number
	// the stage actually enforces, and never offers a target of nothing.
	{
		bool spoken = true;
		bool numbered = true;
		std::string detail;
		for (const campaign::Stage& stage : campaign::stages()) {
			const std::string line = campaign::goal_line(stage);
			if (line.empty() || line.back() != '.') {
				spoken = false;
				detail += std::string(stage.id) + " says nothing; ";
				continue;
			}
			// The number in the sentence has to be the stage's own.
			const long long want = stage.mode == 5 ? 0
				: stage.mode == 4 && stage.survive_seconds > 0
					? stage.survive_seconds
				: stage.score_quota > 0 ? stage.score_quota : stage.quota;
			if (want <= 0 && stage.mode != 5) {
				numbered = false;
				detail += std::string(stage.id) + " asks for nothing; ";
				continue;
			}
			if (stage.mode != 5) {
				// Written back the way the line groups it, so a five-figure
				// score matches "16,000" and not "16000".
				std::string digits = std::to_string(want);
				for (int at = static_cast<int>(digits.size()) - 3; at > 0;
						at -= 3) {
					digits.insert(static_cast<size_t>(at), ",");
				}
				if (line.find(digits) == std::string::npos) {
					numbered = false;
					detail += std::string(stage.id) + " lost its number ("
						+ digits + " not in \"" + line + "\"); ";
				}
			}
		}
		check("every stage says what it wants, in a sentence", spoken,
			detail);
		check("and the number it says is the number it enforces", numbered,
			detail);
		// A duel says how many falls it takes rather than a quota - except
		// a raid, whose first_to is the length of the gauntlet and not a
		// number of rounds, and which says so in words instead.
		{
			const campaign::Stage* boss = nullptr;
			const campaign::Stage* pack = nullptr;
			for (const campaign::Stage& stage : campaign::stages()) {
				if (stage.mode != 5) {
					continue;
				}
				if (stage.raid != nullptr) {
					pack = pack != nullptr ? pack : &stage;
				} else if (stage.first_to > 1 && boss == nullptr) {
					boss = &stage;
				}
			}
			check("and a duel counts rounds instead",
				boss != nullptr && campaign::goal_line(*boss).find(
					std::to_string(boss->first_to)) != std::string::npos,
				boss != nullptr ? campaign::goal_line(*boss) : "no boss");
			check("while a raid counts foes, not rounds",
				pack != nullptr && campaign::goal_line(*pack).find("foe")
					!= std::string::npos,
				pack != nullptr ? campaign::goal_line(*pack) : "no raid");
		}
	}

	// --- The climb's own grade. ---------------------------------------------
	// A run is graded on run facts - rows, deaths, battle seconds - and not
	// on one board's TETR.IO estimate. Every pin below is a shape the grade
	// has to hold whatever the numbers are tuned to, so a retune can move
	// the letters without quietly inverting the meaning.
	{
		const auto run_of = [] (int depth, int deaths, int seconds,
				int difficulty) {
			campaign::Run run;
			run.active = true;
			run.depth = depth;
			run.deaths = deaths;
			run.seconds = seconds;
			run.difficulty = difficulty;
			return run;
		};
		const campaign::Verdict clean = campaign::grade_run(
			run_of(campaign::kMapDepth, 0, 600, campaign::kForged), true);
		const campaign::Verdict bloody = campaign::grade_run(
			run_of(2, 3, 2400, campaign::kForged), false);
		check("a clean fast climb outgrades a slow bloody short one",
			clean.score > bloody.score,
			std::to_string(clean.score) + " vs "
				+ std::to_string(bloody.score));
		check("and the road taken is marked as taken",
			clean.finished && !bloody.finished);

		// Monotone in the two things a player controls. A death can never
		// help, and neither can spending longer.
		{
			bool sane = true;
			std::string detail;
			for (int deaths = 0; deaths < 5; ++deaths) {
				const int here = campaign::grade_run(
					run_of(4, deaths, 900, campaign::kForged), false).score;
				const int worse = campaign::grade_run(
					run_of(4, deaths + 1, 900, campaign::kForged),
					false).score;
				if (worse > here) {
					sane = false;
					detail += std::to_string(deaths) + " deaths graded up; ";
				}
			}
			for (int mins = 2; mins < 40; mins += 4) {
				const int here = campaign::grade_run(
					run_of(4, 1, mins * 60, campaign::kForged), false).score;
				const int slower = campaign::grade_run(
					run_of(4, 1, (mins + 4) * 60, campaign::kForged),
					false).score;
				if (slower > here) {
					sane = false;
					detail += std::to_string(mins) + "min graded up; ";
				}
			}
			check("a death never raises a grade, and neither does time",
				sane, detail);
		}
		// And monotone in the two the map controls: deeper is better, and
		// the same climb at a hotter fire is worth at least as much.
		{
			bool climbs = true;
			for (int depth = 0; depth < campaign::kMapDepth; ++depth) {
				climbs = climbs
					&& campaign::grade_run(
						run_of(depth, 1, 600, campaign::kForged), false).score
					<= campaign::grade_run(
						run_of(depth + 1, 1, 600, campaign::kForged),
						false).score;
			}
			check("a deeper climb never grades lower", climbs);
			const int mild = campaign::grade_run(
				run_of(4, 1, 900, campaign::kMild), false).score;
			const int forged = campaign::grade_run(
				run_of(4, 1, 900, campaign::kForged), false).score;
			const int white = campaign::grade_run(
				run_of(4, 1, 900, campaign::kWhite), false).score;
			check("and a hotter fire is worth at least as much",
				mild <= forged && forged <= white,
				std::to_string(mild) + "/" + std::to_string(forged) + "/"
					+ std::to_string(white));
		}
		// The floor and the ceiling both hold, and a blank run does not
		// reach into anything it should not.
		{
			const campaign::Verdict nothing
				= campaign::grade_run(campaign::Run{}, false);
			const campaign::Verdict best = campaign::grade_run(
				run_of(campaign::kMapDepth, 0, 60, campaign::kWhite), true);
			check("a blank run grades at the floor and survives it",
				nothing.score >= 0 && nothing.rows == 0
					&& nothing.grade[0] == 'D');
			check("and the grade never leaves 0..100",
				best.score <= 100 && best.score > 0,
				std::to_string(best.score));
		}
	}

	// --- Difficulty and the chapter gate. -----------------------------------
	{
		check("slag scales with the weight of death",
			campaign::slag_percent(campaign::kMild) == 100
				&& campaign::slag_percent(campaign::kForged) == 150
				&& campaign::slag_percent(campaign::kWhite) == 200);
		// And so does the foe. Forged fights the recipe's own number and
		// white is one rung up; mild is two rungs down and capped, which
		// is a different rule and needs its own pin.
		const int last = static_cast<int>(bot::ranks().size()) - 1;
		check("forged is the recipe and white is a rung above it",
			campaign::rank_for(6, campaign::kForged) == 6
				&& campaign::rank_for(6, campaign::kWhite) == 7);
		check("the gentlest fire drops two rungs",
			campaign::rank_for(6, campaign::kMild) == 4
				&& campaign::rank_for(5, campaign::kMild) == 3
				&& campaign::rank_for(3, campaign::kMild) == 1);
		// The ceiling is the whole point of the arc: two rungs off chapter
		// three's SS bosses is still A, and A is not a fight a player who
		// picked the gentlest fire can win. Nothing on mild goes above B.
		{
			bool capped = true;
			for (int rank = 0; rank <= last; ++rank) {
				capped = capped && campaign::rank_for(rank, campaign::kMild)
					<= campaign::kMildCeiling;
			}
			check("and never fields worse than B, whatever the recipe asks",
				capped && campaign::rank_for(last, campaign::kMild)
					== campaign::kMildCeiling);
		}
		// The shipped road, walked: what a beginner who picks the gentlest
		// fire is actually handed, stage by stage. This is the pin that
		// would have caught the road ending on S at the easiest setting.
		{
			int worst = -1;
			int first = -1;
			for (const campaign::Stage& stage : campaign::stages()) {
				if (stage.mode != 5) {
					continue;
				}
				const int fielded = campaign::rank_for(stage.rank,
					campaign::kMild);
				worst = std::max(worst, fielded);
				if (first < 0) {
					first = fielded;
				}
			}
			check("no duel on the gentlest fire climbs past B",
				worst >= 0 && worst <= campaign::kMildCeiling,
				worst >= 0 ? bot::ranks()[static_cast<size_t>(worst)].name
					: "no duels");
			// And the road opens below the league, not at it - a real TL
			// average out-paces someone meeting the game this week.
			check("and the road opens below the league",
				first >= 0 && first < 2,
				first >= 0 ? bot::ranks()[static_cast<size_t>(first)].name
					: "no duels");
		}
		check("and never off the ladder",
			campaign::rank_for(0, campaign::kMild) == 0
				&& campaign::rank_for(last, campaign::kWhite) == last
				&& campaign::rank_for(-3, campaign::kMild) == 0
				&& campaign::rank_for(99, campaign::kWhite) == last);
		// Monotone in the fire: a hotter run never fields an easier foe.
		{
			bool climbs = true;
			for (int rank = 0; rank <= last; ++rank) {
				climbs = climbs
					&& campaign::rank_for(rank, campaign::kMild)
						<= campaign::rank_for(rank, campaign::kForged)
					&& campaign::rank_for(rank, campaign::kForged)
						<= campaign::rank_for(rank, campaign::kWhite);
			}
			check("a hotter fire never fields an easier foe", climbs);
		}
		check("difficulty names round-trip",
			campaign::difficulty_from(
					campaign::difficulty_name(campaign::kWhite))
					== campaign::kWhite
				&& campaign::difficulty_from(
					campaign::difficulty_name(campaign::kForged))
					== campaign::kForged
				&& campaign::difficulty_from("gibberish") == campaign::kMild);
		campaign::State fresh;
		bool gated = campaign::chapter_open(fresh, 0)
			&& !campaign::chapter_open(fresh, 1)
			&& !campaign::chapter_bosses(0).empty();
		// Whichever of chapter one's bosses a run climbed to, beating it
		// is beating the chapter.
		for (const int at : campaign::chapter_bosses(0)) {
			campaign::State keyed;
			keyed.stars[campaign::stages()[static_cast<size_t>(at)].id] = 1;
			gated = gated && campaign::chapter_open(keyed, 1)
				&& !campaign::chapter_open(keyed, 99);
		}
		check("the first chapter is open, the second waits for any boss",
			gated);
	}

	std::printf("%s\n",
		failures == 0 ? "all campaign checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
