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
	// The V2.1 simmed gimmicks, wired through to SimConfig's own fields.
	int sealed = 0;          // Sealed Columns: bit x walls column x off.
	bool cold_iron = false;  // Cold Iron: clears freeze, then shatter.
	bool pressure = false;   // The fuse burns hot all stage. Solo only.
	// The seen-not-simmed gimmicks, read by the GUI the way preheat is:
	// the sim's rules never change, only what the player is shown.
	bool dim = false;        // A lantern around the piece; darkness beyond.
	bool fog = false;        // The NEXT queue smoked over past one piece.
	const char* board = nullptr;    // Preset rows for Board::from_rows,
	                                // newline-separated, top row first.
	const char* tempers = nullptr;  // Pre-applied temper ids, comma-separated.
	int rank = -1;           // Boss: index into bot::ranks().
	int first_to = 1;
	const char* blade = nullptr;    // Boss blade ids; nullptr = blade_for(rank).
	int slag_first = 20;     // Slag for the first clear...
	int slag_repeat = 5;     // ...and for every clear after it.
};

// One chapter of the road: a named stretch of stages ending in a boss.
// `id` is reserved for the save file the way stage ids are, and frozen the
// same way. Chapters may differ in length - the screens walk this table
// rather than assume one.
struct Chapter {
	const char* id;
	const char* name;
	const char* blurb;
	int stages;
};

// The road, in order: the chapters, and every stage flat in chapter order.
// Both tables live in stages.cpp - the content file - and campaigncheck
// holds them in agreement.
const std::vector<Chapter>& chapters ();
const std::vector<Stage>& stages ();

// Where a flat stage index falls on the road: which chapter, and which
// stage within it, both 0-based. Meaningful only for indexes into
// stages(); anything past the road lands one chapter past the last.
struct Spot {
	int chapter;
	int stage;
};
Spot spot_of (size_t stage_index);

// --- The map: a chapter played as one seeded climb. ---------------------
// A run is a branching graph six rows deep, entrance at row 0, the
// chapter's boss alone at the top. Every fork is the player's to pick and
// the whole graph is a pure function of (chapter, seed), so a map can be
// rebuilt from the save file's two numbers and graded without a window.
constexpr int kMapDepth = 6;
// What a node holds. `kind` is written into no file yet, but its values
// are frozen the way ids are: 0 battle, 1 boss, 2 forge (a draft, the
// paid extras and the melting pot, no fight), 3 event (one card of
// choice), 4 miniboss (a duel on the risky branch under the boss); rest
// arrives in a later version and takes the next number.
struct MapNode {
	int depth = 0;             // Row, 0 entrance .. kMapDepth-1 boss.
	int lane = 0;              // Position in the row, left to right.
	int kind = 0;              // 0 battle, 1 boss, 2 forge, 3 event, 4 miniboss.
	int stage = 0;             // Flat index into stages(); -1 off-battle.
	std::vector<int> next;     // Node indices in the row above.
};
std::vector<MapNode> build_map (int chapter, unsigned seed);

// The run in progress, part of the save. Tempers and embers persist
// across the whole climb - that is the roguelite - and what death costs
// depends on the difficulty picked at the door.
constexpr int kMild = 0;    // Death re-offers the same node.
constexpr int kForged = 1;  // Death spends a life; none left ends the run.
constexpr int kWhite = 2;   // Death ends the run outright.
constexpr int kForgedLives = 3;
struct Run {
	bool active = false;
	int chapter = 0;
	unsigned seed = 0;
	int difficulty = kMild;
	int depth = 0;                     // The next row to fight.
	std::vector<int> path;             // The node picked at each row done.
	std::vector<std::string> tempers;  // The build, in pick order.
	int embers = 0;
	int lives = kForgedLives;          // Meaningful under kForged only.
};
// Slag awards scale with the weight of death: 100 / 150 / 200 percent.
int slag_percent (int difficulty);
const char* difficulty_name (int difficulty);   // "mild" / "forged" / "white".
int difficulty_from (const std::string& name);  // kMild when unrecognized.


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
	Run run;                            // The climb in progress, if any.
	std::vector<std::string> unknown;
};

// FORCETRIS_CAMPAIGN if set, else <root>/data/campaign.dat.
std::string path (const std::string& root);
State load (const std::string& path);
bool save (const std::string& path, const State& state);

// The gate: stage 0 is always open, each later stage opens once the one
// before it holds at least one star.
bool open (const State& state, size_t stage);

// The chapter gate the map uses instead: the first chapter is always
// open, each later one opens once the chapter before it has its boss
// starred.
bool chapter_open (const State& state, int chapter);

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
