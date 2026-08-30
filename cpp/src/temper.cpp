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
		case Family::Chaos: return 1;
		case Family::Risk: return 3;
		// The ward stands with risk rather than with fuel: half its cards
		// only matter in the rooms that threaten what they guard, and a
		// guard drawn where there is nothing to guard is a wasted pick.
		case Family::Ward: return 3;
		default: return 4;
	}
}

} // namespace

const std::vector<Temper>& pool () {
	// Thirty-four cards, and every face reads without a manual: the number
	// lives in
	// the arithmetic here and in the README, never on the card. The effects
	// are sized so that a single copy is felt - half a second of wick, three
	// seconds of Overdrive - because a card whose effect needs a stopwatch
	// to notice teaches the player that cards do not matter.
	static const std::vector<Temper> all = {
		// --- Fuel: what feeds the fire. -----------------------------------
		// The supply, as against Flow's payout: extra faucets nobody else
		// opens, a bigger tank, a tank that does not drain, and a fire
		// that can be topped up while it burns. The family used to feed a
		// wick; the wick left the duels, and what it feeds now is the
		// gauge - which is live in every room on the road.
		{"thick_wick", "Thick Wick",
			"every clear feeds the fire while it burns", Family::Fuel, 3},
		{"quench", "Quench",
			"digging out rubble charges the gauge", Family::Fuel, 3},
		{"slow_burn", "Slow Burn",
			"the gauge keeps its heat when the fire dies", Family::Fuel, 2},
		{"deep_bank", "The Deep Bank",
			"the gauge holds more than it needs", Family::Fuel, 2},
		{"hard_quench", "Hard Quench",
			"what lands on you charges the gauge", Family::Fuel, 2},
		// --- Flow: Overdrive sooner, longer, worth more. ------------------
		{"bellows", "Bellows",
			"Overdrive lasts longer", Family::Flow, 3},
		{"white_heat", "White Heat",
			"Overdrive hits harder", Family::Flow, 2},
		{"spark", "Spark",
			"Flow charges faster", Family::Flow, 2},
		{"heavy_hand", "Heavy Hand",
			"your attacks hit harder", Family::Flow, 2},
		{"frostbrand", "Frostbrand",
			"duels: the foe's clears freeze", Family::Flow, 1},
		{"hobnails", "Hobnails",
			"duels open with rust on the foe's floor", Family::Flow, 1},
		{"draught", "The Draught",
			"Overdrive catches sooner", Family::Flow, 2},
		// --- Risk: a gain with a price on it. ----------------------------
		{"overheat", "Overheat",
			"double Flow - a shorter burn", Family::Risk, 1},
		{"gamble", "Gamble",
			"huge Overdrive - the flood spills your gauge", Family::Risk, 1},
		{"loaded_dice", "Loaded Dice",
			"every third strike lands double", Family::Risk, 2},
		{"cold_forge", "Cold Forge",
			"your iron freezes - your hand strikes far harder",
			Family::Risk, 1},
		{"glass_edge", "The Glass Edge",
			"a far harder hand - and a far faster forge",
			Family::Risk, 2},
		{"hair_trigger", "The Hair Trigger",
			"a narrower flash, worth much more", Family::Risk, 2},
		{"hollow_wick", "The Hollow Wick",
			"a long Overdrive - a slow gauge", Family::Risk, 2},
		// --- Rule: the run becomes a different game. ----------------------
		{"collapse", "Collapse",
			"clears cascade", Family::Rule, 1},
		{"every_twist", "Every Twist",
			"every spin scores", Family::Rule, 1},
		{"turning_rack", "The Turning Rack",
			"every clear stirs the hold", Family::Rule, 1},
		{"linked_chain", "The Linked Chain",
			"clears cascade, and the pieces stay whole", Family::Rule, 1},
		// --- Chaos: something the hands trusted, bought and sold. ---------
		// Each face names the gift and the price in one breath, because a
		// card that only takes is a card nobody picks - and one that only
		// gives is not chaos, it is a reward.
		{"wild_spins", "The Crooked Judge",
			"wedge it in and it twists - nothing else does",
			Family::Chaos, 1},
		{"ring_walls", "The Ring",
			"the walls open onto each other - the forge spins faster",
			Family::Chaos, 1},
		{"crossed_wires", "Crossed Wires",
			"your hands are not where you left them - but they hit harder",
			Family::Chaos, 1},
		{"loose_ratchet", "The Loose Ratchet",
			"every third turn goes too far - the Flow loves it",
			Family::Chaos, 1},
		{"sticky_tongs", "Sticky Tongs",
			"every fourth hold sticks - the Flow feeds on the fumble",
			Family::Chaos, 1},
		// --- Ward: nothing here wins faster; everything here survives. ----
		// The guard family. Half of it only matters in the rooms that
		// threaten what it guards, which is the point: a ward is a bet on
		// the road ahead, and the map shows the road.
		{"counterweight", "The Counterweight",
			"the forge lets go slower", Family::Ward, 2},
		{"free_hand", "The Free Hand",
			"the hold box never locks", Family::Ward, 1},
		{"floor_sweep", "The Floor Sweep",
			"now and then the rubble underfoot is swept away",
			Family::Ward, 2},
		{"cold_shoulder", "The Cold Shoulder",
			"duels: what lands on you lands thinner", Family::Ward, 2},
		{"coolant", "Coolant",
			"another forge's heat leans on you less", Family::Ward, 2},
		{"sifter", "The Sifter",
			"the rubble falls in a straighter line", Family::Ward, 2},
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
	if (id == "thick_wick") {
		rules.overdrive_refill += 0.5;
	} else if (id == "quench") {
		rules.flow_gain_dig += 2.0;
	} else if (id == "slow_burn") {
		// A ceiling rather than the whole: a gauge that kept everything
		// would leave a fire that relights itself.
		rules.flow_keep = std::min(0.75, rules.flow_keep + 0.25);
	} else if (id == "bellows") {
		rules.overdrive_secs += 3.0;
	} else if (id == "white_heat") {
		rules.overdrive_mult += 0.5;
	} else if (id == "spark") {
		rules.flow_gain_line += 2.0;
		rules.flow_gain_attack += 2.0;
	} else if (id == "overheat") {
		rules.flow_gain_line *= 2.0;
		rules.flow_gain_attack *= 2.0;
		rules.flow_flash_gain *= 2.0;
		// The price is the burn itself: twice the charge, and less of the
		// fire to spend it on. A floor, so the fire is always worth
		// lighting.
		rules.overdrive_secs = std::max(3.0, rules.overdrive_secs - 3.0);
	} else if (id == "gamble") {
		// The price used to be paid on a forced drop, which duels no
		// longer have. The flood pays it instead - and the flood is the
		// duel's own pressure, so the card trades where the fight is.
		rules.overdrive_mult += 1.0;
		rules.flow_flood_loss += 15.0;
	} else if (id == "collapse") {
		rules.cleartype = 1;
	} else if (id == "every_twist") {
		rules.spin_rule = 3;
	} else if (id == "heavy_hand") {
		rules.attack_scale += 0.25;
	} else if (id == "loaded_dice") {
		// The first copy lands every third strike double; the second
		// tightens it to every other.
		rules.crit_every = rules.crit_every == 0 ? 3
			: std::max(2, rules.crit_every - 1);
	} else if (id == "cold_forge") {
		// The price is your own iron: every clear freezes first and
		// shatters a lock later - and the hand behind it hits far harder.
		rules.cold_iron = true;
		rules.attack_scale += 0.75;
	} else if (id == "turning_rack") {
		rules.hold_churn = true;
	} else if (id == "wild_spins") {
		// The gift and the price are the same switch: every boxed-in lock
		// scores, and every honest corner-rule spin stops.
		rules.wild_spins = true;
	} else if (id == "ring_walls") {
		// The walls open, and the price is paid in gravity - the ring turns
		// faster the moment it opens. A floor under it so a stack of other
		// speed-ups can never leave a piece unplayable.
		rules.wrap_walls = true;
		rules.fall_delay = std::max(6, rules.fall_delay - 6);
	} else if (id == "crossed_wires") {
		// The curse itself lives in the GUI's hands (the keys are not the
		// sim's business); what the sim owns is what it buys.
		rules.attack_scale += 0.5;
	} else if (id == "loose_ratchet") {
		// Same shape: the extra turn is the screen's doing, the reward is
		// here.
		rules.flow_gain_line += 3.0;
		rules.flow_gain_attack += 3.0;
	} else if (id == "sticky_tongs") {
		// And again: the tongs stick in the screen's hands, the gauge pays
		// for the fumble here.
		rules.flow_gain_line += 2.0;
		rules.flow_gain_attack += 2.0;
	} else if (id == "deep_bank") {
		// A gauge that holds more than ignition needs: the overfill rides
		// out of one burn and into the next, where a build keeps it.
		rules.flow_cap += 30.;
	} else if (id == "hard_quench") {
		rules.flow_gain_taken += 3.0;
	} else if (id == "draught") {
		// A floor rather than nothing: an Overdrive that lit on a breath
		// would never be a moment worth watching for.
		rules.flow_ignite = std::max(60., rules.flow_ignite - 12.);
	} else if (id == "glass_edge") {
		// Both halves are real and both are large. The same floor the ring
		// walls keep, for the same reason.
		rules.attack_scale += 0.6;
		rules.fall_delay = std::max(6, rules.fall_delay - 6);
	} else if (id == "hair_trigger") {
		// The flash used to pay for being fast against a clock. With the
		// clock gone from every duel it pays for being clean instead: a
		// lock made within a press or two of the finesse ideal. The face
		// is unchanged and still true - the window narrows, and is worth
		// far more.
		rules.flash_finesse = rules.flash_finesse == 0 ? 2
			: std::max(1, rules.flash_finesse - 1);
		rules.flow_flash_gain += 6.0;
	} else if (id == "hollow_wick") {
		// The inverse of Overheat: all the fire, none of the hurry.
		rules.flow_gain_line = std::max(0.5, rules.flow_gain_line - 1.5);
		rules.flow_gain_attack = std::max(0.5, rules.flow_gain_attack - 1.5);
		rules.overdrive_secs += 4.0;
	} else if (id == "linked_chain") {
		rules.cleartype = 2;
	} else if (id == "counterweight") {
		rules.fall_delay += 6;
	} else if (id == "free_hand") {
		rules.free_hold = true;
	} else if (id == "floor_sweep") {
		// The first copy sweeps every eighth clear made over rubble; the
		// second tightens it, the way the dice do.
		rules.sweep_every = rules.sweep_every == 0 ? 8
			: std::max(4, rules.sweep_every - 3);
	} else if (id == "cold_shoulder") {
		// A floor, because a blow that landed is owed its row: the sim's
		// own arithmetic already refuses to round one away.
		rules.garbage_scale = std::max(0.5, rules.garbage_scale - 0.25);
	} else if (id == "coolant") {
		// Down to one, which is no pressure at all - the other forge's
		// Overdrive stops being an attack and goes back to being a buff.
		rules.fuse_pressure = std::max(1.0, rules.fuse_pressure - 0.25);
	} else if (id == "sifter") {
		// A floor rather than zero: messiness at nothing would deal one
		// perfectly clean well forever, which is not a tidier pile, it is
		// no pile at all.
		rules.cheese_messiness
			= std::max(20, rules.cheese_messiness - 60);
	}
	// frostbrand and hobnails touch the FOE's board, not this config: the
	// versus wiring reads them off the run's build at every round start.
	// crossed_wires and loose_ratchet are the same shape pointed at the
	// player's own hands - the arithmetic above is only what they pay.
	// An id this build does not know - a card from a newer build's replay,
	// or one that has since been retired - is read rather than refused, so
	// there is deliberately no terminal else.
}

SimConfig tempered (const SimConfig& start,
		const std::vector<std::string>& taken) {
	SimConfig rules = start;
	for (const std::string& id : taken) {
		apply(rules, id);
	}
	return rules;
}

int heats_done (int lines, int downstack, bool by_digging) {
	// The one owner of "how far through the forge is this run": everything
	// that counts heats - the offer gate, the HUD, the stat panel, the
	// bot's side of a duel - counts them here, so no two screens can
	// disagree about which heat a board is in. A dig race is measured by
	// the garbage it has dug out rather than the lines it happened to
	// clear doing it; everything else is measured in cleared lines.
	if (by_digging) {
		return std::max(0, downstack) / kDigsPerHeat;
	}
	return std::max(0, lines) / kLinesPerHeat;
}

std::vector<std::string> offer (unsigned seed, int heat,
		const std::vector<std::string>& taken, unsigned salt, bool chaos) {
	// The roll is the run, the heat and the reroll count, and nothing else:
	// the same run offers the same three cards at the same heat however it
	// got there, so a replay can say what the choice actually was. Salt
	// zero must perturb nothing - it is every caller that predates rerolls.
	std::mt19937 rng(seed ^ (0x9e3779b9u * static_cast<unsigned>(heat + 1))
		^ (0x85ebca6bu * salt));
	std::vector<const Temper*> left;
	std::vector<int> weights;
	for (const Temper& entry : pool()) {
		const int held = static_cast<int>(
			std::count(taken.begin(), taken.end(), std::string(entry.id)));
		if (held >= entry.stacks) {
			continue;
		}
		// The challenge tier, shut unless the run asked for it.
		if (entry.family == Family::Chaos && !chaos) {
			continue;
		}
		left.push_back(&entry);
		weights.push_back(weight_of(entry.family));
	}
	std::vector<std::string> cards;
	bool chaos_dealt = false;
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
		if (left[at]->family == Family::Chaos) {
			chaos_dealt = true;
		}
		left.erase(left.begin() + static_cast<long>(at));
		weights.erase(weights.begin() + static_cast<long>(at));
		// One is a decision; two is a hand nobody would keep. Once the
		// tier has spoken, the rest of the table is ordinary metal - and
		// the draw goes on from the same roll, so the hand stays the pure
		// function of (seed, heat, salt) that a replay depends on.
		if (chaos_dealt) {
			for (size_t i = left.size(); i-- > 0;) {
				if (left[i]->family == Family::Chaos) {
					left.erase(left.begin() + static_cast<long>(i));
					weights.erase(weights.begin() + static_cast<long>(i));
				}
			}
		}
	}
	return cards;
}

int bot_pick (const std::vector<std::string>& offers, int rank_index,
		std::mt19937& rng) {
	// A pick by temperament rather than by reading the board: a low rank
	// plays to survive and leans on Fuel, a high rank plays to press and
	// leans on Flow and Risk. Collapse is never taken - the duel planner
	// assumes naive clears, and rewriting the clearing rule would sabotage
	// the very search that plays the pieces. The duel bot itself no longer
	// drafts mid-round (it carries a blade instead); this drives the test
	// harness's stand-in player, and waits for blade variety.
	// -1 when nothing acceptable is on the table.
	const double press = std::clamp(rank_index / 6.0, 0.0, 1.0);
	std::vector<int> weights;
	std::vector<int> takeable;
	for (size_t at = 0; at < offers.size(); ++at) {
		const Temper* card = find(offers[at]);
		if (card == nullptr) {
			continue;
		}
		// Never a card that rewrites what the planner searches with: the
		// two cascade rules settle a board the search never modelled, cold
		// forge freezes the clears, and the turning rack churns the hold
		// under a plan already typed. Chaos goes out whole, family and all
		// - two of those cards move the walls and the judge out from under
		// the search, and the other two curse hands the bot does not have.
		const std::string id = card->id;
		if (id == "collapse" || id == "linked_chain" || id == "cold_forge"
			|| id == "turning_rack" || card->family == Family::Chaos) {
			continue;
		}
		int weight = 0;
		switch (card->family) {
			case Family::Fuel:
				weight = 2 + static_cast<int>(6 * (1.0 - press));
				break;
			case Family::Flow:
				weight = 2 + static_cast<int>(6 * press);
				break;
			case Family::Risk:
				weight = 1 + static_cast<int>(4 * press);
				break;
			case Family::Rule:
				weight = 1;   // every_twist: harmless, occasionally taken.
				break;
			case Family::Chaos:
				break;        // Unreachable: skipped above.
			case Family::Ward:
				// A net is worth most to the ranks that still fall in. It
				// is never worth nothing, though: every branch here must
				// leave a weight of at least one, or a hand of nothing but
				// one family would divide by an empty total below.
				weight = 1 + static_cast<int>(3 * (1.0 - press));
				break;
		}
		takeable.push_back(static_cast<int>(at));
		weights.push_back(weight);
	}
	if (takeable.empty()) {
		return -1;
	}
	int total = 0;
	for (const int weight : weights) {
		total += weight;
	}
	std::uniform_int_distribution<int> pick(0, total - 1);
	int roll = pick(rng);
	size_t at = 0;
	while (at + 1 < takeable.size() && roll >= weights[at]) {
		roll -= weights[at];
		++at;
	}
	return takeable[at];
}

std::vector<std::string> blade_for (int rank_index) {
	// One build per rung, hand-set rather than rolled: a duel against the
	// same rank should be the same fight, and the escalation is the point -
	// each rung keeps what the one below carried and adds an edge. Never
	// collapse; the planner searches naive clears.
	// The three bottom rungs share one card on purpose: below the league a
	// foe should differ in hands, not in build. A beginner's opponent
	// carrying its own escalating deck is the opposite of the point.
	static const std::vector<std::vector<std::string>> blades = {
		{"thick_wick"},                                          // F
		{"thick_wick"},                                          // E
		{"thick_wick"},                                          // D
		{"thick_wick", "quench"},                                // C
		{"thick_wick", "quench", "bellows"},                     // B
		{"thick_wick", "quench", "bellows", "spark"},            // A
		{"quench", "bellows", "spark", "white_heat"},            // S
		{"quench", "bellows", "spark", "white_heat", "every_twist"},  // SS
		{"bellows", "spark", "white_heat", "overheat", "every_twist"},  // U
		{"bellows", "spark", "white_heat", "white_heat", "overheat",
			"gamble"},                                           // X
	};
	const int last = static_cast<int>(blades.size()) - 1;
	return blades[static_cast<size_t>(std::clamp(rank_index, 0, last))];
}

int embers_of (int lines, int attack) {
	// The same shape as the Flow gains: lines pay, the attack they carried
	// pays more, and nothing else does - haste alone earns no coin.
	return std::max(0, lines) * 2 + std::max(0, attack) * 3;
}

} // namespace temper
} // namespace forcetris
