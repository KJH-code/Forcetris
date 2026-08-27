// The Forge Road: the campaign of stages, and the prestige economy that
// carries between attempts.
//
// A stage is a declarative recipe - a gametype the GUI already knows how to
// start, a finish line, and a handful of rule overrides - because the sim
// reads its numbers live and already owns every win condition a stage
// needs: line_quota for "clear N lines" under any gametype, the cheese
// race's own win for digs, the versus verdict for bosses. Nothing here
// simulates anything; this module is the map, the arithmetic and the file,
// so campaigncheck can grade all three without a window.
//
// The economy is two coins. Embers are the run's own (temper::embers_of),
// spent on the draft screen and gone with the run. Slag is what survives:
// granted when a stage ends - generously for a win, as a remnant of unspent
// embers for a death - and spent at the Anvil on permanent upgrades that
// apply inside campaign stages only, so the ordinary modes' score tables
// stay pure.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "forcetris/sim.hpp"

namespace forcetris {
namespace campaign {

// One stage of the road. `id` is written into campaign.dat and frozen
// forever; everything else may be rebalanced. Sentinels: -1 leaves a knob
// at the mode's default, nullptr means none.
struct Stage {
	const char* id;
	const char* name;
	const char* blurb;       // The gimmick, in one line on the map.
	int mode;                // The GUI gametype: 0 quota, 3 dig, 4 survive, 5 boss.
	int quota;               // Lines (0/4) or rows of cheese (3); 0 for a boss.
	int par_seconds;         // The second star's clock; 0 for bosses.
	double fuse_scale = 1.;  // Multiplies fuse_base; under 1 burns faster.
	int fall_delay = -1;     // Gravity, in frames per row.
	int cheese_holes = -1;
	int cheese_messiness = -1;
	int cheese_period = -1;
	bool no_kicks = false;
	int cleartype = -1;
	int spin_rule = -1;
	bool pressure = false;   // The fuse burns hot all stage. Solo only.
	const char* board = nullptr;    // Preset rows for Board::from_rows,
	                                // newline-separated, top row first.
	const char* tempers = nullptr;  // Pre-applied temper ids, comma-separated.
	int rank = -1;           // Boss: index into bot::ranks().
	int first_to = 1;
	const char* blade = nullptr;    // Boss blade ids; nullptr = blade_for(rank).
	int slag_first = 20;     // Slag for the first clear...
	int slag_repeat = 5;     // ...and for every clear after it.
};

// The road, in order. Two chapters of eight; each chapter ends in a boss.
const std::vector<Stage>& stages ();
// How many stages open each chapter, for the screen's headers.
constexpr int kPerChapter = 8;

// The permanent upgrades the Anvil sells. Level `n` costs cost_base * n
// slag; effects apply to a stage's rules in apply_anvil, except the two
// the GUI itself consumes (ember gain, the free opening draft).
struct Upgrade {
	const char* id;
	const char* name;
	const char* text;
	int levels;
	int cost_base;
};
const std::vector<Upgrade>& anvil ();
const Upgrade* upgrade (const std::string& id);

// What has been earned and bought, in campaign.dat: the same tolerant
// key=value style as the career and the profile, unknown lines preserved.
struct State {
	std::map<std::string, int> stars;   // By stage id, 0..3.
	int slag = 0;
	std::map<std::string, int> forge;   // Upgrade id -> level bought.
	std::vector<std::string> unknown;
};

// FORCETRIS_CAMPAIGN if set, else <root>/data/campaign.dat.
std::string path (const std::string& root);
State load (const std::string& path);
bool save (const std::string& path, const State& state);

// The gate: stage 0 is always open, each later stage opens once the one
// before it holds at least one star.
bool open (const State& state, size_t stage);

// A stage's rules, built in the honest order: the player's base config,
// then the stage's own overrides, then its pre-applied tempers, then the
// Anvil - with the fuse forced on, because a stage is a fuse game whatever
// the Rules tab says. For a boss this builds the PLAYER's side; the bot's
// side must come from bot_config below, or the player's permanent
// upgrades leak onto the boss's board.
SimConfig stage_config (const Stage& stage, SimConfig base,
	const std::map<std::string, int>& forge);
// The boss's rules: the stage's overrides with no player tempers and no
// Anvil, ready for the blade.
SimConfig bot_config (const Stage& stage, SimConfig base);
// The boss's blade: the recipe's own, or the rank's standard issue.
std::vector<std::string> blade_of (const Stage& stage);

// The stage's preset board rows, split for Board::from_rows; empty when
// the stage starts on a clean floor.
std::vector<std::string> board_rows (const Stage& stage);

// --- The arithmetic, all of it here so the screen stays dumb. -----------
// Stars for a finished solo stage: one for the clear, one under par, one
// with no forced drops. Bosses count differently - the match verdict is
// the star: win, sweep, sweep with Overdrive ignited.
int solo_stars (bool won, double seconds, int par_seconds, int forced);
int boss_stars (bool won, bool sweep, bool ignited);
// Slag granted when a stage attempt ends. A win pays the stage's bounty
// (first clear or repeat) plus a star bonus; a death renders the unspent
// embers down instead - the prestige remnant.
int slag_award (const Stage& stage, bool first_clear, bool won, int stars,
	int embers_left);
// The Anvil's effects on the run the GUI reads directly: extra ember
// income in percent, and free drafts dealt at the stage's start.
int ember_bonus_percent (const std::map<std::string, int>& forge);
int free_drafts (const std::map<std::string, int>& forge);
// What level `level` of an upgrade costs (1-based).
int upgrade_cost (const Upgrade& upgrade, int level);

} // namespace campaign
} // namespace forcetris
