#include "config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <SDL.h>

#include "stats.hpp"

namespace forcetris {
namespace gui {

const std::vector<ActionDef>& all_actions () {
	static const std::vector<ActionDef> actions = {
		{"left", "Move left", Key::Left},
		{"right", "Move right", Key::Right},
		{"soft", "Soft drop", Key::Soft},
		{"hard", "Hard drop", Key::Hard},
		{"hold", "Hold", Key::Hold},
		{"ccw", "Rotate CCW", Key::Ccw},
		{"cw", "Rotate CW", Key::Cw},
		{"flip", "Rotate 180", Key::Flip},
	};
	return actions;
}

std::map<std::string, std::vector<int>> default_keys () {
	// The Python game's defaults, scancode for scancode.
	return {
		{"left", {SDL_SCANCODE_LEFT}},
		{"right", {SDL_SCANCODE_RIGHT}},
		{"soft", {SDL_SCANCODE_DOWN}},
		{"hard", {SDL_SCANCODE_SPACE}},
		{"hold", {SDL_SCANCODE_LSHIFT, SDL_SCANCODE_C}},
		{"ccw", {SDL_SCANCODE_Z}},
		{"cw", {SDL_SCANCODE_X, SDL_SCANCODE_UP}},
		{"flip", {SDL_SCANCODE_A}},
	};
}

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
	config.cleartype = cleartype;
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

std::string game_root () {
	namespace fs = std::filesystem;
	if (const char* forced = std::getenv("FORCETRIS_ROOT")) {
		return forced;
	}
	std::vector<fs::path> starts;
	if (char* base = SDL_GetBasePath()) {
		starts.emplace_back(base);
		SDL_free(base);
	}
	std::error_code ignored;
	starts.push_back(fs::current_path(ignored));
	for (const fs::path& start : starts) {
		fs::path probe = start;
		for (int depth = 0; depth < 4; ++depth) {
			if (fs::is_directory(probe / "sound", ignored)) {
				return probe.string();
			}
			probe = probe.parent_path();
		}
	}
	return ".";
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
		else if (key == "clears") in >> config.cleartype;
		else if (key == "sfx_volume") in >> config.sfx_volume;
		else if (key == "music_volume") in >> config.music_volume;
		else if (key == "preset") in >> config.preset;
		else if (key == "key") {
			// The whole line is that action's binding: an action listed with
			// no codes is deliberately unbound.
			std::string action;
			in >> action;
			bool known = false;
			for (const ActionDef& def : all_actions()) {
				if (action == def.id) {
					known = true;
					break;
				}
			}
			if (known) {
				std::vector<int> codes;
				int code = 0;
				while (in >> code) {
					if (code > 0 && code < SDL_NUM_SCANCODES) {
						codes.push_back(code);
					}
				}
				config.keys[action] = codes;
			} else {
				config.unknown.push_back(line);
			}
		}
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
	out << "clears " << config.cleartype << "\n";
	out << "sfx_volume " << config.sfx_volume << "\n";
	out << "music_volume " << config.music_volume << "\n";
	out << "preset " << config.preset << "\n";
	for (const ActionDef& action : all_actions()) {
		out << "key " << action.id;
		const auto found = config.keys.find(action.id);
		if (found != config.keys.end()) {
			for (const int code : found->second) {
				out << " " << code;
			}
		}
		out << "\n";
	}
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
		// A panel is a label over a headline figure; the step leaves a
		// breath of dark between one and the next.
		y += 78.f;
	}
	config.preset = name;
}

} // namespace gui
} // namespace forcetris
