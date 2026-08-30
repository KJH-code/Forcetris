// The fuse ruleset, pinned: the schedule shrinks with the level and never
// past its floor, the bank only pays out once the schedule squeezes, the
// Flash window charges Flow and a burned fuse bleeds it, Overdrive freezes
// the burn and multiplies what goes out, a hold does not reset the clock -
// and with the flag off, none of it exists.
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/replay.hpp"
#include "forcetris/sim.hpp"

using namespace forcetris;

namespace {

int failures = 0;

void check (const char* name, bool ok, const std::string& detail = "") {
	std::printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name,
		!ok && !detail.empty() ? " -- " : "", !ok ? detail.c_str() : "");
	if (!ok) {
		++failures;
	}
}

SimConfig fused () {
	SimConfig config;
	config.forced_delay = 0.;   // The fuse replaces it; both on would be odd.
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.fall_delay = 1000;   // Gravity out of the way; the fuse is the clock.
	config.fuse = true;
	// The wick's own numbers, stated here rather than borrowed from the
	// shipped defaults. Everything below tests the schedule's ARITHMETIC -
	// base, shave, floor - and it should keep testing that when the
	// shipped tuning is retuned for whoever is meant to be playing. Those
	// values get their own pin, first thing in main().
	config.fuse_base = 3.0;
	config.fuse_decay = 0.15;
	config.fuse_min = 0.8;
	return config;
}

// A double said plainly, for a failure's detail line.
std::string number (double value) {
	char text[32];
	std::snprintf(text, sizeof text, "%.3f", value);
	return text;
}

std::vector<int> bags () {
	std::vector<int> dealt;
	for (int i = 0; i < 20; ++i) {
		for (int form = 0; form < 7; ++form) {
			dealt.push_back(form);
		}
	}
	return dealt;
}

void wait_spawn (Sim& sim, std::set<std::string>* heard = nullptr) {
	for (int i = 0; i < 100 && !sim.entry(); ++i) {
		sim.step(std::nullopt);
		if (heard != nullptr) {
			heard->insert(sim.cues().begin(), sim.cues().end());
		}
	}
}

// Steps until the current piece is gone (locked), collecting cues.
void run_out (Sim& sim, std::set<std::string>& heard, int limit = 400) {
	const size_t locks = sim.locked().size();
	for (int i = 0; i < limit && sim.locked().size() == locks; ++i) {
		if (!sim.step(std::nullopt)) {
			break;
		}
		heard.insert(sim.cues().begin(), sim.cues().end());
	}
}

void tap (Sim& sim, Key key, std::set<std::string>* heard = nullptr) {
	sim.step(std::vector<Event>{{key, true}, {key, false}});
	if (heard != nullptr) {
		heard->insert(sim.cues().begin(), sim.cues().end());
	}
}

// A board one clean single away: the bottom row full but for the spawn
// column region where a vertical I lands.
Board welled (int depth) {
	Board board;
	for (int y = kHeight - depth; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (x != kSpawnX + 1) {
				board.set(x, y, S);
			}
		}
	}
	return board;
}

// A floor of garbage `depth` rows deep with the right-hand four columns
// open: a flat I laid in the gutter digs exactly one row out.
Board rubbled (int depth) {
	Board board;
	for (int y = kHeight - depth; y < kHeight; ++y) {
		for (int x = 0; x + 4 < kWidth; ++x) {
			board.set(x, y, GARBAGE);
		}
	}
	return board;
}

void clear_a_row (Sim& sim) {
	wait_spawn(sim);
	for (int step = 0; step < kWidth; ++step) {
		tap(sim, Key::Right);
	}
	tap(sim, Key::Hard);
	for (int settle = 0; settle < 30; ++settle) {
		sim.step(std::nullopt);
	}
}

} // namespace

int main () {
	// What the game actually ships, on purpose and in one place.
	//
	// These used to be read off SimConfig's defaults by five checks that
	// were really testing the arithmetic, so retuning the wick for a new
	// player broke all five and none of them said why. The arithmetic has
	// its own ground in fused() now; this is the tuning, and it is a
	// decision: the fuse a first game meets is long, the shave is gentle,
	// and the floor is somewhere a human can still place a piece.
	{
		const SimConfig ships;
		const double at_ten = ships.fuse_base - 10 * ships.fuse_decay;
		check("the wick ships long, for a first game",
			ships.fuse_base == 5.0 && ships.fuse_decay == 0.10
				&& ships.fuse_min == 1.2,
			number(ships.fuse_base) + " / " + number(ships.fuse_decay)
				+ " / " + number(ships.fuse_min));
		check("and a hundred lines in it is still over three seconds",
			at_ten > 3.0 && ships.fuse_min > 1.0, number(at_ten));
	}

	// The schedule: base at level zero, shaved per level, floored.
	{
		SimConfig config = fused();
		Sim fresh(config, bags());
		wait_spawn(fresh);
		check("level zero burns the base fuse", fresh.fuse_total() == 3.0,
			std::to_string(fresh.fuse_total()));

		config.start_lines = 100;   // Level ten.
		Sim squeezed(config, bags());
		wait_spawn(squeezed);
		check("level ten shaves the schedule",
			squeezed.fuse_total() == 3.0 - 10 * 0.15,
			std::to_string(squeezed.fuse_total()));

		config.start_lines = 1000;  // Far past the floor.
		Sim floored(config, bags());
		wait_spawn(floored);
		check("the schedule never sinks past its floor",
			floored.fuse_total() == 0.8, std::to_string(floored.fuse_total()));
	}

	// The burn: a piece left alone is slammed down when the fuse runs out,
	// the warning cue sounding on the way; the forced drop bleeds Flow.
	{
		SimConfig config = fused();
		config.fuse_base = 0.4;    // Twenty frames.
		config.fuse_min = 0.4;
		Sim sim(config, bags());
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		run_out(sim, heard);
		check("the fuse forces the drop", !sim.locked().empty()
			&& sim.locked().back().forced);
		check("the warning sounded first", heard.count("fusewarn") == 1);
		check("a burned fuse leaves the gauge empty", sim.flow() == 0.);
	}

	// Flow charges on quality: a clearless lock, however fast, earns only
	// the small Flash bonus; a single clear adds its line's worth; and a
	// quad perfect clear pours it in - lines plus attack.
	{
		SimConfig config = fused();
		config.clear_delay = false;
		Sim sim(config, bags());
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		tap(sim, Key::Hard, &heard);
		check("a clearless flash lock earns only the flash bonus",
			sim.flow() == 4., std::to_string(sim.flow()));
		check("and the flash cue fired", heard.count("flash") == 1);

		Sim single(config, bags());
		wait_spawn(single);
		single.seed(welled(1));
		tap(single, Key::Cw);
		tap(single, Key::Hard);
		// Flash 4 at the lock, then line gain 2 for the single, attack 0.
		check("a single clear adds its line's worth",
			single.flow() == 6., std::to_string(single.flow()));

		Sim quad(config, std::vector<int>(40, I));
		wait_spawn(quad);
		quad.seed(welled(4));
		tap(quad, Key::Cw);
		tap(quad, Key::Hard);
		// Flash 4, lines 4x2, and the quad's perfect clear attack (14) at
		// 4 apiece: quality is what fills the gauge.
		check("a quad perfect clear pours it in",
			quad.flow() == 4. + 8. + 56., std::to_string(quad.flow()));
	}

	// The bank: at level zero a clear banks refuel but the schedule is
	// already the base, so nothing is drawn; at level ten the same bank
	// tops the next fuse up - by no more than the draw cap. The hard tap
	// itself locks the piece; waiting for the next spawn walks through the
	// clear's resolution, and that next piece is the one measured.
	{
		SimConfig config = fused();
		for (const int start : {0, 100}) {
			config.start_lines = start;
			Sim sim(config, bags());
			std::set<std::string> heard;
			wait_spawn(sim, &heard);
			sim.seed(welled(1));
			tap(sim, Key::Cw);
			tap(sim, Key::Hard);
			wait_spawn(sim, &heard);
			// A vertical I in the well clears one line, no attack: the bank
			// gains refuel_line * 1 = 0.4 seconds.
			if (start == 0) {
				check("level zero: refuel banks but cannot draw",
					sim.fuse_total() == 3.0 && sim.fuse_bank() > 0.,
					std::to_string(sim.fuse_total()) + " "
						+ std::to_string(sim.fuse_bank()));
			} else {
				const double schedule = 3.0 - 10 * 0.15;
				check("level ten: the bank tops the next fuse up",
					sim.fuse_total() > schedule
						&& sim.fuse_total() <= schedule + 1.0,
					std::to_string(sim.fuse_total()));
			}
		}
	}

	// The hold: under the fuse the clock rides through the swap.
	{
		Sim sim(fused(), bags());
		wait_spawn(sim);
		tap(sim, Key::Hold);       // First hold: next piece, fresh fuse.
		for (int i = 0; i < 10; ++i) {
			sim.step(std::nullopt);
		}
		const double before = sim.piece_elapsed().value_or(-1.);
		tap(sim, Key::Hold);       // The swap.
		check("a hold does not reset the fuse clock",
			sim.piece_elapsed().value_or(-1.) >= before && before > 0.,
			std::to_string(before));
	}
	{
		SimConfig config = fused();
		config.fuse = false;
		config.forced_delay = 5.;
		Sim sim(config, bags());
		wait_spawn(sim);
		tap(sim, Key::Hold);
		for (int i = 0; i < 10; ++i) {
			sim.step(std::nullopt);
		}
		tap(sim, Key::Hold);
		check("without the fuse the swap still resets it",
			sim.piece_elapsed().value_or(-1.) < 0.1,
			std::to_string(sim.piece_elapsed().value_or(-1.)));
	}

	// Overdrive: a cranked line gain lets one single ignite it; the fuse
	// freezes while it burns, attack is multiplied, and when it ends the
	// gauge starts over.
	{
		SimConfig config = fused();
		config.flow_gain_line = 120.;
		config.overdrive_secs = 0.5;   // 25 frames.
		config.clear_delay = false;    // The quad must resolve inside it.
		Sim sim(config, std::vector<int>(40, I));   // An all-I deck: quads.
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		sim.seed(welled(1));
		tap(sim, Key::Cw, &heard);
		tap(sim, Key::Hard, &heard);
		check("a full gauge ignites overdrive",
			sim.overdrive() && heard.count("overdrive") == 1);
		wait_spawn(sim, &heard);
		const double frozen = sim.piece_elapsed().value_or(-1.);
		for (int i = 0; i < 5; ++i) {
			sim.step(std::nullopt);
		}
		check("the fuse is frozen while overdrive burns",
			sim.overdrive()
				&& sim.piece_elapsed().value_or(-1.) == frozen);
		// A quad sent during overdrive carries the multiplier. The seeded
		// well is the whole board, so the quad is also a perfect clear:
		// four for the quad plus ten for the PC, then times 1.5 is 21.
		// With the clear delay off it resolves on the lock frame, well
		// inside the burn.
		sim.seed(welled(4));
		tap(sim, Key::Cw, &heard);
		tap(sim, Key::Hard, &heard);
		check("overdrive multiplies the attack",
			sim.locked().back().scored && sim.locked().back().attack == 21,
			std::to_string(sim.locked().back().attack));
		// And when a second and a third multiplier are on, they ADD to
		// that one instead of multiplying it. The same quad-plus-perfect
		// worth fourteen, with Overdrive up, a hand at 1.5 and a crit
		// landing: one and a half plus a half plus one is three, so forty
		// two. Composed the old way it was fourteen times 1.5 times 1.5
		// times two - sixty four - and that runaway is why a plain double
		// worth one could leave a board as twelve.
		{
			SimConfig stacked = config;
			stacked.attack_scale = 1.5;
			stacked.crit_every = 1;
			Sim piled(stacked, std::vector<int>(40, I));
			std::set<std::string> also;
			wait_spawn(piled, &also);
			piled.seed(welled(1));
			tap(piled, Key::Cw, &also);
			tap(piled, Key::Hard, &also);
			wait_spawn(piled, &also);
			piled.seed(welled(4));
			tap(piled, Key::Cw, &also);
			tap(piled, Key::Hard, &also);
			check("and a second multiplier adds to it rather than "
					"multiplying it",
				piled.overdrive() && piled.locked().back().attack == 42,
				std::to_string(piled.locked().back().attack));
		}
		for (int i = 0; i < 40 && sim.overdrive(); ++i) {
			sim.step(std::nullopt);
			heard.insert(sim.cues().begin(), sim.cues().end());
		}
		check("overdrive ends and the gauge starts over",
			!sim.overdrive() && sim.flow() == 0.
				&& heard.count("overdrive_end") == 1);
	}

	// The Draught lowers the bar the gauge has to reach. The same clear that
	// leaves the gauge short of a hundred lights Overdrive once the bar
	// comes down - and it is a lower bar, not a standing light: the gauge
	// still empties when the burn guts out, and more charge inside the burn
	// never re-ignites it.
	{
		SimConfig high = fused();
		high.flow_gain_line = 70.;      // One line: short of a hundred.
		high.flow_gain_attack = 0.;     // Lines only, so the sum is plain.
		high.clear_delay = false;
		Sim sim(high, std::vector<int>(40, I));
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		sim.seed(welled(1));
		tap(sim, Key::Cw, &heard);
		tap(sim, Key::Hard, &heard);
		check("a gauge short of the bar lights nothing",
			!sim.overdrive() && heard.count("overdrive") == 0
				&& sim.flow() > 0.,
			std::to_string(sim.flow()));

		SimConfig low = high;
		low.flow_ignite = 60.;
		low.overdrive_secs = 0.5;       // 25 frames.
		Sim drawn(low, std::vector<int>(40, I));
		std::set<std::string> drawn_heard;
		wait_spawn(drawn, &drawn_heard);
		drawn.seed(welled(1));
		tap(drawn, Key::Cw, &drawn_heard);
		tap(drawn, Key::Hard, &drawn_heard);
		check("and the same clear lights it once the bar comes down",
			drawn.overdrive() && drawn_heard.count("overdrive") == 1);
		// More charge inside the burn must not light a second one: the
		// gate is the frame counter, not the gauge.
		wait_spawn(drawn, &drawn_heard);
		drawn.seed(welled(1));
		tap(drawn, Key::Cw, &drawn_heard);
		tap(drawn, Key::Hard, &drawn_heard);
		check("a burning forge never re-lights",
			drawn_heard.count("overdrive") == 1);
		for (int i = 0; i < 60 && drawn.overdrive(); ++i) {
			drawn.step(std::nullopt);
			drawn_heard.insert(drawn.cues().begin(), drawn.cues().end());
		}
		check("and a drawn light still empties the gauge when it guts out",
			!drawn.overdrive() && drawn.flow() == 0.);
	}

	// --- The gauge's supply. ------------------------------------------------
	// Seven tunings that feed, hold, cap and spill the gauge. Every one is
	// inert at its default - which is what lets the Python-graded suites
	// stay still - so each block here proves the default does nothing AND
	// that the tuning does its one thing.
	{
		// Digging charges the gauge, where a build asked for it. A rail
		// with no clock at all, because that is where the road lives.
		SimConfig plain = fused();
		plain.fuse = false;
		plain.flow_rail = true;
		plain.flow_gain_line = 0.;    // Lines pay nothing: the dig is the
		plain.flow_gain_attack = 0.;  // only faucet under test.
		plain.clear_delay = false;
		Sim quiet(plain, std::vector<int>(40, I));
		quiet.seed(rubbled(4));
		wait_spawn(quiet);
		clear_a_row(quiet);
		check("rubble dug out charges nothing by default",
			quiet.flow() == 0., number(quiet.flow()));

		SimConfig dug = plain;
		dug.flow_gain_dig = 5.;
		Sim digger(dug, std::vector<int>(40, I));
		digger.seed(rubbled(4));
		wait_spawn(digger);
		clear_a_row(digger);
		check("and charges it once a build opens that faucet",
			digger.flow() > 0., number(digger.flow()));
	}
	{
		// Taking a blow stokes the fire - counted on what actually landed,
		// so a ward that thinned the blow thinned this too.
		SimConfig plain = fused();
		plain.fuse = false;
		plain.flow_rail = true;
		plain.gametype = 5;
		Sim quiet(plain, std::vector<int>(8, I));
		quiet.receive_attack(4);
		check("a blow charges nothing by default", quiet.flow() == 0.);

		SimConfig hit = plain;
		hit.flow_gain_taken = 4.;
		Sim struck(hit, std::vector<int>(8, I));
		struck.receive_attack(4);
		check("and stokes the fire once a build asks",
			std::abs(struck.flow() - 16.) < 1e-9, number(struck.flow()));

		SimConfig warded = hit;
		warded.garbage_scale = 0.5;
		Sim guarded(warded, std::vector<int>(8, I));
		guarded.receive_attack(4);
		check("a thinner blow stokes a smaller fire",
			guarded.flow() < struck.flow() && guarded.flow() > 0.,
			number(guarded.flow()));
	}
	{
		// The ceiling, and the floor under it: however a build stacks, a
		// hundred is always reachable, so Overdrive can always light.
		SimConfig deep = fused();
		deep.fuse = false;
		deep.flow_rail = true;
		deep.gametype = 5;
		deep.flow_gain_taken = 50.;   // The blow is the only faucet here.
		deep.flow_ignite = 1000.;     // Never light; the gauge just fills.
		deep.flow_cap = 160.;
		Sim sim(deep, std::vector<int>(8, I));
		sim.receive_attack(8);
		check("the gauge fills to the ceiling a build bought",
			sim.flow() <= 160. + 1e-9 && sim.flow() > 100.,
			number(sim.flow()));

		SimConfig shallow = deep;
		shallow.flow_cap = 10.;     // Nonsense; the floor must refuse it.
		Sim floored(shallow, std::vector<int>(8, I));
		floored.receive_attack(8);
		check("and a nonsense ceiling never puts ignition out of reach",
			floored.flow() >= 100. - 1e-9, number(floored.flow()));
	}
	{
		// What the gauge keeps when the fire guts out. Zero by default -
		// the gauge starts over - and never the whole of it, or a build
		// would own a fire that relights itself.
		const auto after_burn = [] (double keep) {
			SimConfig config = fused();
			config.fuse = false;
			config.flow_rail = true;
			config.flow_gain_line = 200.;
			config.overdrive_secs = 0.2;   // Ten frames.
			config.clear_delay = false;
			Sim sim(config, std::vector<int>(40, I));
			config.flow_keep = keep;
			sim.retune(config);
			sim.seed(welled(1));
			wait_spawn(sim);
			tap(sim, Key::Cw);
			tap(sim, Key::Hard);
			for (int i = 0; i < 40; ++i) {
				sim.step(std::nullopt);
			}
			return sim.flow();
		};
		check("the gauge starts over when the fire guts out",
			after_burn(0.) == 0., number(after_burn(0.)));
		check("or keeps the share a build bought it",
			after_burn(0.5) > 0., number(after_burn(0.5)));
		check("and never keeps the whole of it",
			after_burn(1.0) < 100., number(after_burn(1.0)));
	}
	{
		// A fire that can be fed while it burns - up to one full burn and
		// never past it, so the reward is keeping it alive, not owning it.
		SimConfig config = fused();
		config.fuse = false;
		config.flow_rail = true;
		config.flow_gain_line = 200.;
		config.overdrive_secs = 2.;
		config.overdrive_refill = 0.5;
		config.clear_delay = false;
		Sim sim(config, std::vector<int>(40, I));
		sim.seed(welled(1));
		wait_spawn(sim);
		tap(sim, Key::Cw);
		tap(sim, Key::Hard);
		const long lit = sim.overdrive_left();
		for (int i = 0; i < 30; ++i) {
			sim.step(std::nullopt);
		}
		const long spent = sim.overdrive_left();
		sim.seed(welled(1));
		wait_spawn(sim);
		tap(sim, Key::Cw);
		tap(sim, Key::Hard);
		check("a clear feeds the fire it is made inside",
			sim.overdrive_left() > spent, std::to_string(spent) + " -> "
				+ std::to_string(sim.overdrive_left()));
		check("and never past one whole burn",
			sim.overdrive_left() <= lit, std::to_string(lit) + " vs "
				+ std::to_string(sim.overdrive_left()));
	}
	{
		// The flood spills the gauge: what rises on you costs the fire you
		// were banking, charged once for the wave.
		SimConfig config = fused();
		config.fuse = false;
		config.flow_rail = true;
		config.gametype = 5;
		config.flow_flood_loss = 20.;
		config.flow_gain_taken = 30.;
		config.flow_ignite = 1000.;   // Never light; the spill is the test.
		Sim sim(config, std::vector<int>(20, I));
		sim.feed_garbage(1 << 3);
		sim.feed_garbage(1 << 3);
		sim.receive_attack(2);
		const double banked = sim.flow();
		wait_spawn(sim);
		tap(sim, Key::Hard);
		for (int i = 0; i < 40; ++i) {
			sim.step(std::nullopt);
		}
		check("the flood spills the gauge",
			sim.flow() < banked, number(banked) + " -> "
				+ number(sim.flow()));
	}
	{
		// Heat pressure without a clock: the other board's Overdrive
		// smothers this one's supply, so igniting stays an attack in a
		// duel that has no wick to lean on.
		const auto charged = [] (bool pressured) {
			SimConfig config = fused();
			config.fuse = false;
			config.flow_rail = true;
			config.flow_gain_line = 40.;
			config.clear_delay = false;
			Sim sim(config, std::vector<int>(40, I));
			sim.set_pressure(pressured);
			sim.seed(welled(1));
			wait_spawn(sim);
			tap(sim, Key::Cw);
			tap(sim, Key::Hard);
			return sim.flow();
		};
		check("their fire smothers your supply, clock or no clock",
			charged(true) < charged(false) && charged(true) > 0.,
			number(charged(true)) + " vs " + number(charged(false)));
	}
	{
		// The clean flash: with no clock to be fast against, the bonus
		// goes to the placement that wasted the fewest presses - and only
		// where a card asked for it.
		const auto flashes = [] (int slack, int wasted) {
			SimConfig config = fused();
			config.fuse = false;
			config.flow_rail = true;
			config.flash_finesse = slack;
			config.flow_flash_gain = 30.;
			config.clear_delay = false;
			Sim sim(config, std::vector<int>(20, I));
			wait_spawn(sim);
			for (int i = 0; i < wasted; ++i) {
				tap(sim, Key::Left);
				tap(sim, Key::Right);
			}
			tap(sim, Key::Hard);
			for (int i = 0; i < 10; ++i) {
				sim.step(std::nullopt);
			}
			return sim.flow();
		};
		check("a clean placement flashes where a build asked",
			flashes(2, 0) > 0., number(flashes(2, 0)));
		check("a wasteful one does not",
			flashes(2, 4) == 0., number(flashes(2, 4)));
		check("and nothing flashes without the card",
			flashes(0, 0) == 0., number(flashes(0, 0)));
	}

	// The rail without the fuse: the reward half of the ruleset standing on
	// its own, which is how every room off the Forge Road plays. Quality
	// still charges the gauge, a full gauge still ignites, and Overdrive
	// still multiplies the blow - but nothing burns, so no piece is ever
	// slammed down and the Flash bonus (a share of a fuse that is not
	// there) never pays.
	{
		SimConfig config = fused();
		config.fuse = false;
		config.flow_rail = true;
		config.flow_gain_line = 120.;
		config.overdrive_secs = 0.5;   // 25 frames.
		config.clear_delay = false;
		Sim sim(config, std::vector<int>(40, I));
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		sim.seed(welled(1));
		tap(sim, Key::Cw, &heard);
		tap(sim, Key::Hard, &heard);
		check("the gauge charges with no fuse anywhere",
			sim.flow() > 0. && sim.overdrive()
				&& heard.count("overdrive") == 1);
		check("and no fuse means no flash bonus and no forced drop",
			heard.count("flash") == 0 && heard.count("forced") == 0
				&& heard.count("fusewarn") == 0);
		wait_spawn(sim, &heard);
		// The same quad-into-a-perfect-clear the fused block measures: four
		// for the quad plus ten for the PC, times the 1.5 multiplier.
		sim.seed(welled(4));
		tap(sim, Key::Cw, &heard);
		tap(sim, Key::Hard, &heard);
		check("overdrive multiplies the attack without a fuse",
			sim.locked().back().attack == 21,
			std::to_string(sim.locked().back().attack));
		// The piece clock never advances, so a long-held piece is never
		// taken away from the player.
		wait_spawn(sim, &heard);
		for (int i = 0; i < 300; ++i) {
			sim.step(std::nullopt);
		}
		check("a piece is never slammed down on a pure board",
			sim.piece_elapsed().value_or(-1.) == 0.,
			std::to_string(sim.piece_elapsed().value_or(-1.)));
	}

	// And with neither flag the gauge is dead metal: the graded engine.
	{
		SimConfig config = fused();
		config.fuse = false;
		config.flow_rail = false;
		config.flow_gain_line = 120.;
		Sim sim(config, std::vector<int>(40, I));
		std::set<std::string> heard;
		wait_spawn(sim, &heard);
		sim.seed(welled(1));
		tap(sim, Key::Cw, &heard);
		tap(sim, Key::Hard, &heard);
		check("with both flags down nothing charges",
			sim.flow() == 0. && !sim.overdrive()
				&& heard.count("overdrive") == 0);
	}

	// The record: a fuse-rules replay carries every tunable through a save
	// and load, and a file without the keys reads back as trainer rules.
	{
		replay::Replay game;
		game.meta.played = "2026-08-24T12:00:00";
		game.meta.fuse = true;
		game.meta.fuse_base = 3.0;
		game.meta.fuse_min = 0.8;
		game.meta.fuse_decay = 0.15;
		game.meta.fuse_bank_cap = 6.0;
		game.meta.fuse_draw_cap = 1.0;
		game.meta.fuse_refuel_line = 0.4;
		game.meta.fuse_refuel_attack = 0.5;
		game.meta.flash_frac = 0.30;
		game.meta.flash_floor = 0.25;
		game.meta.flow_lock_gain = 8.;
		game.meta.flow_flash_gain = 12.;
		game.meta.flow_burn_loss = 18.;
		game.meta.overdrive_secs = 8.;
		game.meta.overdrive_mult = 1.5;
		replay::Placement one;
		one.form = I;
		one.presses = {"HARD"};
		game.placements.push_back(one);
		check("the fuse replay saves", replay::save(game, "fusecheck-replays"));
		const auto back = replay::load(game.path);
		check("and loads with its ruleset whole", back.has_value()
			&& back->meta.fuse
			&& back->meta.fuse_base == 3.0
			&& back->meta.fuse_decay == 0.15
			&& back->meta.flash_floor == 0.25
			&& back->meta.overdrive_mult == 1.5);
		replay::Replay plain;
		plain.meta.played = "2026-08-24T12:00:01";
		plain.placements.push_back(one);
		check("a trainer replay saves without fuse keys",
			replay::save(plain, "fusecheck-replays"));
		const auto legacy = replay::load(plain.path);
		check("and reads back as trainer rules",
			legacy.has_value() && !legacy->meta.fuse);
	}

	// The backdraft: a clear resolved inside Overdrive burns the bottom
	// garbage row off this board too - dig counted, nothing paid - and
	// outside Overdrive the same clear burns nothing.
	{
		const auto dig_board = [] {
			Board board;
			for (int x = 0; x < kWidth; ++x) {
				if (x != kSpawnX + 1) {
					board.set(x, kHeight - 3, S);
				}
			}
			for (const int y : {kHeight - 2, kHeight - 1}) {
				for (int x = 0; x < kWidth; ++x) {
					if (x != 0) {
						board.set(x, y, GARBAGE);
					}
				}
			}
			return board;
		};
		SimConfig config = fused();
		config.clear_delay = false;
		for (const bool ignited : {true, false}) {
			SimConfig run = config;
			if (ignited) {
				run.flow_gain_line = 120.;   // The single itself ignites.
			}
			Sim sim(run, std::vector<int>(40, I));
			std::set<std::string> heard;
			wait_spawn(sim, &heard);
			sim.seed(dig_board());
			tap(sim, Key::Cw, &heard);
			tap(sim, Key::Hard, &heard);
			if (ignited) {
				check("overdrive's clear burns the floor's garbage",
					sim.board().garbage_rows() == 1
						&& sim.downstack() == 1
						&& heard.count("burn") == 1,
					std::to_string(sim.board().garbage_rows()));
			} else {
				check("outside overdrive nothing burns",
					sim.board().garbage_rows() == 2
						&& heard.count("burn") == 0,
					std::to_string(sim.board().garbage_rows()));
			}
		}
	}

	// The variant's own score file: a duel entry round-trips through
	// fusescore.dat, the trainer's hiscore.dat is never created, and a
	// name from neither ruleset is refused.
	{
		const std::string folder = "fusecheck-scores";
		std::filesystem::remove_all(folder);
		hiscore::Entry entry;
		const char* name = "Fusebox ";
		std::copy(name, name + 8, entry.name.begin());
		entry.score = 4200;
		entry.lines = 17;
		entry.timer = 9000;
		check("a variant score submits", hiscore::submit_fuse(folder, "duel", entry));
		const hiscore::FuseTables tables = hiscore::load_fuse(folder);
		check("and tops its own table",
			hiscore::shown_name(tables[5][0]) == "Fusebox"
				&& tables[5][0].score == 4200);
		check("the trainer file was never touched",
			!std::filesystem::exists(
				std::filesystem::path(folder) / "hiscore.dat"));
		check("an unknown name is refused",
			!hiscore::submit_fuse(folder, "free", entry)
				&& hiscore::place_fuse(tables, "free", entry)
					== hiscore::kPerTable);
	}

	// Heat pressure: the other board's Overdrive makes this fuse burn
	// faster - the same piece is slammed sooner under it, releasing the
	// pressure restores the pace, and with the flag off nothing changes.
	{
		SimConfig config = fused();
		config.fuse_base = 1.0;   // Fifty frames unpressured.
		config.fuse_min = 1.0;
		long plain_frames = 0;
		long pressed_frames = 0;
		for (const bool pressed : {false, true}) {
			Sim sim(config, bags());
			wait_spawn(sim);
			sim.set_pressure(pressed);
			const long from = sim.frame();
			std::set<std::string> heard;
			run_out(sim, heard);
			(pressed ? pressed_frames : plain_frames)
				= sim.locked().back().frame - from;
		}
		check("pressure burns the fuse faster",
			pressed_frames < plain_frames && pressed_frames > 0,
			std::to_string(pressed_frames) + " vs "
				+ std::to_string(plain_frames));
		Sim relieved(config, bags());
		wait_spawn(relieved);
		relieved.set_pressure(true);
		relieved.set_pressure(false);
		const long from = relieved.frame();
		std::set<std::string> heard;
		run_out(relieved, heard);
		check("releasing it restores the pace",
			relieved.locked().back().frame - from == plain_frames);
	}

	// Flags off: the ruleset does not exist.
	{
		SimConfig config;
		config.forced_delay = 0.;
		config.finesse_rule = 0;
		Sim sim(config, bags());
		wait_spawn(sim);
		tap(sim, Key::Hard);
		check("with the flag off nothing stirs",
			sim.fuse_total() == 0. && sim.flow() == 0. && !sim.overdrive());
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
