// The munch, pinned on hand-built games: the clear buckets land where the
// muncher's rules put them, the attack-per-line split respects what was
// dug, the surge floor is four, the well rule wants four validated rows,
// and an empty game chews down to nothing.
#include <cstdio>
#include <string>
#include <vector>

#include "forcetris/board.hpp"
#include "forcetris/munch.hpp"
#include "forcetris/replay.hpp"

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

replay::Placement place (int form, int lines, const char* spin, int attack,
                         double elapsed, std::vector<std::string> rows,
                         bool perfect = false) {
	replay::Placement out;
	out.form = form;
	out.lines = lines;
	out.spin = spin;
	out.attack = attack;
	out.elapsed = elapsed;
	out.rows = std::move(rows);
	out.perfect = perfect;
	out.presses = {"LEFT", "HARD"};
	return out;
}

// A board that is one validated four-deep well at column 9: four complete
// rows but for the last column.
std::vector<std::string> welled_rows () {
	return {"111111111.", "111111111.", "111111111.", "111111111."};
}

// Two rows of garbage with a hole at column 3.
std::vector<std::string> cheese_rows () {
	return {"777.777777", "777.777777"};
}

} // namespace

int main () {
	// Nothing in, nothing out.
	{
		replay::Replay game;
		const munch::Stats stats = munch::crunch(game);
		check("an empty game chews to nothing", stats.groups.empty());
	}

	// The clear buckets: a quad, a TSD, a mini (all-spin), an S-spin
	// (all-spin), a plain double, a perfect clear.
	{
		replay::Replay game;
		game.placements.push_back(place(I, 4, "", 4, 1., welled_rows()));
		game.placements.push_back(place(T, 2, "T-SPIN", 4, 2., welled_rows()));
		game.placements.push_back(place(T, 1, "MINI T-SPIN", 0, 3., welled_rows()));
		game.placements.push_back(place(S, 2, "S-SPIN", 4, 4., welled_rows()));
		game.placements.push_back(place(J, 2, "", 1, 5., welled_rows()));
		game.placements.push_back(place(L, 1, "", 10, 6., {}, true));
		const munch::Stats stats = munch::crunch(game);
		check("the quad lands as a quad", stats.get("ct_quad") == 1.);
		check("the T-spin double as tsd", stats.get("ct_tsd") == 1.);
		check("the mini as an all-spin", stats.get("ct_allspin") == 2.,
			std::to_string(stats.get("ct_allspin")));
		check("the plain double as a double", stats.get("ct_double") == 1.);
		check("the perfect clear as a pc", stats.get("ct_pc") == 1.);
		check("t efficiency counts full T-spins only",
			stats.get("eff_t") == 50.);
		check("i efficiency is the quad", stats.get("eff_i") == 100.);
		// Six clears: quad, tsd, mini, s-spin, pc keep the chain (5 links),
		// the plain double breaks it once at length 4 - one surge.
		check("the chain of four surges once",
			stats.get("surge_rate") == 100.
			&& stats.get("surge_len") == 4.,
			std::to_string(stats.get("surge_len")));
		check("attack per line adds up",
			stats.get("apl") > 1.9 && stats.get("apl") < 2.,
			std::to_string(stats.get("apl")));
	}

	// Downstack accounting: garbage on the board, dug by the second
	// placement - its attack lands in the downstack APL, and the garbage
	// was cheese (a run under four), so in the cheese APL too.
	{
		replay::Replay game;
		game.placements.push_back(place(L, 0, "", 0, 1., cheese_rows()));
		game.placements.push_back(place(I, 1, "", 2, 2., {"777.777777"}));
		game.placements.push_back(place(J, 0, "", 0, 3., {"777.777777"}));
		const munch::Stats stats = munch::crunch(game);
		check("the dig's attack lands in downstack APL",
			stats.get("apl_ds") == 2.,
			std::to_string(stats.get("apl_ds")));
		check("and in cheese APL, the run being short",
			stats.get("apl_cheese") == 2.);
	}

	// The well rule: four validated rows count, three do not.
	{
		replay::Replay game;
		game.placements.push_back(place(L, 0, "", 0, 1., welled_rows()));
		const munch::Stats four = munch::crunch(game);
		check("a four-deep well is seen at column nine",
			four.get("well_col") == 9. && four.get("well_share") == 100.);
		replay::Replay shallow;
		shallow.placements.push_back(place(L, 0, "", 0, 1.,
			{"111111111.", "111111111.", "111111111."}));
		const munch::Stats three = munch::crunch(shallow);
		check("a three-deep dip is not a well",
			three.get("well_share") == 0.,
			std::to_string(three.get("well_share")));
	}

	// Pace: five pieces in one second read as five PPS in the mixture's
	// range, and keys per piece count the presses.
	{
		replay::Replay game;
		for (int i = 0; i < 30; ++i) {
			game.placements.push_back(
				place(J, 0, "", 0, 0.2 * (i + 1), welled_rows()));
		}
		const munch::Stats stats = munch::crunch(game);
		check("burst and plonk bracket a steady five",
			stats.get("burst_pps") > 4. && stats.get("burst_pps") < 6.
			&& stats.get("plonk_pps") > 4. && stats.get("plonk_pps") < 6.,
			std::to_string(stats.get("burst_pps")) + " "
				+ std::to_string(stats.get("plonk_pps")));
		check("keys per piece counts the presses",
			stats.get("kpp") == 2.);
	}

	// Every id in order() labels itself.
	{
		bool labelled = true;
		for (const char* id : munch::order()) {
			labelled = labelled && munch::label(id)[0] != '\0';
		}
		check("every stat id carries a label", labelled);
	}

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
