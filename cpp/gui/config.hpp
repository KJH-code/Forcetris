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

// Bumped whenever the shipped handling changes. A config file written before
// the bump is still carrying the old numbers, so it is brought forward once
// and stamped - see load_config.
constexpr int kHandlingRev = 1;

// The three handling sets the settings screen offers. Named rather than typed
// in twice, so the trainer's numbers are never lost and the fast ones are one
// click away.
enum class Handling {
	Instant = 0,  // ARR 0, instant soft drop: what a stacker actually plays.
	Fast = 1,     // A column per frame instead of a teleport.
	Trainer = 2,  // The Python trainer's numbers, kept whole.
};

struct Config {
	// The handling, in the same units the Python settings menu shows. These
	// are a stacker's numbers, not the trainer's: on the engine's 20ms grid
	// the trainer's cost 500ms to cross the board and two full seconds to
	// soft drop from spawn to the floor, which is most of what made the game
	// feel slow next to a modern one. apply_handling holds all three sets.
	int das = 100;
	int arr = 0;
	int dcd = 0;
	int sdf = 40;
	int are = 0;
	double forced_delay = 1.0;
	bool kicks = true;
	int finesse_rule = 1;   // 0 off, 1 count, 2 retry.
	int spin_rule = 2;      // spins::Rule.
	int cleartype = 0;      // 0 naive, 1 sticky cascade, 2 linked cascade.
	// Animated clears, or resolved on the lock frame. Animated costs six
	// frames plus a resume per clearing pass, and a naive quad is four passes:
	// 580ms with no piece to control. The burn the board draws over a cleared
	// row is the GUI's own and outlives the sim either way, so off loses the
	// wait rather than the moment.
	bool clear_delay = false;
	// Which shipped handling this file has seen, so a retune reaches a config
	// that already exists instead of only a fresh one.
	int handling_rev = kHandlingRev;
	// The fuse ruleset - the variant's identity, on by default. Off plays
	// the plain trainer rules with the flat forced-drop delay above.
	bool fuse = true;
	// The board's shudder on quads, spins and Overdrive. Purely cosmetic.
	bool shake = true;
	// Uncapped rendering: vsync off, the loop paced by a millisecond nap.
	// Cuts a frame or two of input latency on a desk; phones keep vsync.
	bool lowlatency = true;

	// The mode picker's dials, remembered between runs: how the cheese is
	// cut and how the bot fight is set up.
	int cheese_total = 18;       // The race's quota.
	int cheese_period = 250;     // Survival's frames per rising row.
	int cheese_holes = 1;        // Holes per cheese row.
	int cheese_messiness = 100;  // Percent chance a row re-rolls its holes.
	int bot_rank = 4;            // Index into bot::ranks(); 4 is S.
	int first_to = 1;            // Rounds a versus match is played to.

	// Volumes, as fractions, matching the Python game's defaults.
	float sfx_volume = 1.f;
	float music_volume = 1.f;
	// What plays behind the game: 0 the generated Forge score, 1 the classic
	// chiptune track, 2 nothing. Matches Audio::Music.
	int music_mode = 0;
	// The furnace bed under everything, scaled by the music volume.
	bool ambience = true;

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

// One of the three handling sets, written over the config's handling fields -
// clear delay included, because the freeze is felt as handling whatever the
// settings screen files it under.
void apply_handling (Config& config, Handling set);
const char* handling_name (Handling set);
const char* handling_note (Handling set);

} // namespace gui
} // namespace forcetris
