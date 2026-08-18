#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <SDL.h>

#include "stats.hpp"

namespace forcetris {
namespace gui {

SimConfig Config::sim () const {
	SimConfig config;
	config.das_ms = das;
	config.arr_ms = arr;
	config.dcd_ms = dcd;
	config.sdf = sdf;
	config.are_ms = are;
	config.forced_delay = forced_delay;
	config.kicks = kicks;
	config.finesse_rule = finesse_rule;
	config.spin_rule = spin_rule;
	return config;
}

std::string config_path () {
	if (const char* forced = std::getenv("FORCETRIS_GUI_CONFIG")) {
		return forced;
	}
	char* pref = SDL_GetPrefPath("forcetris", "forcetris");
	if (pref == nullptr) {
		return "forcetris-gui.cfg";
	}
	std::string path = std::string(pref) + "gui.cfg";
	SDL_free(pref);
	return path;
}

Config load_config (const std::string& path) {
	Config config;
	apply_preset(config, config.preset);
	std::ifstream source(path);
	if (!source) {
		return config;
	}
	// A file exists, so its stat lines are the whole layout: the preset's is
	// only the starting point for a config that has never been saved.
	bool saw_stat = false;
	std::string line;
	while (std::getline(source, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream in(line);
		std::string key;
		in >> key;
		if (key == "das") in >> config.das;
		else if (key == "arr") in >> config.arr;
		else if (key == "dcd") in >> config.dcd;
		else if (key == "sdf") in >> config.sdf;
		else if (key == "are") in >> config.are;
		else if (key == "forced_delay") in >> config.forced_delay;
		else if (key == "kicks") { int flag = 1; in >> flag; config.kicks = flag != 0; }
		else if (key == "finesse") in >> config.finesse_rule;
		else if (key == "spins") in >> config.spin_rule;
		else if (key == "preset") in >> config.preset;
		else if (key == "stat") {
			if (!saw_stat) {
				config.stats.clear();
				saw_stat = true;
			}
			std::string id;
			int shown = 0;
			StatSpot spot;
			in >> id >> shown >> spot.x >> spot.y;
			spot.shown = shown != 0;
			if (!id.empty() && !in.fail()) {
				config.stats[id] = spot;
			}
		} else {
			config.unknown.push_back(line);
		}
	}
	return config;
}

bool save_config (const Config& config, const std::string& path) {
	std::ofstream out(path);
	if (!out) {
		return false;
	}
	out << "# forcetris gui config 1\n";
	out << "das " << config.das << "\n";
	out << "arr " << config.arr << "\n";
	out << "dcd " << config.dcd << "\n";
	out << "sdf " << config.sdf << "\n";
	out << "are " << config.are << "\n";
	out << "forced_delay " << config.forced_delay << "\n";
	out << "kicks " << (config.kicks ? 1 : 0) << "\n";
	out << "finesse " << config.finesse_rule << "\n";
	out << "spins " << config.spin_rule << "\n";
	out << "preset " << config.preset << "\n";
	for (const auto& [id, spot] : config.stats) {
		out << "stat " << id << " " << (spot.shown ? 1 : 0)
		    << " " << spot.x << " " << spot.y << "\n";
	}
	for (const std::string& line : config.unknown) {
		out << line << "\n";
	}
	return out.good();
}

std::vector<std::string> preset_names () {
	return {"tetrastats", "battle", "minimal", "full"};
}

void apply_preset (Config& config, const std::string& name) {
	// Which stats each preset shows, top to bottom in one column; dragging
	// them somewhere else afterwards is what the free layout is for.
	std::vector<const char*> shown;
	if (name == "battle") {
		shown = {"attack", "apm", "aps", "vs", "b2b", "combo"};
	} else if (name == "minimal") {
		shown = {"time", "pps"};
	} else if (name == "full") {
		for (const StatDef& stat : all_stats()) {
			shown.push_back(stat.id);
		}
	} else {
		// The TetraStats-like default: the figures its session view leads with.
		shown = {"pps", "apm", "vs", "time", "pieces", "lines", "finesse"};
	}
	config.stats.clear();
	float y = 0.f;
	for (const char* id : shown) {
		config.stats[id] = StatSpot{true, 0.f, y};
		y += 64.f;
	}
	config.preset = name;
}

} // namespace gui
} // namespace forcetris
