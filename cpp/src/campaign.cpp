#include "forcetris/campaign.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "forcetris/temper.hpp"

namespace forcetris {
namespace campaign {

namespace fs = std::filesystem;

namespace {

// Comma-separated ids out of a recipe string.
std::vector<std::string> split_ids (const char* text) {
	std::vector<std::string> ids;
	if (text == nullptr) {
		return ids;
	}
	std::istringstream in(text);
	std::string id;
	while (std::getline(in, id, ',')) {
		if (!id.empty()) {
			ids.push_back(id);
		}
	}
	return ids;
}

} // namespace

// The chapters and the stage recipes live in stages.cpp - content on its
// own page, so growing the road never touches the machinery here.

const std::vector<Upgrade>& anvil () {
	// Levels are deliberately shallow: the Anvil is a leg up the road, not
	// a treadmill. Wick/bank/bellows land on the stage's rules in
	// apply-order below; sense and preheat are read by the GUI.
	static const std::vector<Upgrade> forge = {
		{"wick", "Forged Wick", "pieces burn longer in stages", 3, 25},
		{"bank", "Deep Bank", "clears refill more fuse in stages", 2, 30},
		{"bellows", "Great Bellows", "Overdrive lasts longer in stages", 2, 30},
		{"sense", "Ember Sense", "earn more embers in stages", 2, 35},
		{"preheat", "Preheat", "start every stage with a free draft", 1, 60},
		// The V2.1d pair, read by begin_run: a life for the forged runs,
		// and coin in the purse before the first fight.
		{"lifeblood", "Forged Lifeblood",
			"forged runs carry one more life", 1, 70},
		{"warchest", "War Chest",
			"runs set out with embers in the purse", 2, 40},
	};
	return forge;
}

const Upgrade* upgrade (const std::string& id) {
	for (const Upgrade& entry : anvil()) {
		if (id == entry.id) {
			return &entry;
		}
	}
	return nullptr;
}

std::string path (const std::string& root) {
	if (const char* forced = std::getenv("FORCETRIS_CAMPAIGN")) {
		return forced;
	}
	return (fs::path(root) / "data" / "campaign.dat").string();
}

State load (const std::string& path) {
	State state;
	std::ifstream source(path);
	if (!source) {
		return state;
	}
	std::string line;
	while (std::getline(source, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream in(line);
		std::string key;
		in >> key;
		if (key == "stage") {
			std::string id;
			int stars = 0;
			in >> id >> stars;
			if (!id.empty() && !in.fail()) {
				state.stars[id] = std::clamp(stars, 0, 3);
			}
		} else if (key == "slag") {
			in >> state.slag;
			state.slag = std::max(0, state.slag);
		} else if (key == "forge") {
			std::string id;
			int level = 0;
			in >> id >> level;
			if (!id.empty() && !in.fail()) {
				// Clamp to what the Anvil actually sells; an unknown id is
				// kept as bought - a newer build's upgrade must survive a
				// round trip through this one.
				const Upgrade* sold = upgrade(id);
				state.forge[id] = sold != nullptr
					? std::clamp(level, 0, sold->levels)
					: std::max(0, level);
			}
		} else if (key == "difficulty") {
			std::string name;
			in >> name;
			state.run.difficulty = difficulty_from(name);
		} else if (key == "run_chapter") {
			in >> state.run.chapter;
			state.run.chapter = std::clamp(state.run.chapter, 0,
				static_cast<int>(chapters().size()) - 1);
		} else if (key == "run_seed") {
			// The seed's presence is what says a climb is under way: every
			// other run key without it is leftovers, not a run.
			in >> state.run.seed;
			state.run.active = !in.fail();
		} else if (key == "run_depth") {
			in >> state.run.depth;
			state.run.depth = std::clamp(state.run.depth, 0, kMapDepth);
		} else if (key == "run_path") {
			std::string csv;
			in >> csv;
			state.run.path.clear();
			std::istringstream nodes(csv);
			std::string one;
			while (std::getline(nodes, one, ',')) {
				if (!one.empty()) {
					state.run.path.push_back(
						std::max(0, std::atoi(one.c_str())));
				}
			}
		} else if (key == "run_tempers") {
			std::string csv;
			in >> csv;
			state.run.tempers = split_ids(csv.c_str());
		} else if (key == "run_oils") {
			std::string csv;
			in >> csv;
			state.run.oils = split_ids(csv.c_str());
		} else if (key == "run_embers") {
			in >> state.run.embers;
			state.run.embers = std::max(0, state.run.embers);
		} else if (key == "run_lives") {
			in >> state.run.lives;
			state.run.lives = std::clamp(state.run.lives, 0, 9);
		} else if (key == "run_endless") {
			int flag = 0;
			in >> flag;
			state.run.endless = flag != 0;
		} else if (key == "run_ring") {
			in >> state.run.ring;
			state.run.ring = std::clamp(state.run.ring, 0, 999);
		} else if (key == "endless_best") {
			in >> state.endless_best;
			state.endless_best = std::max(0, state.endless_best);
		} else {
			state.unknown.push_back(line);
		}
	}
	if (!state.run.active) {
		// Leftover run keys with no seed reset to a clean no-run state, so
		// a half-erased file can never revive a ghost climb.
		state.run = Run{};
	} else {
		// The depth is the number of rows already fought, which is also
		// the path's length - held to it, so a mangled file cannot claim
		// a height it never climbed.
		state.run.depth = std::min(state.run.depth,
			static_cast<int>(state.run.path.size()));
	}
	return state;
}

bool save (const std::string& path, const State& state) {
	std::error_code ignored;
	fs::create_directories(fs::path(path).parent_path(), ignored);
	std::ofstream out(path, std::ios::trunc);
	if (!out) {
		return false;
	}
	out << "# forcetris campaign 1\n";
	out << "slag " << state.slag << "\n";
	if (state.endless_best > 0) {
		out << "endless_best " << state.endless_best << "\n";
	}
	for (const auto& [id, stars] : state.stars) {
		out << "stage " << id << " " << stars << "\n";
	}
	for (const auto& [id, level] : state.forge) {
		out << "forge " << id << " " << level << "\n";
	}
	if (state.run.active) {
		// The run rides in the same file; when there is none the keys are
		// simply not written, which is how ending a run erases it.
		out << "difficulty " << difficulty_name(state.run.difficulty) << "\n";
		out << "run_chapter " << state.run.chapter << "\n";
		out << "run_seed " << state.run.seed << "\n";
		out << "run_depth " << state.run.depth << "\n";
		if (!state.run.path.empty()) {
			out << "run_path ";
			for (size_t i = 0; i < state.run.path.size(); ++i) {
				out << (i > 0 ? "," : "") << state.run.path[i];
			}
			out << "\n";
		}
		if (!state.run.tempers.empty()) {
			out << "run_tempers ";
			for (size_t i = 0; i < state.run.tempers.size(); ++i) {
				out << (i > 0 ? "," : "") << state.run.tempers[i];
			}
			out << "\n";
		}
		if (!state.run.oils.empty()) {
			out << "run_oils ";
			for (size_t i = 0; i < state.run.oils.size(); ++i) {
				out << (i > 0 ? "," : "") << state.run.oils[i];
			}
			out << "\n";
		}
		out << "run_embers " << state.run.embers << "\n";
		out << "run_lives " << state.run.lives << "\n";
		if (state.run.endless) {
			out << "run_endless 1\n";
			out << "run_ring " << state.run.ring << "\n";
		}
	}
	for (const std::string& line : state.unknown) {
		out << line << "\n";
	}
	return out.good();
}

bool open (const State& state, size_t stage) {
	if (stage >= stages().size()) {
		return false;
	}
	if (stage == 0) {
		return true;
	}
	const auto before = state.stars.find(stages()[stage - 1].id);
	return before != state.stars.end() && before->second > 0;
}

bool chapter_open (const State& state, int chapter) {
	if (chapter < 0 || chapter >= static_cast<int>(chapters().size())) {
		return false;
	}
	if (chapter == 0) {
		return true;
	}
	// The previous chapter's boss is its last flat recipe; a star on it -
	// any star - opens the door.
	int boss = -1;
	for (int c = 0; c < chapter; ++c) {
		boss += chapters()[static_cast<size_t>(c)].stages;
	}
	const auto held = state.stars.find(stages()[static_cast<size_t>(boss)].id);
	return held != state.stars.end() && held->second > 0;
}

int slag_percent (int difficulty) {
	return difficulty == kWhite ? 200 : difficulty == kForged ? 150 : 100;
}

int rank_for (int rank, int difficulty) {
	// One rung either side of the recipe, and never off the ladder. A rung
	// is a real step - the bot thinks wider and drops faster with each -
	// so this is felt without any recipe being rewritten, and the recipe
	// stays the honest middle it was balanced as.
	const int shift = difficulty == kWhite ? 1 : difficulty == kMild ? -1 : 0;
	return std::clamp(rank + shift, 0, 7);
}

const char* difficulty_name (int difficulty) {
	return difficulty == kWhite ? "white"
		: difficulty == kForged ? "forged" : "mild";
}

int difficulty_from (const std::string& name) {
	return name == "white" ? kWhite : name == "forged" ? kForged : kMild;
}

namespace {

// The stage's own overrides, shared by both sides of a boss fight.
SimConfig overridden (const Stage& stage, SimConfig config) {
	// The fuse is a stage gimmick, whatever the Rules tab says: most rooms
	// play the board pure - the forced drop was the beginners' wall - and
	// only the recipes that name the burn still burn. A duel always does:
	// the fuse is the duel's own tension, Overdrive and heat pressure
	// with it.
	config.fuse = stage.fuse || stage.mode == 5;
	config.fuse_base *= stage.fuse_scale;
	config.fuse_base = std::max(config.fuse_min, config.fuse_base);
	if (stage.fall_delay >= 1) {
		config.fall_delay = stage.fall_delay;
	}
	if (stage.cheese_holes >= 1) {
		config.cheese_holes = stage.cheese_holes;
	}
	if (stage.cheese_messiness >= 0) {
		config.cheese_messiness = stage.cheese_messiness;
	}
	if (stage.cheese_period >= 1) {
		config.cheese_period = stage.cheese_period;
	}
	if (stage.no_kicks) {
		config.kicks = false;
	}
	if (stage.cleartype >= 0) {
		config.cleartype = stage.cleartype;
	}
	if (stage.spin_rule >= 0) {
		config.spin_rule = stage.spin_rule;
	}
	if (stage.sealed != 0) {
		config.sealed = stage.sealed;
	}
	if (stage.cold_iron) {
		config.cold_iron = true;
	}
	if (stage.mode == 3) {
		config.cheese_total = stage.quota;
	} else if (stage.mode != 5 && stage.quota > 0
		&& stage.survive_seconds == 0) {
		// Under a watch the quota is the star bar, not a finish line.
		config.line_quota = stage.quota;
	}
	if (stage.mode != 5 && stage.score_quota > 0) {
		config.score_quota = stage.score_quota;
	}
	if (stage.mode != 5 && stage.survive_seconds > 0) {
		config.survive_ms = stage.survive_seconds * 1000;
	}
	return config;
}

} // namespace

SimConfig stage_config (const Stage& stage, SimConfig base,
		const std::map<std::string, int>& forge) {
	SimConfig config = overridden(stage, base);
	config = temper::tempered(config, split_ids(stage.tempers));
	// The Anvil, last: permanent metal on top of the stage's terms.
	const auto level = [&forge] (const char* id) {
		const auto found = forge.find(id);
		return found != forge.end() ? found->second : 0;
	};
	config.fuse_base += 0.1 * level("wick");
	config.fuse_refuel_line += 0.05 * level("bank");
	config.overdrive_secs += 0.5 * level("bellows");
	return config;
}

SimConfig bot_config (const Stage& stage, SimConfig base) {
	// The stage's terms and nothing of the player's: no pre-applied
	// tempers, no Anvil. The blade goes on in the versus wiring.
	return overridden(stage, base);
}

std::vector<std::string> blade_of (const Stage& stage) {
	if (stage.blade != nullptr) {
		return split_ids(stage.blade);
	}
	return temper::blade_for(std::max(0, stage.rank));
}

std::vector<std::string> board_rows (const Stage& stage) {
	std::vector<std::string> rows;
	if (stage.board == nullptr) {
		return rows;
	}
	std::istringstream in(stage.board);
	std::string row;
	while (std::getline(in, row)) {
		if (!row.empty()) {
			rows.push_back(row);
		}
	}
	return rows;
}

bool endless_open (const State& state) {
	// The Deep Forge's master must have fallen at least once: the climb
	// draws on every chapter's rooms, so it waits for the shipped road's
	// end - not the White Heart's, which is late-game of its own.
	const auto held = state.stars.find("c2s8");
	return held != state.stars.end() && held->second > 0;
}

int endless_rows (const Run& run) {
	return run.ring * kMapDepth + run.depth;
}

SimConfig endless_scaled (SimConfig config, int ring) {
	// The climb only ever tightens. Gravity gains two frames a ring down
	// to a floor a human can still read; the finish lines stretch; the
	// flood quickens. campaigncheck holds the monotonicity.
	ring = std::max(0, ring);
	config.fall_delay = std::max(8, config.fall_delay - 2 * ring);
	if (config.line_quota > 0) {
		config.line_quota += 2 * ring;
	}
	if (config.score_quota > 0) {
		config.score_quota += config.score_quota * ring / 5;
	}
	if (config.survive_ms > 0) {
		config.survive_ms += ring * 10000;
	}
	config.cheese_period = std::max(120, config.cheese_period - 20 * ring);
	return config;
}

int endless_rank (int rank, int ring) {
	// Half a rank per ring, capped at the ladder's top rung (X sits at
	// index 7 - bot::ranks() is not included here on purpose, the cap is
	// part of the climb's contract and pinned in campaigncheck).
	return std::min(7, std::max(0, rank) + std::max(0, ring) / 2);
}

int solo_stars (bool won, double seconds, int par_seconds, int forced,
		bool fused) {
	if (!won) {
		return 0;
	}
	int stars = 1;
	if (par_seconds > 0 && seconds <= static_cast<double>(par_seconds)) {
		++stars;
	}
	// The third star: in a burn room, an untouched run - the fuse never
	// once slammed a piece down. In a pure room there is no fuse to dodge,
	// so the mastery mark is pace instead: well inside the par.
	if (fused ? forced == 0
		: par_seconds > 0 && seconds <= par_seconds * 0.75) {
		++stars;
	}
	return stars;
}

int survive_stars (bool won, int lines, int bar) {
	// A watch has no par to race - the clock is fixed - so the mastery
	// marks are what was cleared while holding on: the bar for the
	// second star, twice the bar for the third.
	if (!won) {
		return 0;
	}
	return 1 + (bar > 0 && lines >= bar ? 1 : 0)
		+ (bar > 0 && lines >= bar * 2 ? 1 : 0);
}

int boss_stars (bool won, bool sweep, bool ignited) {
	if (!won) {
		return 0;
	}
	return 1 + (sweep ? 1 : 0) + (sweep && ignited ? 1 : 0);
}

int slag_award (const Stage& stage, bool first_clear, bool won, int stars,
		int embers_left) {
	if (won) {
		return (first_clear ? stage.slag_first : stage.slag_repeat)
			+ stars * 5;
	}
	// The prestige remnant: what the run did not spend renders down. The
	// embers already scale with how deep the run got - they were earned by
	// its lines and its attack - so depth needs no second term.
	return std::max(0, embers_left) / 5;
}

int ember_bonus_percent (const std::map<std::string, int>& forge) {
	const auto found = forge.find("sense");
	return found != forge.end() ? found->second * 25 : 0;
}

int free_drafts (const std::map<std::string, int>& forge) {
	const auto found = forge.find("preheat");
	return found != forge.end() && found->second > 0 ? 1 : 0;
}

int upgrade_cost (const Upgrade& upgrade, int level) {
	return upgrade.cost_base * std::max(1, level);
}

} // namespace campaign
} // namespace forcetris
