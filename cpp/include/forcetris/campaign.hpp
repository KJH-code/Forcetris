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

// What a recipe is for, which is how the map generator seats it. Rooms are
// the battle window; the other two are the chapter's watch, and a chapter
// may hold several of each - one miniboss and one boss per concept pair.
constexpr int kRoom = 0;
constexpr int kMiniboss = 1;
constexpr int kBoss = 2;

// One stage of the road. `id` is written into campaign.dat and frozen
// forever; everything else may be rebalanced. Sentinels: -1 leaves a knob
// at the mode's default, nullptr means none.
struct Stage {
	const char* id;
	const char* name;
	const char* blurb;       // The gimmick, in one line on the map.
	int mode;                // The GUI gametype: 0 quota, 3 dig, 4 survive, 5 boss.
	int quota;               // Lines (0/4) or rows of cheese (3); 0 for a boss.
	// A finish line in points instead of rows (mode 0 only): the stage is
	// won at this score, so spins and quads are the fast way through.
	// Set alongside quota = 0.
	long long score_quota = 0;
	// A finish line on the clock (mode 4 only): survive this many seconds
	// of the rising floor and the stage is won. When set, `quota` is not
	// a finish line but the star bar - lines cleared during the watch for
	// the second star, twice that for the third.
	int survive_seconds = 0;
	// A raid (mode 5): a gauntlet of lesser foes fought back to back,
	// comma-separated rank indices. The player must down them all; one
	// loss ends the raid. first_to is the gauntlet's length.
	const char* raid = nullptr;
	int par_seconds;         // The second star's clock; 0 for bosses.
	// V2.1: the fuse - and its forced drop - is a stage gimmick now, not
	// the campaign's default. A recipe that sets this burns like the old
	// days; everything else plays the board pure, and the HUD's fuse
	// chrome stays away. Duels (mode 5) always burn: the fuse is the
	// duel's own tension. Named for the event the player sees.
	bool fuse = false;
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
	// What this recipe is on the map, said out loud rather than derived
	// from where it sits in the table. A room is everything the battle
	// window draws from - the skirmishes and the raids included, because
	// those are fought on ordinary nodes - while a miniboss and a boss are
	// seated by the generator itself. Frozen the way ids are: the save
	// file never stores it, but the map's shape depends on it.
	int role = kRoom;
	// Which watch of the chapter this belongs to. A chapter fields several
	// concept pairs - one miniboss and one boss each - and a run rolls one
	// pair for its whole climb, so the top of the map is a different face
	// every time. Meaningless on a room.
	int pair = 0;
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

// --- Reading the road by role rather than by position. ------------------
// Where a chapter's recipes start in the flat table. Every screen and the
// generator alike used to open-code this loop; now they ask.
int chapter_base (int chapter);
// The chapter's rooms, flat indexes in table order: everything the battle
// window may draw. Skirmishes and raids are rooms - they are fought on
// ordinary nodes - so only the watch (miniboss, boss) is missing.
std::vector<int> chapter_rooms (int chapter);
// The concept pairs the chapter fields, by pair number, in table order. A
// run rolls one of these and climbs to it; the rest of the chapter's
// watch is not on that map at all.
std::vector<int> chapter_pairs (int chapter);
// One pair's two duels, flat indexes; -1 when the chapter has no such
// pair (which campaigncheck forbids, but the readers stay honest).
int pair_miniboss (int chapter, int pair);
int pair_boss (int chapter, int pair);
// Every boss the chapter fields, whichever pair it belongs to - what the
// chapter gate reads, since beating any of them is beating the chapter.
std::vector<int> chapter_bosses (int chapter);

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

// The highest rung the gentlest fire will ever field, whatever the recipe
// asks for: B, the top of the band a learning player actually lives in.
// Without it, two rungs off chapter three's SS bosses still lands on A.
constexpr int kMildCeiling = 4;
constexpr int kForgedLives = 3;
struct Run {
	bool active = false;
	int chapter = 0;
	unsigned seed = 0;
	int difficulty = kMild;
	// The Endless Climb: rings of the same six-row map stacked without
	// end, each ring harder than the last. An endless run ignores
	// `chapter` for its map (the battle pool spans every chapter) and is
	// always played at white heat - one death ends it, and the record is
	// the rows climbed.
	bool endless = false;
	int ring = 0;
	int depth = 0;                     // The next row to fight.
	std::vector<int> path;             // The node picked at each row done.
	std::vector<std::string> tempers;  // The build, in pick order.
	// Oils bought on the map, spent on the next battle entered: "hot"
	// (the hand strikes harder) and "frost" (a duel foe's clears freeze).
	// Consumed - and saved consumed - the moment the battle launches.
	std::vector<std::string> oils;
	int embers = 0;
	int lives = kForgedLives;          // Meaningful under kForged only.
	// What the run cost, for the grade it earns at the end. Seconds are
	// battle seconds summed at each settlement, never wall clock: a run
	// left open overnight is not a worse run, and a player reading a menu
	// is not spending the climb. Deaths counts every battle lost and every
	// mid-fight surrender, the free mild retry included - the grade is
	// about how the climb went, not about what the economy charged.
	int seconds = 0;
	int deaths = 0;
};
// What the stage asks, in a plain sentence, read off the recipe itself.
//
// The blurbs used to carry the win condition inside the flavour - "Old iron
// on the floor. Fifteen lines through it." - which asks a new player to work
// out which half of the sentence is the rule. Worse, a number written by
// hand in prose can drift from the number the stage actually enforces. This
// is computed from the recipe, so it cannot.
std::string goal_line (const Stage& stage);

// What a finished climb was worth, made of the run's own facts rather than
// one board's. The TETR.IO estimate the loss screen used to print after
// every stage grades a single game against public averages - a real number
// in the Training Yard, and the wrong one twelve times over inside a climb
// that has not ended yet.
struct Verdict {
	char grade[3] = "D";
	int score = 0;      // 0..100, what the letter came from.
	int rows = 0;       // Rows of the road taken (rings, when endless).
	int deaths = 0;
	int seconds = 0;    // Battle seconds, not wall clock.
	bool finished = false;
};

// Grade a run. `won` says the last row actually fell rather than the climb
// ending under it. Safe on a blank run: it grades at the floor.
Verdict grade_run (const Run& run, bool won);

// Slag awards scale with the weight of death: 100 / 150 / 200 percent.
int slag_percent (int difficulty);
// And so does the foe. The fire picked at the door was only ever a wager
// on death; it should also be what the road's duels are worth fighting,
// so every duel rank moves with it - a rung down on mild, the recipe's own
// on forged, a rung up at white heat - inside the ladder's own range. The
// miniboss, the boss, the skirmishes and every foe of a raid alike.
int rank_for (int rank, int difficulty);
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
	// The record board: the most rows any Endless Climb has managed.
	int endless_best = 0;
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

// --- What the smith charges. --------------------------------------------
// Every ember price in a run - a reroll, a second pick, melting a card
// down, a duplicate, a life, a coat of oil - climbs with the run. The
// rungs already climbed are the measure: a chapter's rows plus the rows of
// the map in progress, or the climb's own row count when it is endless.
// Prices rise by a fixed share per row and stop at a ceiling, so late is
// dear but never impossible. The skip's consolation is paid on the same
// curve, because it is the shape of one economy and not two.
int priced (int base, const Run& run);

// --- The Endless Climb. -------------------------------------------------
// Opens once the Deep Forge's master has fallen at least once.
bool endless_open (const State& state);
// The record's unit: rows climbed - full rings, plus the rows of the one
// in progress.
int endless_rows (const Run& run);
// A ring of the climb: the same six-row map, built like build_map but
// with its battles drawn from every chapter's pool - the window sliding
// up with the ring - and the top row held by the gatekeeper rotation:
// rings 0..5 walk [c1m1, c1s8, c2m1, c2s8, c3m1, c3s9], and past them
// the Vault Warden and the Forge Heart trade watches without end.
std::vector<MapNode> build_endless_map (int ring, unsigned seed);
// The climb's tightening, applied over a stage's config: gravity up,
// quotas up, the flood faster - and never the other way.
SimConfig endless_scaled (SimConfig config, int ring);
// A duel foe's rank on the climb: half a rank per ring, capped at the
// ladder's top rung.
int endless_rank (int rank, int ring);

// The rank the climb owes the foe before the ladder's ceiling is applied.
int endless_rank_owed (int rank, int ring);

// The foe's extra steel: once endless_rank has nothing left to promote,
// every further half-rung owed is paid as attack instead. Inert until the
// ladder is exhausted.
SimConfig endless_edge (SimConfig foe, int rank, int ring);

// How hard a boss's skills land, as a multiplier on the rows they throw,
// the gauge they drain and the seconds they hold a gimmick down.
//
// A recipe writes one number for the blow; the fire chosen at the door
// decides what that number is worth. The gentlest fire takes most of the
// sting out (a rustfall of three rows becomes two - a beginner who cannot
// yet clear a quad should not be handed one), the forged fire is the
// recipe as written, and white heat lands nearly half again as hard. A
// climb keeps going past that, because everything about a climb does.
double skill_scale (int difficulty, bool endless, int ring);

// A blow's rows and seconds, after that scale. A skill that fires always
// does something: the floors are one row and one second, so the gentlest
// fire softens a blow and never deletes it.
int skill_rows (int rows, double scale);
long skill_frames (long frames, double scale);

// The player's own squeeze: the flood lands heavier every ring. Unbounded,
// and the one dial that never hits a floor.
SimConfig endless_press (SimConfig mine, int ring);

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
// The third star reads the room: a burn room asks for zero forced drops,
// a pure room asks for three quarters of the par.
int solo_stars (bool won, double seconds, int par_seconds, int forced,
	bool fused);
// A watch's stars: the clock is fixed, so the marks are lines cleared
// while holding on - the bar, then twice the bar.
int survive_stars (bool won, int lines, int bar);
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
