// The draft, graded: what a temper moves, what it must not move, and what a
// run of them adds up to.
//
// The whole mode rests on one claim - that the sim reads its fuse and Flow
// numbers live, so replacing them mid-run changes the game from the next
// piece on without disturbing anything else. That claim is worth a test
// rather than a comment, so this drives real sims: a piece's fuse before and
// after a retune, the handling frame counts either side of one, and a run
// with a finish line stopping exactly on it.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "forcetris/bot.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/replay.hpp"
#include "forcetris/sim.hpp"
#include <set>

#include "forcetris/temper.hpp"

using namespace forcetris;
using temper::Temper;

namespace {

int failures = 0;

void check (const char* name, bool ok, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		!ok && !detail.empty() ? " -- " : "", !ok ? detail.c_str() : "");
	if (!ok) {
		++failures;
	}
}

std::string number (double value) {
	char text[64];
	std::snprintf(text, sizeof text, "%.4f", value);
	return text;
}

std::vector<int> bag (int count) {
	std::vector<int> forms;
	for (int i = 0; i < count; ++i) {
		forms.push_back(i % 7);
	}
	return forms;
}

SimConfig rules () {
	SimConfig config;
	config.fuse = true;
	config.das_ms = 100;
	config.arr_ms = 0;
	config.sdf = 40;
	config.clear_delay = false;
	config.finesse_rule = 0;
	return config;
}

// Every field of SimConfig a temper is allowed to touch, named so a change
// can be described rather than merely detected.
struct Reading {
	const char* name;
	double value;
};

std::vector<Reading> reading_of (const SimConfig& c) {
	return {
		{"das_ms", static_cast<double>(c.das_ms)},
		{"arr_ms", static_cast<double>(c.arr_ms)},
		{"dcd_ms", static_cast<double>(c.dcd_ms)},
		{"sdf", static_cast<double>(c.sdf)},
		{"are_ms", static_cast<double>(c.are_ms)},
		{"forced_delay", c.forced_delay},
		{"kicks", c.kicks ? 1. : 0.},
		{"finesse_rule", static_cast<double>(c.finesse_rule)},
		{"fall_delay", static_cast<double>(c.fall_delay)},
		{"spin_rule", static_cast<double>(c.spin_rule)},
		{"cleartype", static_cast<double>(c.cleartype)},
		{"clear_delay", c.clear_delay ? 1. : 0.},
		{"gametype", static_cast<double>(c.gametype)},
		{"timer_ms", static_cast<double>(c.timer_ms)},
		{"start_lines", static_cast<double>(c.start_lines)},
		{"line_quota", static_cast<double>(c.line_quota)},
		{"cheese_total", static_cast<double>(c.cheese_total)},
		{"cheese_period", static_cast<double>(c.cheese_period)},
		{"fuse", c.fuse ? 1. : 0.},
		{"fuse_base", c.fuse_base},
		{"fuse_min", c.fuse_min},
		{"fuse_decay", c.fuse_decay},
		{"fuse_bank_cap", c.fuse_bank_cap},
		{"fuse_draw_cap", c.fuse_draw_cap},
		{"fuse_refuel_line", c.fuse_refuel_line},
		{"fuse_refuel_attack", c.fuse_refuel_attack},
		{"flash_frac", c.flash_frac},
		{"flash_floor", c.flash_floor},
		{"flow_gain_line", c.flow_gain_line},
		{"flow_gain_attack", c.flow_gain_attack},
		{"flow_flash_gain", c.flow_flash_gain},
		{"flow_burn_loss", c.flow_burn_loss},
		{"overdrive_secs", c.overdrive_secs},
		{"overdrive_mult", c.overdrive_mult},
		{"fuse_pressure", c.fuse_pressure},
		{"sealed", static_cast<double>(c.sealed)},
		{"cold_iron", c.cold_iron ? 1. : 0.},
		{"attack_scale", c.attack_scale},
		{"crit_every", static_cast<double>(c.crit_every)},
		{"hold_churn", c.hold_churn ? 1. : 0.},
		{"score_quota", static_cast<double>(c.score_quota)},
		{"survive_ms", static_cast<double>(c.survive_ms)},
		{"flow_rail", c.flow_rail ? 1. : 0.},
		{"wild_spins", c.wild_spins ? 1. : 0.},
		{"wrap_walls", c.wrap_walls ? 1. : 0.},
		{"free_hold", c.free_hold ? 1. : 0.},
		{"sweep_every", static_cast<double>(c.sweep_every)},
		{"garbage_scale", c.garbage_scale},
		{"flow_ignite", c.flow_ignite},
		// The three the table forgot. A claimed name that is absent from
		// this list can never fail the "moves what it claims" arm, so a
		// card that silently stopped working would still pass.
		{"cheese_holes", static_cast<double>(c.cheese_holes)},
		{"cheese_messiness", static_cast<double>(c.cheese_messiness)},
		{"flow_lock_gain", c.flow_lock_gain},
	};
}

// Which fields one temper is declared to move. Written out here rather than
// derived from apply(), so the test is a second opinion and not an echo.
std::vector<std::string> claimed (const std::string& id) {
	if (id == "thick_wick") return {"fuse_base"};
	if (id == "quench") return {"fuse_refuel_line"};
	if (id == "slow_burn") return {"fuse_decay"};
	if (id == "bellows") return {"overdrive_secs"};
	if (id == "white_heat") return {"overdrive_mult"};
	if (id == "spark") return {"flow_gain_line", "flow_gain_attack"};
	if (id == "overheat") {
		return {"flow_gain_line", "flow_gain_attack", "flow_flash_gain",
			"fuse_base"};
	}
	if (id == "gamble") return {"overdrive_mult", "flow_burn_loss"};
	if (id == "collapse") return {"cleartype"};
	if (id == "every_twist") return {"spin_rule"};
	if (id == "heavy_hand") return {"attack_scale"};
	if (id == "loaded_dice") return {"crit_every"};
	if (id == "cold_forge") return {"cold_iron", "attack_scale"};
	if (id == "turning_rack") return {"hold_churn"};
	if (id == "wild_spins") return {"wild_spins"};
	if (id == "ring_walls") return {"wrap_walls", "fall_delay"};
	// The two chaos cards that curse the hands do their damage in the GUI's
	// input path, where no SimConfig field can reach; what they move here
	// is only the price they pay for it.
	if (id == "crossed_wires") return {"attack_scale"};
	if (id == "loose_ratchet") return {"flow_gain_line", "flow_gain_attack"};
	// The brands land on the FOE's board through the duel wiring, so by
	// design they move no field of this config - the claim names the foe
	// so the no-claim gate can tell "versus-only" from "forgotten".
	if (id == "frostbrand" || id == "hobnails") return {"(the foe)"};
	if (id == "sticky_tongs") return {"flow_gain_line", "flow_gain_attack"};
	if (id == "deep_bank") return {"fuse_bank_cap"};
	if (id == "hard_quench") return {"fuse_refuel_attack"};
	if (id == "draught") return {"flow_ignite"};
	if (id == "glass_edge") return {"attack_scale", "fall_delay"};
	if (id == "hair_trigger") {
		return {"flash_frac", "flash_floor", "flow_flash_gain"};
	}
	if (id == "hollow_wick") return {"fuse_decay", "overdrive_secs"};
	if (id == "linked_chain") return {"cleartype"};
	if (id == "counterweight") return {"fall_delay"};
	if (id == "free_hand") return {"free_hold"};
	if (id == "floor_sweep") return {"sweep_every"};
	if (id == "cold_shoulder") return {"garbage_scale"};
	if (id == "coolant") return {"fuse_pressure"};
	if (id == "sifter") return {"cheese_messiness"};
	return {};
}

bool names (const std::vector<std::string>& list, const std::string& want) {
	for (const std::string& entry : list) {
		if (entry == want) {
			return true;
		}
	}
	return false;
}

// Run a sim to the frame its first piece is in play, and report the fuse the
// piece was dealt.
double first_fuse (const SimConfig& config) {
	Sim sim(config, bag(20));
	while (!sim.entry()) {
		sim.step(std::optional<Event>{});
	}
	return sim.fuse_total();
}

// Frames a held direction takes to carry a piece from spawn to the wall -
// the same measurement feelcheck makes, used here only to prove a retune
// leaves it alone.
int traverse (Sim& sim) {
	while (!sim.entry()) {
		sim.step(std::optional<Event>{});
	}
	sim.step(std::optional<Event>(Event{Key::Left, true}));
	int frames = 1;
	int last = sim.piece().x;
	int still = 0;
	while (frames < 1000 && still <= 12) {
		sim.step(std::optional<Event>{});
		++frames;
		if (sim.piece().x == last) {
			++still;
		} else {
			last = sim.piece().x;
			still = 0;
		}
	}
	sim.step(std::optional<Event>(Event{Key::Left, false}));
	// Put the piece away and wait for the next one, so a second measurement
	// on the same sim starts from spawn rather than from the wall.
	sim.step(std::optional<Event>(Event{Key::Hard, true}));
	sim.step(std::optional<Event>(Event{Key::Hard, false}));
	for (int i = 0; i < 10 && !sim.entry(); ++i) {
		sim.step(std::optional<Event>{});
	}
	return frames - still;
}

} // namespace

int main () {
	// --- Every temper moves what it says, and nothing else. -----------------
	{
		bool all_declared = true;
		bool all_moved = true;
		std::string stray;
		for (const Temper& entry : temper::pool()) {
			SimConfig before = rules();
			SimConfig after = before;
			temper::apply(after, entry.id);
			const std::vector<Reading> was = reading_of(before);
			const std::vector<Reading> now = reading_of(after);
			const std::vector<std::string> want = claimed(entry.id);
			if (want.empty()) {
				all_declared = false;
				stray += std::string(entry.id) + " has no claim; ";
				continue;
			}
			for (size_t at = 0; at < was.size(); ++at) {
				const bool moved = std::abs(was[at].value - now[at].value) > 1e-9;
				const bool allowed = names(want, was[at].name);
				if (moved && !allowed) {
					all_declared = false;
					stray += std::string(entry.id) + " moved "
						+ was[at].name + "; ";
				}
				if (!moved && allowed) {
					all_moved = false;
					stray += std::string(entry.id) + " left "
						+ was[at].name + " alone; ";
				}
			}
		}
		check("no temper moves a field it does not claim", all_declared, stray);
		check("every temper moves every field it claims", all_moved, stray);
	}

	// --- The declared arithmetic, spelled out for a few. --------------------
	{
		SimConfig config = rules();
		const double base = config.fuse_base;
		temper::apply(config, "thick_wick");
		temper::apply(config, "thick_wick");
		check("Thick Wick stacks", std::abs(config.fuse_base - (base + 1.0)) < 1e-9,
			number(config.fuse_base));

		SimConfig risky = rules();
		const double flow = risky.flow_gain_line;
		temper::apply(risky, "overheat");
		check("Overheat doubles Flow and shortens the fuse",
			std::abs(risky.flow_gain_line - flow * 2.) < 1e-9
				&& risky.fuse_base < rules().fuse_base);

		SimConfig quick = rules();
		const double line = quick.flow_gain_line;
		const double attack = quick.flow_gain_attack;
		temper::apply(quick, "spark");
		check("Spark charges both halves of the gauge",
			std::abs(quick.flow_gain_line - (line + 2.)) < 1e-9
				&& std::abs(quick.flow_gain_attack - (attack + 2.)) < 1e-9);

		SimConfig bold = rules();
		temper::apply(bold, "gamble");
		check("Gamble pays its multiplier and charges its price",
			std::abs(bold.overdrive_mult - (rules().overdrive_mult + 1.)) < 1e-9
				&& std::abs(bold.flow_burn_loss
					- (rules().flow_burn_loss + 15.)) < 1e-9);

		// The two guards: neither may put the rules somewhere the sim's own
		// arithmetic says is impossible.
		SimConfig slow = rules();
		for (int i = 0; i < 6; ++i) {
			temper::apply(slow, "slow_burn");
		}
		check("Slow Burn never stops the schedule tightening",
			slow.fuse_decay >= 0.03 - 1e-9, number(slow.fuse_decay));
		SimConfig hot = rules();
		for (int i = 0; i < 6; ++i) {
			temper::apply(hot, "overheat");
		}
		check("Overheat never burns the wick below the schedule's own floor",
			hot.fuse_base >= hot.fuse_min - 1e-9, number(hot.fuse_base));
	}

	// --- The V2.1d cards' arithmetic. ---------------------------------------
	{
		SimConfig config = rules();
		temper::apply(config, "heavy_hand");
		check("Heavy Hand weighs in",
			std::abs(config.attack_scale - 1.25) < 1e-9);
		temper::apply(config, "heavy_hand");
		check("and stacks", std::abs(config.attack_scale - 1.5) < 1e-9);

		SimConfig dice = rules();
		temper::apply(dice, "loaded_dice");
		check("Loaded Dice land every third strike", dice.crit_every == 3);
		temper::apply(dice, "loaded_dice");
		check("and every other with the second copy", dice.crit_every == 2);

		SimConfig cold = rules();
		temper::apply(cold, "cold_forge");
		check("Cold Forge freezes your iron and arms your hand",
			cold.cold_iron && std::abs(cold.attack_scale - 1.75) < 1e-9);

		SimConfig rack = rules();
		temper::apply(rack, "turning_rack");
		check("The Turning Rack stirs the hold", rack.hold_churn);
	}

	// --- The ward cards' arithmetic, and their floors. ----------------------
	// Every ward that subtracts has a floor, and the floors are what keep a
	// stack of them from putting the rules somewhere the sim's own
	// arithmetic says is impossible - the same guard Slow Burn and Overheat
	// carry above.
	{
		SimConfig heavy = rules();
		temper::apply(heavy, "counterweight");
		check("The Counterweight eases the fall",
			heavy.fall_delay == rules().fall_delay + 6,
			std::to_string(heavy.fall_delay));
		temper::apply(heavy, "counterweight");
		check("and stacks", heavy.fall_delay == rules().fall_delay + 12,
			std::to_string(heavy.fall_delay));

		SimConfig hand = rules();
		temper::apply(hand, "free_hand");
		check("The Free Hand unlocks the box", hand.free_hold);

		SimConfig sweep = rules();
		temper::apply(sweep, "floor_sweep");
		check("The Floor Sweep sweeps every eighth clear",
			sweep.sweep_every == 8, std::to_string(sweep.sweep_every));
		temper::apply(sweep, "floor_sweep");
		check("and every fifth with the second copy",
			sweep.sweep_every == 5, std::to_string(sweep.sweep_every));

		SimConfig cold = rules();
		temper::apply(cold, "cold_shoulder");
		check("The Cold Shoulder thins what lands",
			std::abs(cold.garbage_scale - 0.75) < 1e-9,
			number(cold.garbage_scale));
		for (int i = 0; i < 6; ++i) {
			temper::apply(cold, "cold_shoulder");
		}
		check("and never thins a blow away to nothing",
			cold.garbage_scale >= 0.5 - 1e-9, number(cold.garbage_scale));

		SimConfig cool = rules();
		temper::apply(cool, "coolant");
		check("Coolant leans the other forge's heat off you",
			std::abs(cool.fuse_pressure - 1.20) < 1e-9,
			number(cool.fuse_pressure));
		for (int i = 0; i < 6; ++i) {
			temper::apply(cool, "coolant");
		}
		check("and never turns their Overdrive into a kindness",
			cool.fuse_pressure >= 1.0 - 1e-9, number(cool.fuse_pressure));

		SimConfig sift = rules();
		temper::apply(sift, "sifter");
		check("The Sifter lines the rubble up",
			sift.cheese_messiness == 40,
			std::to_string(sift.cheese_messiness));
		for (int i = 0; i < 5; ++i) {
			temper::apply(sift, "sifter");
		}
		check("and never deals one perfectly clean well forever",
			sift.cheese_messiness >= 20,
			std::to_string(sift.cheese_messiness));
	}

	// --- The rest of the V2.4 cards' arithmetic. ----------------------------
	{
		SimConfig bank = rules();
		temper::apply(bank, "deep_bank");
		check("The Deep Bank deepens the reservoir",
			std::abs(bank.fuse_bank_cap - (rules().fuse_bank_cap + 2.))
				< 1e-9, number(bank.fuse_bank_cap));

		SimConfig hard = rules();
		temper::apply(hard, "hard_quench");
		check("Hard Quench refills on the blow as well",
			std::abs(hard.fuse_refuel_attack
				- (rules().fuse_refuel_attack + 0.3)) < 1e-9,
			number(hard.fuse_refuel_attack));

		SimConfig draw = rules();
		temper::apply(draw, "draught");
		check("The Draught lowers the bar Overdrive lights at",
			std::abs(draw.flow_ignite - 88.) < 1e-9,
			number(draw.flow_ignite));
		for (int i = 0; i < 8; ++i) {
			temper::apply(draw, "draught");
		}
		check("and never lights it on a breath",
			draw.flow_ignite >= 60. - 1e-9, number(draw.flow_ignite));

		SimConfig edge = rules();
		temper::apply(edge, "glass_edge");
		check("The Glass Edge arms the hand and hurries the forge",
			std::abs(edge.attack_scale - 1.6) < 1e-9
				&& edge.fall_delay == rules().fall_delay - 6,
			number(edge.attack_scale) + " / "
				+ std::to_string(edge.fall_delay));
		for (int i = 0; i < 6; ++i) {
			temper::apply(edge, "glass_edge");
		}
		temper::apply(edge, "ring_walls");
		check("and no stack of hurry leaves a piece unplayable",
			edge.fall_delay >= 6, std::to_string(edge.fall_delay));

		// The flash window is max(floor, total * frac), so a card that
		// moved only the fraction would be a no-op on a short wick. Both
		// move, or the face is a lie.
		SimConfig hair = rules();
		temper::apply(hair, "hair_trigger");
		check("The Hair Trigger narrows the window at both ends",
			hair.flash_frac < rules().flash_frac
				&& hair.flash_floor < rules().flash_floor
				&& hair.flow_flash_gain > rules().flow_flash_gain,
			number(hair.flash_frac) + " / " + number(hair.flash_floor));
		for (int i = 0; i < 6; ++i) {
			temper::apply(hair, "hair_trigger");
		}
		check("and never closes it entirely",
			hair.flash_frac >= 0.08 - 1e-9
				&& hair.flash_floor >= 0.08 - 1e-9,
			number(hair.flash_frac));

		SimConfig hollow = rules();
		temper::apply(hollow, "hollow_wick");
		check("The Hollow Wick buys the burn with the schedule",
			hollow.overdrive_secs > rules().overdrive_secs
				&& hollow.fuse_decay > rules().fuse_decay);

		SimConfig linked = rules();
		temper::apply(linked, "linked_chain");
		check("The Linked Chain settles the pieces whole",
			linked.cleartype == 2, std::to_string(linked.cleartype));
	}

	// --- The pool's shape. --------------------------------------------------
	// Six families, thirty-four cards, and the counts written down here so
	// that growing one family is a decision rather than an accident.
	{
		std::map<int, int> counted;
		std::set<std::string> seen;
		bool unique = true;
		bool wordless = true;
		bool findable = true;
		std::string detail;
		for (const temper::Temper& card : temper::pool()) {
			++counted[static_cast<int>(card.family)];
			unique = unique && seen.insert(card.id).second;
			findable = findable && temper::find(card.id) == &card;
			for (const char* c = card.text; *c != '\0'; ++c) {
				if (*c >= '0' && *c <= '9') {
					wordless = false;
					detail += std::string(card.id) + " counts; ";
				}
			}
		}
		check("the pool is thirty-four cards in six families",
			temper::pool().size() == 34 && counted.size() == 6,
			std::to_string(temper::pool().size()) + " cards, "
				+ std::to_string(counted.size()) + " families");
		check("and the families are five, seven, seven, four, five, six",
			counted[0] == 5 && counted[1] == 7 && counted[2] == 7
				&& counted[3] == 4 && counted[4] == 5 && counted[5] == 6,
			std::to_string(counted[0]) + "/" + std::to_string(counted[1])
				+ "/" + std::to_string(counted[2]) + "/"
				+ std::to_string(counted[3]) + "/"
				+ std::to_string(counted[4]) + "/"
				+ std::to_string(counted[5]));
		check("no two cards share an id, and every id is findable",
			unique && findable);
		// The card face carries no numbers - the arithmetic lives in
		// apply() and in the README, never on the table.
		check("no card face says a number", wordless, detail);
	}

	// --- The dice and the rack, through a live sim. -------------------------
	{
		// Two identical games of three quads, one with loaded dice: the
		// first two strikes match, the third lands double.
		const auto quads = [] (int crit_every) {
			SimConfig config;
			config.forced_delay = 0.;
			config.finesse_rule = 0;
			config.sdf = 40;
			config.das_ms = 330;
			config.clear_delay = false;
			config.crit_every = crit_every;
			Sim sim(config, std::vector<int>{0, 0, 0, 0, 0});
			// A twelve-deep well: each upright I fills four rows of the
			// gap, so three drops are three quads back to back.
			Board board;
			for (int y = kHeight - 12; y < kHeight; ++y) {
				for (int x = 0; x < kWidth; ++x) {
					if (x != kSpawnX + 1) {
						board.set(x, y, 3);
					}
				}
			}
			sim.seed(board);
			std::vector<int> attacks;
			for (int piece = 0; piece < 3; ++piece) {
				for (int i = 0; i < 100 && !sim.entry(); ++i) {
					sim.step(std::nullopt);
				}
				sim.step(Event{Key::Cw, true});
				sim.step(Event{Key::Cw, false});
				sim.step(Event{Key::Hard, true});
				sim.step(Event{Key::Hard, false});
				for (int i = 0; i < 20; ++i) {
					sim.step(std::nullopt);
				}
				attacks.push_back(sim.locked().back().attack);
			}
			return attacks;
		};
		const std::vector<int> plain = quads(0);
		const std::vector<int> diced = quads(3);
		check("the first two strikes are honest",
			plain.size() == 3 && diced.size() == 3
				&& plain[0] == diced[0] && plain[1] == diced[1]
				&& plain[0] > 0);
		check("the third lands double", diced[2] == plain[2] * 2,
			number(diced[2]));

		// The rack: hold a piece, clear a line, and the held piece has
		// been traded for the queue's front.
		SimConfig config;
		config.forced_delay = 0.;
		config.finesse_rule = 0;
		config.sdf = 40;
		config.das_ms = 330;
		config.clear_delay = false;
		config.hold_churn = true;
		Sim sim(config, std::vector<int>{3, 0, 4, 5, 6});
		Board board;
		for (int x = 0; x < kWidth; ++x) {
			if (x != kSpawnX + 1) {
				board.set(x, kHeight - 1, 3);
			}
		}
		sim.seed(board);
		for (int i = 0; i < 100 && !sim.entry(); ++i) {
			sim.step(std::nullopt);
		}
		sim.step(Event{Key::Hold, true});    // S away; the I comes out.
		sim.step(Event{Key::Hold, false});
		for (int i = 0; i < 100 && !sim.entry(); ++i) {
			sim.step(std::nullopt);
		}
		sim.step(Event{Key::Cw, true});
		sim.step(Event{Key::Cw, false});
		sim.step(Event{Key::Hard, true});
		sim.step(Event{Key::Hard, false});
		for (int i = 0; i < 20; ++i) {
			sim.step(std::nullopt);
		}
		check("the rack trades the hold on a clear",
			sim.stored() == 4, number(sim.stored()));
	}

	// --- The roll. ----------------------------------------------------------
	{
		bool repeats = true;
		bool distinct = true;
		bool three = true;
		for (int heat = 0; heat < temper::kHeats; ++heat) {
			const auto once = temper::offer(1234u, heat, {});
			const auto again = temper::offer(1234u, heat, {});
			repeats = repeats && once == again;
			three = three && once.size() == 3;
			distinct = distinct && once[0] != once[1] && once[1] != once[2]
				&& once[0] != once[2];
		}
		check("the same run offers the same cards at the same heat", repeats);
		check("a heat offers three of them", three);
		check("the three differ", distinct);

		const auto early = temper::offer(1234u, 0, {});
		const auto late = temper::offer(4321u, 0, {});
		check("a different run is offered something else", early != late);

		// A run that has taken every copy of a temper is never shown it.
		std::vector<std::string> taken;
		for (int i = 0; i < 3; ++i) {
			taken.emplace_back("quench");
		}
		bool never = true;
		for (int heat = 0; heat < 200; ++heat) {
			for (const std::string& card
				: temper::offer(static_cast<unsigned>(heat), heat, taken)) {
				never = never && card != "quench";
			}
		}
		check("a temper taken to its cap stops being offered", never);
	}

	// --- Applying a whole run's worth. --------------------------------------
	{
		const SimConfig start = rules();
		const SimConfig built = temper::tempered(start,
			{"quench", "bellows", "quench", "white_heat"});
		check("a run's tempers compound in order",
			std::abs(built.fuse_refuel_line - (start.fuse_refuel_line + 0.6)) < 1e-9
				&& std::abs(built.overdrive_secs - (start.overdrive_secs + 3.)) < 1e-9
				&& std::abs(built.overdrive_mult - (start.overdrive_mult + 0.5)) < 1e-9);
		check("an id this build does not know is read rather than refused",
			temper::find("no_such_temper") == nullptr
				&& temper::tempered(start, {"no_such_temper"}).fuse_base
					== start.fuse_base);
	}

	// --- The claim the whole mode rests on: retune lands at once. -----------
	{
		SimConfig config = rules();
		const double plain = first_fuse(config);
		SimConfig longer = config;
		temper::apply(longer, "thick_wick");
		check("a temper taken before the game changes the first fuse",
			std::abs(first_fuse(longer) - (plain + 0.5)) < 1e-9,
			number(first_fuse(longer)) + " vs " + number(plain));

		// The same, mid-run: the piece in play keeps the fuse it was dealt,
		// and the next one is dealt the new schedule.
		Sim sim(config, bag(40));
		while (!sim.entry()) {
			sim.step(std::optional<Event>{});
		}
		const double before = sim.fuse_total();
		sim.retune(longer);
		check("the piece in play keeps the fuse it was dealt",
			std::abs(sim.fuse_total() - before) < 1e-9);
		sim.step(std::optional<Event>(Event{Key::Hard, true}));
		for (int i = 0; i < 8 && !sim.entry(); ++i) {
			sim.step(std::optional<Event>{});
		}
		check("the next piece is dealt the tempered fuse",
			std::abs(sim.fuse_total() - (before + 0.5)) < 1e-9,
			number(sim.fuse_total()) + " vs " + number(before));
	}

	// --- And leaves the pad alone. -----------------------------------------
	{
		SimConfig config = rules();
		Sim quiet(config, bag(40));
		const int plain = traverse(quiet);
		Sim retuned(config, bag(40));
		const int first = traverse(retuned);
		// Everything a temper could reach, at once.
		SimConfig everything = config;
		for (const Temper& entry : temper::pool()) {
			temper::apply(everything, entry.id);
		}
		retuned.retune(everything);
		const int after = traverse(retuned);
		check("a retune does not touch the handling",
			plain == first && first == after,
			std::to_string(plain) + " / " + std::to_string(first) + " / "
				+ std::to_string(after));
	}

	// --- The finish line. ---------------------------------------------------
	{
		SimConfig config = rules();
		config.fuse = false;
		config.forced_delay = 0.;
		config.line_quota = 2;
		config.start_lines = 0;
		// A board one row short of two clears, filled but for a column.
		Board board;
		for (int r = 0; r < 2; ++r) {
			const int y = kHeight - 1 - r;
			for (int x = 0; x < kWidth - 1; ++x) {
				board.set(x, y, GARBAGE);
			}
		}
		std::vector<int> forms{I};
		for (const int form : bag(20)) {
			forms.push_back(form);
		}
		Sim sim(config, forms);
		sim.seed(board);
		while (!sim.entry()) {
			sim.step(std::optional<Event>{});
		}
		sim.step(std::optional<Event>(Event{Key::Cw, true}));
		sim.step(std::optional<Event>(Event{Key::Cw, false}));
		sim.step(std::optional<Event>(Event{Key::Right, true}));
		for (int i = 0; i < 40; ++i) {
			sim.step(std::optional<Event>{});
		}
		sim.step(std::optional<Event>(Event{Key::Right, false}));
		check("a run short of its quota is still running", !sim.won());
		sim.step(std::optional<Event>(Event{Key::Hard, true}));
		for (int i = 0; i < 10 && !sim.won(); ++i) {
			sim.step(std::optional<Event>{});
		}
		check("crossing the finish line wins the run",
			sim.won() && sim.lines_cleared() >= 2,
			std::to_string(sim.lines_cleared()) + " lines, won=" + (sim.won() ? "y" : "n"));

		// And a game with no quota never wins this way.
		SimConfig endless = config;
		endless.line_quota = 0;
		Sim forever(endless, forms);
		forever.seed(board);
		for (int i = 0; i < 200; ++i) {
			forever.step(std::optional<Event>{});
		}
		check("a game with no finish line has none", !forever.won());
	}

	// --- The heat counter, which everything shares. --------------------------
	{
		check("ten lines forge a heat",
			temper::heats_done(0, 0, false) == 0
				&& temper::heats_done(9, 99, false) == 0
				&& temper::heats_done(10, 0, false) == 1
				&& temper::heats_done(119, 0, false) == 11
				&& temper::heats_done(120, 0, false) == 12);
		check("a dig race counts dug rows instead",
			temper::heats_done(99, 5, true) == 0
				&& temper::heats_done(0, 6, true) == 1
				&& temper::heats_done(0, 17, true) == 2);
		check("a broken counter never forges a negative heat",
			temper::heats_done(-5, -5, false) == 0
				&& temper::heats_done(-5, -5, true) == 0);
	}

	// --- The bot's pick. ----------------------------------------------------
	{
		const std::vector<std::string> table
			= {"thick_wick", "bellows", "overheat"};
		std::mt19937 one(77u);
		std::mt19937 two(77u);
		const int first = temper::bot_pick(table, 3, one);
		check("the bot's pick is a card on the table",
			first >= 0 && first < static_cast<int>(table.size()));
		check("the same seed picks the same card",
			temper::bot_pick(table, 3, two) == first);

		// Collapse is never taken, wherever it sits and whatever else is up.
		bool never = true;
		for (unsigned roll = 0; roll < 300 && never; ++roll) {
			std::mt19937 rng(roll);
			const std::vector<std::string> mixed
				= {"collapse", "quench", "white_heat"};
			const int at = temper::bot_pick(mixed,
				static_cast<int>(roll % 7), rng);
			never = at != 0 && at >= 0;
		}
		check("the bot never takes Collapse", never);
		// Nor any chaos card, whichever one is on the table: two of them
		// move the walls and the judge out from under its search, and the
		// other two curse hands it does not have.
		bool no_chaos = true;
		for (const temper::Temper& card : temper::pool()) {
			if (card.family != temper::Family::Chaos) {
				continue;
			}
			for (unsigned roll = 0; roll < 60 && no_chaos; ++roll) {
				std::mt19937 rng(roll);
				const std::vector<std::string> mixed
					= {card.id, "quench", "white_heat"};
				const int at = temper::bot_pick(mixed,
					static_cast<int>(roll % 7), rng);
				no_chaos = at != 0 && at >= 0;
			}
		}
		check("the bot never takes a chaos card", no_chaos);
		// The linked chain settles a board the search never modelled, the
		// same way collapse does, so it goes out with it.
		bool no_chain = true;
		for (unsigned roll = 0; roll < 300 && no_chain; ++roll) {
			std::mt19937 rng(roll);
			const std::vector<std::string> mixed
				= {"linked_chain", "quench", "white_heat"};
			const int at = temper::bot_pick(mixed,
				static_cast<int>(roll % 7), rng);
			no_chain = at != 0 && at >= 0;
		}
		check("the bot never takes the linked chain", no_chain);
		std::mt19937 rng(5u);
		check("a table with nothing acceptable returns no pick",
			temper::bot_pick({"collapse"}, 6, rng) == -1
				&& temper::bot_pick({"collapse", "linked_chain"}, 6, rng)
					== -1
				&& temper::bot_pick({}, 6, rng) == -1);
		// Every family the picker will take must carry a weight of at
		// least one at every rank. A branch that yielded zero would leave
		// a single-family hand dividing by an empty total, which is not a
		// wrong pick but an undefined one - so the pin is on the picker's
		// arithmetic, not its taste.
		bool weighted = true;
		std::string light;
		for (const temper::Temper& card : temper::pool()) {
			const std::string id = card.id;
			if (id == "collapse" || id == "linked_chain"
				|| id == "cold_forge" || id == "turning_rack"
				|| card.family == temper::Family::Chaos) {
				continue;
			}
			for (int rank = 0; rank < 8; ++rank) {
				std::mt19937 one(static_cast<unsigned>(rank));
				if (temper::bot_pick({card.id}, rank, one) != 0) {
					weighted = false;
					light += id + " at " + std::to_string(rank) + "; ";
				}
			}
		}
		check("every family the picker will take carries a weight",
			weighted, light);

		// Temperament: over many rolls, the lowest rank reaches for Fuel
		// more often than the highest does. Loose on purpose - the claim
		// is a lean, not a schedule.
		int low_fuel = 0;
		int high_fuel = 0;
		const std::vector<std::string> spread
			= {"thick_wick", "bellows", "gamble"};
		for (unsigned roll = 0; roll < 400; ++roll) {
			std::mt19937 a(roll);
			std::mt19937 b(roll);
			if (temper::bot_pick(spread, 0, a) == 0) {
				++low_fuel;
			}
			if (temper::bot_pick(spread, 6, b) == 0) {
				++high_fuel;
			}
		}
		check("a low rank leans on Fuel harder than a high rank",
			low_fuel > high_fuel,
			std::to_string(low_fuel) + " vs " + std::to_string(high_fuel));
	}

	// --- The reroll salt. ---------------------------------------------------
	{
		// Salt zero is every caller that predates rerolls, so it must be a
		// perfect no-op; each further salt is one paid reroll of the same
		// heat, deterministic like the heat itself.
		const auto plain = temper::offer(777u, 3, {});
		check("salt zero deals the hand it always dealt",
			temper::offer(777u, 3, {}, 0) == plain);
		const auto rerolled = temper::offer(777u, 3, {}, 1);
		check("a reroll deals a different hand", rerolled != plain);
		check("the same reroll deals the same hand",
			temper::offer(777u, 3, {}, 1) == rerolled
				&& temper::offer(777u, 3, {}, 2) != rerolled);
		check("a reroll still honours the caps",
			rerolled.size() == 3 && rerolled[0] != rerolled[1]
				&& rerolled[1] != rerolled[2] && rerolled[0] != rerolled[2]);
	}

	// --- How often each family comes up. ------------------------------------
	// The weights live in an anonymous namespace, so the only honest way to
	// read them is through the roll itself. Bounds rather than equalities:
	// the claim is a lean, and the pool is meant to grow under it.
	{
		std::map<int, int> seen;
		for (unsigned seed = 1; seed <= 400; ++seed) {
			for (int heat = 0; heat < 10; ++heat) {
				for (const std::string& id
					: temper::offer(seed * 977u, heat, {})) {
					const temper::Temper* card = temper::find(id);
					if (card != nullptr) {
						++seen[static_cast<int>(card->family)];
					}
				}
			}
		}
		const int fuel = seen[static_cast<int>(temper::Family::Fuel)];
		const int flow = seen[static_cast<int>(temper::Family::Flow)];
		const int risk = seen[static_cast<int>(temper::Family::Risk)];
		const int rule = seen[static_cast<int>(temper::Family::Rule)];
		const int chaos = seen[static_cast<int>(temper::Family::Chaos)];
		const int ward = seen[static_cast<int>(temper::Family::Ward)];
		const std::string tally = std::to_string(fuel) + "/"
			+ std::to_string(flow) + "/" + std::to_string(risk) + "/"
			+ std::to_string(rule) + "/" + std::to_string(chaos) + "/"
			+ std::to_string(ward);
		check("Flow is still the family the forge offers most",
			flow > fuel && flow > risk && flow > ward, tally);
		check("and the rule and chaos cards are still the rarest",
			rule < ward && chaos < ward && rule < risk && chaos < risk,
			tally);
		// The ward stands with risk, not with fuel: half its cards are
		// only worth taking in the rooms that threaten what they guard.
		check("the wards come up about as often as the risks",
			ward > risk / 2 && ward < risk * 2, tally);
		check("and every family comes up at all",
			seen.size() == 6, std::to_string(seen.size()));
	}

	// --- The run economy's arithmetic. --------------------------------------
	{
		check("embers pay on lines and attack, and on nothing else",
			temper::embers_of(0, 0) == 0
				&& temper::embers_of(10, 0) == 20
				&& temper::embers_of(0, 10) == 30
				&& temper::embers_of(4, 6) == 26
				&& temper::embers_of(-3, -3) == 0);
		check("a reroll is cheaper than a second card",
			temper::kRerollCost > 0
				&& temper::kExtraPickCost > temper::kRerollCost);
		check("melting a card down sits between the two",
			temper::kRemoveCost > temper::kRerollCost
				&& temper::kRemoveCost < temper::kExtraPickCost);
	}

	// --- The blades. --------------------------------------------------------
	{
		bool sane = true;
		bool armed = true;
		std::string detail;
		size_t below = 0;
		for (size_t rank = 0; rank < bot::ranks().size(); ++rank) {
			const std::vector<std::string> blade
				= temper::blade_for(static_cast<int>(rank));
			armed = armed && !blade.empty();
			below = std::max(below, blade.size());
			for (const std::string& id : blade) {
				if (id == "collapse") {
					sane = false;
					detail += bot::ranks()[rank].name;
					detail += " carries collapse; ";
				} else if (temper::find(id) == nullptr) {
					sane = false;
					detail += id + " unknown; ";
				}
			}
		}
		check("every rank carries a blade of real cards", armed && sane, detail);
		check("the ladder's blades escalate",
			temper::blade_for(0).size()
					< temper::blade_for(
						static_cast<int>(bot::ranks().size()) - 1).size()
				&& temper::blade_for(99).size()
					== temper::blade_for(
						static_cast<int>(bot::ranks().size()) - 1).size());
	}

	// --- A whole run of the mode, played out. -------------------------------
	{
		// The pieces of the mode, driven the way the GUI drives them: play
		// until the line counter crosses a heat, take a card, retune from
		// the run's start plus everything taken so far, play on. The bot is
		// the only thing here the mode does not use; it is standing in for
		// a player good enough to reach the twelfth heat.
		SimConfig start = rules();
		start.line_quota = temper::kQuota;
		start.forced_delay = 0.;
		const unsigned seed = 20260826u;
		Sim sim(start, bag(700));
		bot::Driver driver(seed, bot::ranks().back());
		std::vector<std::string> taken;
		int heat = 0;
		long frame = 0;
		bool ran_dry = false;
		bool alive = true;
		while (alive && frame < 60000) {
			const std::optional<Event> event = driver.next(sim);
			alive = sim.step(event);
			++frame;
			const int forged = temper::heats_done(
				sim.lines_cleared(), sim.downstack(), false);
			if (forged > heat && heat < temper::kHeats) {
				const std::vector<std::string> cards
					= temper::offer(seed, heat, taken);
				if (cards.empty()) {
					ran_dry = true;
					break;
				}
				// The bot's own picker, seeded, so the harness drafts the
				// way a real duel bot would - which also keeps it off
				// collapse, the one card that would flip the clearing rule
				// out from under the naive planner driving the run.
				std::mt19937 picker(seed + static_cast<unsigned>(heat));
				const int at = temper::bot_pick(cards,
					static_cast<int>(bot::ranks().size()) - 1, picker);
				if (at >= 0) {
					taken.push_back(cards[static_cast<size_t>(at)]);
					sim.retune(temper::tempered(start, taken));
				}
				++heat;
			}
		}
		check("a Tempering run reaches its twelfth heat and is forged",
			sim.won() && !ran_dry,
			std::to_string(sim.lines_cleared()) + " lines at heat "
				+ std::to_string(heat));
		// Eleven picks forge a run - the twelfth crossing is the win - and
		// the picker may pass a heat by, so the pin is a floor, not the
		// count the GUI happens to reach.
		check("and drafted most of the heats it crossed",
			static_cast<int>(taken.size()) >= 10,
			std::to_string(taken.size()) + " taken");
		// The build actually moved the rules the run finished under.
		const SimConfig built = temper::tempered(start, taken);
		// Named fields would go stale the moment a family is added; the
		// reading is every field the gate knows, which is strictly more.
		const std::vector<Reading> before = reading_of(start);
		const std::vector<Reading> after = reading_of(built);
		bool moved = before.size() != after.size();
		for (size_t i = 0; i < before.size() && i < after.size(); ++i) {
			moved = moved || before[i].value != after[i].value;
		}
		check("the rules it finished under are not the ones it started with",
			moved);
	}

	// --- What adding the mode cost the score file. --------------------------
	{
		// Tempering needed a seventh variant table, which makes this build
		// write a longer fusescore.dat than the one already on a player's
		// disk. The reader has to carry that file forward rather than
		// declare it the wrong length, or the arc would quietly cost
		// everyone every variant score they had.
		namespace fs = std::filesystem;
		std::error_code ignored;
		const fs::path folder = fs::temp_directory_path() / "forcetris-temper";
		fs::remove_all(folder, ignored);
		fs::create_directories(folder, ignored);

		hiscore::Entry mine;
		const char* who = "SMITH   ";
		std::copy(who, who + 8, mine.name.begin());
		mine.score = 123456;
		mine.lines = 40;
		mine.timer = 6000;
		hiscore::submit_fuse(folder.string(), "ignition", mine);

		// Cut the file back to the six tables an older build wrote.
		const fs::path file = folder / "fusescore.dat";
		std::string data;
		{
			std::ifstream source(file, std::ios::binary);
			data.assign(std::istreambuf_iterator<char>(source),
				std::istreambuf_iterator<char>());
		}
		const size_t six = 6 * hiscore::kPerTable * hiscore::kRecordBytes;
		check("this build writes a table per mode",
			data.size() == static_cast<size_t>(hiscore::kFuseTables)
				* hiscore::kPerTable * hiscore::kRecordBytes,
			std::to_string(data.size()) + " bytes");
		{
			std::ofstream out(file, std::ios::binary | std::ios::trunc);
			out.write(data.data(), static_cast<std::streamsize>(six));
		}
		fs::remove(folder / "back" / "fusescore.bak", ignored);

		const hiscore::FuseTables read = hiscore::load_fuse(folder.string());
		check("a six-table file keeps its scores",
			read[0][0].score == 123456
				&& hiscore::shown_name(read[0][0]) == "SMITH");
		check("and the seventh table starts empty",
			read[hiscore::fuse_table_for("temper")][0].score == 0);
		check("temper has a table of its own",
			hiscore::fuse_table_for("temper") == 6
				&& hiscore::fuse_table_for("ignition") == 0);
		fs::remove_all(folder, ignored);
	}

	// --- A run's build survives the file. -----------------------------------
	{
		// The replay's fuse block records the rules a run *started* under;
		// on a Tempering run those numbers are only half the story, so the
		// draft goes in beside them. The trainer's files must not grow a key
		// for it, which is the second half of this check.
		namespace fs = std::filesystem;
		std::error_code ignored;
		const fs::path folder = fs::temp_directory_path() / "forcetris-temper-rp";
		fs::remove_all(folder, ignored);
		fs::create_directories(folder, ignored);

		const std::vector<std::string> build
			= {"quench", "overheat", "bellows", "quench"};
		replay::Meta meta;
		meta.played = "2026-01-01T00:00:00";
		meta.gametype = "temper";
		meta.fuse = true;
		meta.fuse_base = 3.0;
		meta.tempers = build;
		replay::Placement place;
		place.form = I;
		place.x = 4;
		place.y = 18;
		replay::Recorder recorder;
		recorder.begin(meta);
		recorder.add(place);
		std::optional<replay::Replay> written
			= recorder.finish(1000, 4, 0, 12.5, true);
		check("a Tempering run records", written.has_value());
		if (written.has_value()) {
			check("its file saves", replay::save(*written, folder.string()));
			const std::optional<replay::Replay> read
				= replay::load(written->path);
			check("and reads back with the build it was played with",
				read.has_value() && read->meta.tempers == build);
		}

		// A file with no draft carries no key for one.
		replay::Meta plain;
		plain.played = "2026-01-01T00:00:01";
		plain.gametype = "ignition";
		plain.fuse = true;
		replay::Recorder quiet;
		quiet.begin(plain);
		quiet.add(place);
		std::optional<replay::Replay> other = quiet.finish(10, 0, 0, 1., true);
		if (other.has_value() && replay::save(*other, folder.string())) {
			std::ifstream source(other->path);
			const std::string text{std::istreambuf_iterator<char>(source),
				std::istreambuf_iterator<char>{}};
			check("a run with no draft writes no key for one",
				text.find("tempers") == std::string::npos);
		} else {
			check("a run with no draft writes no key for one", false,
				"could not save the control file");
		}
		fs::remove_all(folder, ignored);
	}

	std::printf("%s\n", failures == 0 ? "all temper checks passed" : "FAILURES");
	return failures == 0 ? 0 : 1;
}
