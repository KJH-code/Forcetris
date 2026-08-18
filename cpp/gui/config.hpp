// What the player has chosen, and where it is kept between runs.
//
// The GUI's settings live in a plain key-value text file rather than the
// Python game's JSON: the file is written and read by this code alone, and a
// format a person can read in a glance and fix with any editor beats a parser
// dependency. One line per setting; unknown keys are kept so an older build
// does not eat a newer one's settings.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "forcetris/sim.hpp"

namespace forcetris {
namespace gui {

// One stat panel on the play screen: whether it is shown, and where it sits.
// Positions are relative to the top-left of the stat area so a resized window
// keeps the arrangement.
struct StatSpot {
	bool shown = false;
	float x = 0.f;
	float y = 0.f;
};

// One rebindable action: the id the config file stores, the label the
// settings screen shows, and the key the sim understands.
struct ActionDef {
	const char* id;
	const char* label;
	Key key;
};

const std::vector<ActionDef>& all_actions ();

// The out-of-the-box bindings, as SDL scancodes - several keys may share an
// action, the way the Python game's controls do.
std::map<std::string, std::vector<int>> default_keys ();

struct Config {
	// The handling, in the same units the Python settings menu shows.
	int das = 140;
	int arr = 40;
	int dcd = 0;
	int sdf = 6;
	int are = 0;
	double forced_delay = 1.0;
	bool kicks = true;
	int finesse_rule = 1;   // 0 off, 1 count, 2 retry.
	int spin_rule = 2;      // spins::Rule.

	// Volumes, as fractions, matching the Python game's defaults.
	float sfx_volume = 1.f;
	float music_volume = 1.f;

	// The stat panels, keyed by the stat ids stats.cpp registers.
	std::map<std::string, StatSpot> stats;

	// The key bindings, keyed by action id. An action may hold any number of
	// scancodes, including none - escape stays reserved for the pause menu.
	std::map<std::string, std::vector<int>> keys = default_keys();

	// Which named preset the layout started from, for the settings screen.
	std::string preset = "tetrastats";

	// Lines this build did not understand, preserved verbatim.
	std::vector<std::string> unknown;

	SimConfig sim () const;
};

// Where the config file lives: FORCETRIS_GUI_CONFIG if set (which is how the
// tests keep out of the real one), otherwise SDL's per-user pref directory.
std::string config_path ();

// The repository checkout the executable belongs to: the directory holding
// sound/, music/ and data/. FORCETRIS_ROOT if set, otherwise found by
// walking up from the executable; falls back to the working directory.
std::string game_root ();

Config load_config (const std::string& path);
bool save_config (const Config& config, const std::string& path);

// The named layouts the settings screen offers. Applying one replaces the
// stat map wholesale; the player's own dragging then edits the copy.
std::vector<std::string> preset_names ();
void apply_preset (Config& config, const std::string& name);

} // namespace gui
} // namespace forcetris
