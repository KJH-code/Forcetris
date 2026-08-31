// The tempering: what a run is allowed to change about itself, and what it
// is offered. This is the game's whole gimmick, not one mode's: every game
// played under the fuse rules crosses a heat and drafts a temper there -
// the trainer rules never do - and the standalone Tempering mode is the
// twelve-heat run of exactly this.
//
// Every temper here is one or two numbers out of SimConfig - the fuse's
// schedule, the refuel bank, the Flash window, the Flow gains, Overdrive's
// length and multiplier - because the sim reads all of them live, every use,
// rather than deriving anything from them at construction. So a draft is not
// a new rule; it is the same rules with different arithmetic, applied to a
// game already in progress through Sim::retune.
//
// The pool and the roll live in the core rather than the GUI for the usual
// reason: a replay has to be able to say what a run was played under, and a
// test has to be able to check that a temper moves what it claims and
// nothing else.
#pragma once

#include <random>
#include <string>
#include <vector>

#include "forcetris/sim.hpp"

namespace forcetris {
namespace temper {

// What a temper is for, which is also how the draft colours it.
enum class Family {
	Fuel = 0,   // Longer to burn, more to bank: the run survives further.
	Flow = 1,   // Overdrive sooner, longer, worth more.
	Risk = 2,   // A gain paid for with a loss.
	Rule = 3,   // The rare one that changes what a clear or a spin is.
	// The rarest: a trade that also breaks something the hands had learned
	// to trust - the judge, the walls, the keys themselves. Every one of
	// them pays, or it would be a punishment rather than a card.
	Chaos = 4,
	// The guard: nothing here wins a fight faster, and everything here
	// makes one survivable - gentler gravity, a hand that never locks, a
	// floor that sweeps itself, a blow that lands thinner. Conditional by
	// nature, which is why it sits at Risk's weight rather than Flow's.
	Ward = 5,
	// How you play, sold in pieces. The four styles - the rubble, the
	// chain, the opening, the plain clear - each come as three small
	// cards rather than one that decides for you: two that only pay, and
	// one creed that pays more and charges every other style for it.
	// A run leans by picking, and commits only if it wants to.
	Style = 6,
};

struct Temper {
	const char* id;      // The key written into a replay. Frozen forever.
	const char* name;    // What the card says.
	const char* text;    // One line, in the units the player feels.
	Family family;
	int stacks;          // How many times one run may take it.
};

// Every temper there is, in a fixed order - the roll indexes into this.
const std::vector<Temper>& pool ();

// One temper by id, or nullptr for a key this build does not know (an id
// from a newer file, or one since retired - read rather than refused).
const Temper* find (const std::string& id);

// Apply a temper's arithmetic to `rules`. An unknown id does nothing.
void apply (SimConfig& rules, const std::string& id);

// Every temper in `taken`, in order, over the rules a run started from.
SimConfig tempered (const SimConfig& start, const std::vector<std::string>& taken);

// The three cards this heat offers: decided by the run's seed and the heat
// number alone, so the same run always offers the same choices, and never
// including a temper the run has already taken as many times as it may.
// Fewer than three only if the pool itself runs dry. `salt` is the reroll
// counter: zero - the default, and every pre-reroll caller - leaves the
// hand exactly as it always was, and each paid reroll deals the same heat
// again under salt+1, deterministic like everything else here.
// `chaos` opens the challenge tier. The chaos family bends what the hands
// had learned to trust - the keys, the judge, the walls - and nobody ever
// picked one. A card that takes has to give enough to be worth taking, and
// giving that much made each of them a wash. They are not cards any more:
// see curses() below.
std::vector<std::string> offer (unsigned seed, int heat,
	const std::vector<std::string>& taken, unsigned salt = 0);

// The five Chaos effects, which are laid on a climb rather than drafted.
// Same struct, same frozen ids, same apply() - only the door is different:
// the Endless Climb hands one out every second ring and does not ask.
const std::vector<Temper>& curses ();

// The curse this climb lays at the given step (0-based), or empty once all
// five are down. A pure function of (seed, step): the order is the run's
// own, and a resumed climb lays exactly what it laid before.
std::string curse_at (unsigned seed, int step);

// How many curses a climb of this many rings is carrying. One every second
// ring, and the table runs out at five - past that the climb tightens with
// its own arithmetic instead.
int curses_by (int ring);

// How many heats this run has forged so far - the one owner of the count,
// shared by the offer gate, the HUD, the stat panel and the bot's side of
// a duel. A dig race (Meltdown) is measured by the garbage rows it has dug
// out; every other game by the lines it has cleared.
int heats_done (int lines, int downstack, bool by_digging);

// A pick from an offer by rank temperament: low ranks lean on Fuel, high
// ranks on Flow and Risk, and collapse is never taken (the duel planner
// assumes naive clears). Returns the index into `offers`, or -1 when
// nothing on the table is acceptable. The duel bot no longer drafts
// mid-round - it arrives with a blade instead - so today this drives the
// test harness's stand-in player, and stays for the day blades want
// variety rolled at match time.
int bot_pick (const std::vector<std::string>& offers, int rank_index,
	std::mt19937& rng);

// The blade a bot of this rank carries into a duel: a fixed list of temper
// ids applied to its rules at round start, escalating from a D rank's
// single thick wick to an X rank's full pressing build. Never collapse -
// the planner searches naive clears - and indexes past the ladder clamp
// to its top rung.
std::vector<std::string> blade_for (int rank_index);

// The run economy. Embers are the in-run coin: earned by what a run has
// actually resolved - lines and the attack they carried - and spent on the
// draft screen. The balance is derived (earned minus spent) rather than
// accumulated, because lines_cleared and attack_sent are already monotone
// live totals on the sim.
// The rates are one and one, and the prices below are what they are,
// because between them a run must never be able to buy the whole shop.
// It used to: two and three a room banked several times the till's entire
// stock, and no purchase was ever a decision.
int embers_of (int lines, int attack);
constexpr int kRerollCost = 10;      // Deal this heat's three again.
constexpr int kExtraPickCost = 30;
// Melting a picked temper back down at a forge node on the map.
constexpr int kRemoveCost = 20;  // Take a second card from the offer.
// Burning a curse off instead of a card. Deliberately most of a good
// room's whole earnings: a climb's curses are the difficulty that keeps
// climbing after the ladder runs out, and one you could buy off with
// pocket change would be no difficulty at all. It is still buyable,
// because the choice between shedding the ring's work and building on is
// a better decision than either one alone.
constexpr int kCurseCost = 60;
// The rest of what the coin buys (V2.1e): a second copy of a held card
// struck at the forge, a life bought back on forged fire, the two oils
// painted on before a battle, and the small solace for walking past the
// spoils untaken.
constexpr int kDuplicateCost = 16;
constexpr int kLifeCost = 35;
constexpr int kHotOilCost = 14;
constexpr int kFrostOilCost = 16;
constexpr int kSkipSolace = 4;

// The shape of a heat, and of the one mode that is a complete run of them:
// ten lines to a heat (six dug rows in Meltdown), twelve heats to a
// finished blade. The quota the sim is given is the product; only the
// Tempering mode has a quota or a heat cap - everywhere else the forge
// keeps dealing until the pool runs dry.
constexpr int kLinesPerHeat = 10;
constexpr int kDigsPerHeat = 6;
constexpr int kHeats = 12;
constexpr int kQuota = kLinesPerHeat * kHeats;

} // namespace temper
} // namespace forcetris
