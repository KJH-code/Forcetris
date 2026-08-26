// The tempering: what a run of the draft mode is allowed to change about
// itself, and what it is offered.
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
// from a newer file, which is read rather than refused).
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

// The shape of a Tempering run: ten lines to a heat, twelve heats to a
// finished blade. The quota the sim is given is the product.
constexpr int kLinesPerHeat = 10;
constexpr int kHeats = 12;
constexpr int kQuota = kLinesPerHeat * kHeats;

} // namespace temper
} // namespace forcetris
