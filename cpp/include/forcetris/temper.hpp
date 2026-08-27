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
// Fewer than three only if the pool itself runs dry.
std::vector<std::string> offer (unsigned seed, int heat,
	const std::vector<std::string>& taken);

// How many heats this run has forged so far - the one owner of the count,
// shared by the offer gate, the HUD, the stat panel and the bot's side of
// a duel. A dig race (Meltdown) is measured by the garbage rows it has dug
// out; every other game by the lines it has cleared.
int heats_done (int lines, int downstack, bool by_digging);

// The bot's pick from an offer, by rank temperament: low ranks lean on
// Fuel, high ranks on Flow and Risk, and collapse is never taken (its
// planner assumes naive clears). Returns the index into `offers`, or -1
// when nothing on the table is acceptable - the caller then passes the
// heat by without a card.
int bot_pick (const std::vector<std::string>& offers, int rank_index,
	std::mt19937& rng);

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
