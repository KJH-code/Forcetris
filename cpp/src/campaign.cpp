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

const std::vector<Stage>& stages () {
	// The road. Chapter one teaches the forge one gimmick at a time;
	// chapter two turns each of them against the player. Every id is
	// frozen - it is the key the save file stores stars under.
	static const std::vector<Stage> road = [] {
		std::vector<Stage> list;
		Stage s{};

		// --- Chapter 1: The Outer Yard. --------------------------------
		s = Stage{};
		s.id = "c1s1"; s.name = "First Sparks";
		s.blurb = "Clear ten lines. The forge is patient, once.";
		s.mode = 0; s.quota = 10; s.par_seconds = 100;
		s.slag_first = 15; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s2"; s.name = "The Scrap Pile";
		s.blurb = "Old iron on the floor. Fifteen lines through it.";
		s.mode = 0; s.quota = 15; s.par_seconds = 130;
		s.board = "..77777.77\n777.777777\n7777777.77";
		s.slag_first = 18; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s3"; s.name = "The First Cut";
		s.blurb = "Ten rows of cheese, cut clean. Dig.";
		s.mode = 3; s.quota = 10; s.par_seconds = 110;
		s.cheese_holes = 1; s.cheese_messiness = 30;
		s.slag_first = 18; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s4"; s.name = "Loose Mortar";
		s.blurb = "Nothing holds: clears cascade. Fifteen lines.";
		s.mode = 0; s.quota = 15; s.par_seconds = 140;
		s.cleartype = 1;
		s.slag_first = 20; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s5"; s.name = "Rusted Joints";
		s.blurb = "The walls give nothing back: no kicks. Fifteen lines.";
		s.mode = 0; s.quota = 15; s.par_seconds = 150;
		s.no_kicks = true;
		s.slag_first = 20; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s6"; s.name = "The Rising Floor";
		s.blurb = "The floor climbs. Twelve lines before it takes you.";
		s.mode = 4; s.quota = 12; s.par_seconds = 150;
		s.cheese_period = 350;
		s.slag_first = 22; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s7"; s.name = "Backdraft";
		s.blurb = "The fuse burns hot the whole way. Twenty lines.";
		s.mode = 0; s.quota = 20; s.par_seconds = 170;
		s.pressure = true; s.fuse_scale = 0.9;
		s.slag_first = 24; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s8"; s.name = "The Gatekeeper";
		s.blurb = "The yard's keeper, and its blade. Beat it out.";
		s.mode = 5; s.rank = 1; s.first_to = 1;
		s.slag_first = 40; s.slag_repeat = 8;
		list.push_back(s);

		// --- Chapter 2: The Deep Forge. --------------------------------
		s = Stage{};
		s.id = "c2s1"; s.name = "Heavier Air";
		s.blurb = "Faster gravity, a shorter wick. Twenty lines.";
		s.mode = 0; s.quota = 20; s.par_seconds = 160;
		s.fuse_scale = 0.85; s.fall_delay = 20;
		s.slag_first = 26; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s2"; s.name = "Three Cuts";
		s.blurb = "Fourteen rows, three holes each, cut wild.";
		s.mode = 3; s.quota = 14; s.par_seconds = 150;
		s.cheese_holes = 3; s.cheese_messiness = 100;
		s.slag_first = 26; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s3"; s.name = "Chain Collapse";
		s.blurb = "Linked cascade, every twist scored. Twenty lines.";
		s.mode = 0; s.quota = 20; s.par_seconds = 170;
		s.cleartype = 2; s.spin_rule = 3;
		s.slag_first = 28; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s4"; s.name = "The Overheated Wing";
		s.blurb = "Overheat is already in your blood. Twenty lines, hot.";
		s.mode = 0; s.quota = 20; s.par_seconds = 160;
		s.pressure = true; s.fuse_scale = 0.8;
		s.tempers = "overheat";
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s5"; s.name = "The Buried Hall";
		s.blurb = "Deep rubble and rigid walls. Eighteen lines out.";
		s.mode = 0; s.quota = 18; s.par_seconds = 180;
		s.no_kicks = true;
		s.board = "7.77777777\n77777.7777\n777.777777\n7777777.77\n77.7777777";
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s6"; s.name = "The Flood";
		s.blurb = "The floor climbs faster here. Fifteen lines.";
		s.mode = 4; s.quota = 15; s.par_seconds = 180;
		s.cheese_period = 250;
		s.slag_first = 32; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s7"; s.name = "Cold Iron";
		s.blurb = "The deepest room: fast, short, unforgiving. 25 lines.";
		s.mode = 0; s.quota = 25; s.par_seconds = 200;
		s.fuse_scale = 0.75; s.fall_delay = 15;
		s.slag_first = 34; s.slag_repeat = 8;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s8"; s.name = "The Forgemaster";
		s.blurb = "Two falls against the master and the master's blade.";
		s.mode = 5; s.rank = 4; s.first_to = 2;
		s.blade = "bellows,white_heat,overheat,gamble";
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		return list;
	}();
	return road;
}

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
		} else {
			state.unknown.push_back(line);
		}
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
	for (const auto& [id, stars] : state.stars) {
		out << "stage " << id << " " << stars << "\n";
	}
	for (const auto& [id, level] : state.forge) {
		out << "forge " << id << " " << level << "\n";
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

namespace {

// The stage's own overrides, shared by both sides of a boss fight.
SimConfig overridden (const Stage& stage, SimConfig config) {
	config.fuse = true;   // A stage is a fuse game whatever the Rules say.
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
	if (stage.mode == 3) {
		config.cheese_total = stage.quota;
	} else if (stage.mode != 5 && stage.quota > 0) {
		config.line_quota = stage.quota;
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

int solo_stars (bool won, double seconds, int par_seconds, int forced) {
	if (!won) {
		return 0;
	}
	int stars = 1;
	if (par_seconds > 0 && seconds <= static_cast<double>(par_seconds)) {
		++stars;
	}
	if (forced == 0) {
		++stars;
	}
	return stars;
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
