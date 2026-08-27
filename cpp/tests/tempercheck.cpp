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
		std::mt19937 rng(5u);
		check("a table with nothing acceptable returns no pick",
			temper::bot_pick({"collapse"}, 6, rng) == -1
				&& temper::bot_pick({}, 6, rng) == -1);

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
		check("the rules it finished under are not the ones it started with",
			built.fuse_base != start.fuse_base
				|| built.fuse_refuel_line != start.fuse_refuel_line
				|| built.overdrive_secs != start.overdrive_secs
				|| built.flow_gain_line != start.flow_gain_line);
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
