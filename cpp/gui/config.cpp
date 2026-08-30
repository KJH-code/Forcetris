#include "config.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <SDL.h>

#include "forcetris/bot.hpp"
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
	config.finesse_rule = finesse_rule;
	config.clear_delay = clear_delay;
	// The game's own rules, fixed - the settings screen sells feel, never
	// rulebook. The board plays pure: no forced drop, no fuse (the fuse is
	// the Forge's gimmick, forced on by the burn recipes and campaign
	// duels), kicks on, every spin honoured, plain clears (the cascades
	// are stage gimmicks too).
	config.forced_delay = 0.;
	config.fuse = false;
	config.kicks = true;
	config.spin_rule = 2;
	config.cleartype = 0;
	// The Flow gauge and Overdrive, on the other hand, belong to every
	// game: they are what quality buys. The fuse is the Forge's punishment
	// and stays a room's gimmick; this is the reward, and it runs in the
	// Training Yard, on the ladder and on the road alike. A campaign stage
	// and a duel bot both build from here, so both sides ignite.
	config.flow_rail = true;
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
	// A file written before the handling was retuned holds the trainer's
	// numbers, and a player who never opened the settings screen would keep
	// them forever. So a file starts at revision zero and is brought forward
	// below unless it says otherwise.
	config.handling_rev = 0;
	config.ladder_rev = 0;
	config.stats_rev = 0;
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
		// The rule knobs an older build wrote - forced_delay, kicks, spins,
		// clears, fuse - are the game's own now; their lines fall through to
		// the unknown pile and ride along unread.
		else if (key == "finesse") in >> config.finesse_rule;
		else if (key == "cleardelay") { int flag = 1; in >> flag; config.clear_delay = flag != 0; }
		else if (key == "handlingrev") in >> config.handling_rev;
		else if (key == "ladderrev") in >> config.ladder_rev;
		else if (key == "statsrev") in >> config.stats_rev;
		else if (key == "shake") { int flag = 1; in >> flag; config.shake = flag != 0; }
		else if (key == "lowlatency") { int flag = 1; in >> flag; config.lowlatency = flag != 0; }
		else if (key == "smooth") { int flag = 1; in >> flag; config.smooth = flag != 0; }
		else if (key == "cheesetotal") in >> config.cheese_total;
		else if (key == "cheeseperiod") in >> config.cheese_period;
		else if (key == "cheeseholes") in >> config.cheese_holes;
		else if (key == "cheesemess") in >> config.cheese_messiness;
		else if (key == "botrank") in >> config.bot_rank;
		else if (key == "firstto") in >> config.first_to;
		else if (key == "sfx_volume") in >> config.sfx_volume;
		else if (key == "music_volume") in >> config.music_volume;
		else if (key == "music_mode") in >> config.music_mode;
		else if (key == "ambience") { int flag = 1; in >> flag; config.ambience = flag != 0; }
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
	// The bring-forward, but only for a file still on the numbers an older
	// build shipped: a player who chose their own handling - Trainer, or
	// hand-typed values - keeps it, and only picks up the stamp. A file on
	// exactly the old shipped set (rev 1's Instant) moves to the current
	// default; the deliberate-Instant player is indistinguishable from the
	// never-touched one and gets moved once, with the Instant button one
	// click away.
	if (config.handling_rev < kHandlingRev) {
		const bool old_shipped = config.das == 100 && config.arr == 0
			&& config.dcd == 0 && config.sdf == 40 && config.are == 0
			&& !config.clear_delay;
		if (old_shipped || config.handling_rev < 1) {
			apply_handling(config, Handling::Standard);
		}
		config.handling_rev = kHandlingRev;
	}
	// The ladder grew two rungs at the bottom, so every index above them
	// moved up by two. A file from before that remembers its duel opponent
	// by a number, and that number now names a foe two rungs gentler than
	// the one the player picked - so carry the pick, not the index.
	if (config.ladder_rev < kLadderRev) {
		config.bot_rank += 2;
		config.ladder_rev = kLadderRev;
	}
	// The board ships bare now. A file from before that carries the old
	// seven-panel default, which nobody chose - it was what the first
	// launch wrote down - so it is cleared once, exactly like the handling
	// bring-forward above, and only for a layout still on that set. A
	// player who picked their own panels keeps them and takes the stamp.
	if (config.stats_rev < kStatsRev) {
		static const char* kOldDefault[] = {"pps", "apm", "vs", "time",
			"pieces", "lines", "finesse"};
		size_t lit = 0;
		for (const auto& [id, spot] : config.stats) {
			lit += spot.shown ? 1 : 0;
		}
		bool as_shipped = lit == sizeof kOldDefault / sizeof kOldDefault[0];
		for (const char* id : kOldDefault) {
			const auto found = config.stats.find(id);
			as_shipped = as_shipped && found != config.stats.end()
				&& found->second.shown;
		}
		if (as_shipped) {
			apply_preset(config, "none");
		}
		config.stats_rev = kStatsRev;
	}
	// A hand-edited or damaged file must not smuggle values the sliders
	// cannot reach - the sim divides gravity by sdf, and the Python side
	// clamps its own file the same way on load.
	config.das = std::clamp(config.das, 0, 330);
	config.arr = std::clamp(config.arr, 0, 83);
	config.dcd = std::clamp(config.dcd, 0, 330);
	config.sdf = std::clamp(config.sdf, 5, 40);
	config.are = std::clamp(config.are, 0, 500);
	// The picker's dials the same way: only values its own buttons offer.
	config.cheese_total = std::clamp(config.cheese_total, 1, 400);
	config.cheese_period = std::clamp(config.cheese_period, 30, 3000);
	config.cheese_holes = std::clamp(config.cheese_holes, 1, 3);
	config.cheese_messiness = std::clamp(config.cheese_messiness, 0, 100);
	config.bot_rank = std::clamp<int>(config.bot_rank, 0,
		static_cast<int>(bot::ranks().size()) - 1);
	config.first_to = std::clamp(config.first_to, 1, 3);
	config.music_mode = std::clamp(config.music_mode, 0, 2);
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
	out << "finesse " << config.finesse_rule << "\n";
	out << "cleardelay " << (config.clear_delay ? 1 : 0) << "\n";
	out << "handlingrev " << config.handling_rev << "\n";
	out << "ladderrev " << config.ladder_rev << "\n";
	out << "statsrev " << config.stats_rev << "\n";
	out << "shake " << (config.shake ? 1 : 0) << "\n";
	out << "lowlatency " << (config.lowlatency ? 1 : 0) << "\n";
	out << "smooth " << (config.smooth ? 1 : 0) << "\n";
	out << "cheesetotal " << config.cheese_total << "\n";
	out << "cheeseperiod " << config.cheese_period << "\n";
	out << "cheeseholes " << config.cheese_holes << "\n";
	out << "cheesemess " << config.cheese_messiness << "\n";
	out << "botrank " << config.bot_rank << "\n";
	out << "firstto " << config.first_to << "\n";
	out << "sfx_volume " << config.sfx_volume << "\n";
	out << "music_volume " << config.music_volume << "\n";
	out << "music_mode " << config.music_mode << "\n";
	out << "ambience " << (config.ambience ? 1 : 0) << "\n";
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
	return {"none", "tetrastats", "battle", "minimal", "full"};
}

void apply_preset (Config& config, const std::string& name) {
	// Which stats each preset shows, top to bottom in one column; dragging
	// them somewhere else afterwards is what the free layout is for.
	std::vector<const char*> shown;
	if (name == "none") {
		// Nothing beside the board, which is where a first game should
		// start. Seven live figures next to a well is a dashboard, and a
		// dashboard is for someone who already knows which number they are
		// trying to move; anyone else reads it as noise between them and
		// the piece. Every panel is one tick away in Settings - Layout.
	} else if (name == "battle") {
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
	// Every stat is registered, off, so the Layout tab lists them all and a
	// panel switched on later already knows where to stand.
	{
		float rest = 0.f;
		for (const StatDef& stat : all_stats()) {
			config.stats[stat.id] = StatSpot{false, 0.f, rest};
			rest += 78.f;
		}
	}
	float y = 0.f;
	for (const char* id : shown) {
		config.stats[id] = StatSpot{true, 0.f, y};
		// A panel is a label over a headline figure; the step leaves a
		// breath of dark between one and the next.
		y += 78.f;
	}
	config.preset = name;
}

void apply_handling (Config& config, Handling set) {
	// A delayed spawn is the one thing none of the three asks for.
	config.are = 0;
	switch (set) {
	case Handling::Trainer:
		// The Python trainer's numbers, kept whole so nothing is taken away.
		config.das = 140;
		config.arr = 40;
		config.dcd = 0;
		config.sdf = 6;
		config.clear_delay = true;
		break;
	case Handling::Instant:
		config.das = 100;
		config.arr = 0;
		config.dcd = 0;
		config.sdf = 40;
		config.clear_delay = false;
		break;
	case Handling::Standard:
	default:
		// TETR.IO's own defaults, digit for digit - where its players
		// start, and so where this game's start too. The clear delay stays
		// off in every set: the frozen board was responsiveness, not
		// difficulty, and no modern game has it.
		config.das = 167;
		config.arr = 33;
		config.dcd = 17;
		config.sdf = 6;
		config.clear_delay = false;
		break;
	}
}

const char* handling_name (Handling set) {
	switch (set) {
	case Handling::Trainer: return "Trainer";
	case Handling::Instant: return "Instant";
	case Handling::Standard:
	default: return "Standard";
	}
}

const char* handling_note (Handling set) {
	switch (set) {
	case Handling::Trainer:
		return "the trainer's old numbers, clears animate";
	case Handling::Instant:
		return "straight to the wall, straight to the floor - a stacker's";
	case Handling::Standard:
	default:
		return "TETR.IO's defaults: start here, tune down as you improve";
	}
}

} // namespace gui
} // namespace forcetris
