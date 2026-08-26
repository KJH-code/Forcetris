#include "forcetris/temper.hpp"

#include <algorithm>
#include <random>

namespace forcetris {
namespace temper {

namespace {

// How often a family's cards come up in the roll. The rule pair is rare
// because each one changes what the run *is* rather than how much of it
// there is, and one of those a run is a swing; three would be noise.
int weight_of (Family family) {
	switch (family) {
		case Family::Rule: return 1;
		case Family::Risk: return 3;
		default: return 4;
	}
}

} // namespace

const std::vector<Temper>& pool () {
	static const std::vector<Temper> all = {
		// --- Fuel: the run lasts longer. ---------------------------------
		{"quench", "Quench",
			"a cleared line banks 0.15s more fuse", Family::Fuel, 3},
		{"cistern", "Cistern",
			"the bank holds 2.0s more", Family::Fuel, 3},
		{"ladle", "Ladle",
			"a new piece may draw 0.4s more from the bank", Family::Fuel, 3},
		{"slow_burn", "Slow Burn",
			"each heat shortens the fuse 0.04s less", Family::Fuel, 3},
		{"thick_wick", "Thick Wick",
			"every piece burns 0.35s longer", Family::Fuel, 3},
		// --- Flow: Overdrive sooner, longer, worth more. ------------------
		{"bellows", "Bellows",
			"Overdrive burns 2.0s longer", Family::Flow, 3},
		{"white_heat", "White Heat",
			"Overdrive multiplies 0.25 more", Family::Flow, 3},
		{"spark", "Spark",
			"a cleared line is worth 1 more Flow", Family::Flow, 3},
		{"clean_strike", "Clean Strike",
			"a lock inside the Flash is worth 3 more Flow", Family::Flow, 3},
		{"wide_window", "Wide Window",
			"the Flash window opens 8% wider", Family::Flow, 2},
		// --- Risk: a gain with a price on it. ----------------------------
		{"overheat", "Overheat",
			"Flow gains double; every piece burns 0.4s less", Family::Risk, 2},
		{"gamble", "Gamble",
			"Overdrive multiplies 0.75 more; a burnt piece costs 12 more Flow",
			Family::Risk, 2},
		{"thin_walls", "Thin Walls",
			"attack banks 0.4s more; the bank holds 1.5s less",
			Family::Risk, 2},
		// --- Rule: the run becomes a different game. ----------------------
		{"collapse", "Collapse",
			"cleared rows collapse: the stack falls in pieces", Family::Rule, 1},
		{"every_twist", "Every Twist",
			"every spin scores, minis included", Family::Rule, 1},
	};
	return all;
}

const Temper* find (const std::string& id) {
	for (const Temper& entry : pool()) {
		if (id == entry.id) {
			return &entry;
		}
	}
	return nullptr;
}

void apply (SimConfig& rules, const std::string& id) {
	if (id == "quench") {
		rules.fuse_refuel_line += 0.15;
	} else if (id == "cistern") {
		rules.fuse_bank_cap += 2.0;
	} else if (id == "ladle") {
		rules.fuse_draw_cap += 0.4;
	} else if (id == "slow_burn") {
		// A floor rather than zero: a schedule that never tightens would
		// make the twelfth heat the same as the first.
		rules.fuse_decay = std::max(0.03, rules.fuse_decay - 0.04);
	} else if (id == "thick_wick") {
		rules.fuse_base += 0.35;
	} else if (id == "bellows") {
		rules.overdrive_secs += 2.0;
	} else if (id == "white_heat") {
		rules.overdrive_mult += 0.25;
	} else if (id == "spark") {
		rules.flow_gain_line += 1.0;
	} else if (id == "clean_strike") {
		rules.flow_flash_gain += 3.0;
	} else if (id == "wide_window") {
		rules.flash_frac += 0.08;
	} else if (id == "overheat") {
		rules.flow_gain_line *= 2.0;
		rules.flow_gain_attack *= 2.0;
		rules.flow_flash_gain *= 2.0;
		// Never below the floor the schedule itself may not cross.
		rules.fuse_base = std::max(rules.fuse_min, rules.fuse_base - 0.4);
	} else if (id == "gamble") {
		rules.overdrive_mult += 0.75;
		rules.flow_burn_loss += 12.0;
	} else if (id == "thin_walls") {
		rules.fuse_refuel_attack += 0.4;
		// The bank may not shrink past the draw it has to serve.
		rules.fuse_bank_cap
			= std::max(rules.fuse_draw_cap, rules.fuse_bank_cap - 1.5);
	} else if (id == "collapse") {
		rules.cleartype = 1;
	} else if (id == "every_twist") {
		rules.spin_rule = 3;
	}
}

SimConfig tempered (const SimConfig& start,
		const std::vector<std::string>& taken) {
	SimConfig rules = start;
	for (const std::string& id : taken) {
		apply(rules, id);
	}
	return rules;
}

std::vector<std::string> offer (unsigned seed, int heat,
		const std::vector<std::string>& taken) {
	// The roll is the run and the heat, and nothing else: the same run
	// offers the same three cards at the same heat however it got there,
	// so a replay can say what the choice actually was.
	std::mt19937 rng(seed ^ (0x9e3779b9u * static_cast<unsigned>(heat + 1)));
	std::vector<const Temper*> left;
	std::vector<int> weights;
	for (const Temper& entry : pool()) {
		const int held = static_cast<int>(
			std::count(taken.begin(), taken.end(), std::string(entry.id)));
		if (held < entry.stacks) {
			left.push_back(&entry);
			weights.push_back(weight_of(entry.family));
		}
	}
	std::vector<std::string> cards;
	while (cards.size() < 3 && !left.empty()) {
		int total = 0;
		for (const int weight : weights) {
			total += weight;
		}
		std::uniform_int_distribution<int> pick(0, total - 1);
		int roll = pick(rng);
		size_t at = 0;
		while (at + 1 < left.size() && roll >= weights[at]) {
			roll -= weights[at];
			++at;
		}
		cards.emplace_back(left[at]->id);
		left.erase(left.begin() + static_cast<long>(at));
		weights.erase(weights.begin() + static_cast<long>(at));
	}
	return cards;
}

} // namespace temper
} // namespace forcetris
