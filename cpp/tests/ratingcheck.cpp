// The rating estimate's arithmetic, pinned: the official TR conversion on
// known points, monotonicity in every input, the ranks in ladder order, and
// the edges - an empty run, a zero VS, an off-the-curve monster.
#include <cmath>
#include <cstdio>
#include <string>

#include "forcetris/rating.hpp"

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

} // namespace

int main () {
	// The TR formula on its fixed points: 1500 rating is exactly the middle
	// of the scale whatever the deviation, and the scale's ends are asymptotes.
	check("1500 glicko is 12500 TR",
		std::fabs(rating::tr_for(1500., 60.) - 12500.) < 1e-6,
		std::to_string(rating::tr_for(1500., 60.)));
	check("the scale tops out under 25000",
		rating::tr_for(4000., 60.) < 25000. && rating::tr_for(4000., 60.) > 24500.);
	check("and bottoms out over 0",
		rating::tr_for(0., 60.) > 0. && rating::tr_for(0., 60.) < 700.);
	check("a wider deviation squeezes TR toward the middle",
		rating::tr_for(2500., 200.) < rating::tr_for(2500., 60.)
		&& rating::tr_for(500., 200.) > rating::tr_for(500., 60.));

	// The curve rises with every input, nowhere falls.
	double last = -1e18;
	bool rising = true;
	for (double vs = 0.; vs <= 400.; vs += 5.) {
		const double glicko = rating::glicko_for(vs);
		if (glicko < last) {
			rising = false;
		}
		last = glicko;
	}
	check("estimated glicko never falls as VS rises", rising);
	check("the curve is clamped at both ends",
		rating::glicko_for(0.) < rating::glicko_for(342.58)
		&& rating::glicko_for(10000.) < rating::glicko_from_tr(25000., 60.));

	// The curve is calibrated on TETR.IO's own real per-rank averages
	// (APM, PPS, VS and the rank's actual TR, taken off the live
	// leaderboard breakdown) rather than an invented shape, so a run at
	// exactly a rank's real average VS has to land back on that rank and
	// that TR - not just somewhere in its neighbourhood.
	struct Real { double vs; double tr; const char* rank; };
	const Real reals[] = {
		{14.09, 0., "D"}, {19.79, 430.11, "D+"}, {22.31, 1032.46, "C-"},
		{26.93, 1914.17, "C"}, {31.35, 2904.97, "C+"}, {35.61, 3973.10, "B-"},
		{40.85, 5450.39, "B"}, {46.53, 7124.54, "B+"}, {53.35, 8816.94, "A-"},
		{61.04, 10415.09, "A"}, {71.29, 12061.27, "A+"}, {83.98, 13873.06, "S-"},
		{98.81, 15310.48, "S"}, {118.76, 16738.00, "S+"}, {149.78, 18426.53, "SS"},
		{199.49, 20521.20, "U"}, {268.89, 22792.44, "X"}, {342.58, 24062.15, "X+"},
	};
	bool anchors_exact = true;
	for (const Real& r : reals) {
		const rating::Estimate got = rating::estimate(0., 0., r.vs);
		if (std::fabs(got.tr - r.tr) > 1e-6 || std::string(got.rank) != r.rank) {
			anchors_exact = false;
			check("real anchor holds", false, std::string(r.rank) + ": wanted TR "
				+ std::to_string(r.tr) + " rank " + r.rank + ", got TR "
				+ std::to_string(got.tr) + " rank " + got.rank);
		}
	}
	check("every real rank average interpolates back to its own TR and rank",
		anchors_exact);

	// The user-reported regression: a B+ player's real average (21.66 APM,
	// 1.12 PPS, 46.53 VS) must not read out as A - the fabricated curve
	// this replaced put it near 11000 TR / A.
	const rating::Estimate bplus = rating::estimate(21.66, 1.12, 46.53);
	check("a real B+ average rates as B+, not A",
		std::string(bplus.rank) == "B+", bplus.rank);
	check("and its TR sits near the real B+ figure, not inflated toward A",
		bplus.tr > 6500. && bplus.tr < 7800.,
		std::to_string(bplus.tr));

	// The ranks come out in ladder order as TR climbs.
	const char* order[] = {"D", "D+", "C-", "C", "C+", "B-", "B", "B+",
		"A-", "A", "A+", "S-", "S", "S+", "SS", "U", "X", "X+"};
	int at = 0;
	bool ordered = true;
	for (double tr = 0.; tr <= 25000.; tr += 10.) {
		const std::string rank = rating::rank_for(tr);
		while (at < 17 && rank == order[at + 1]) {
			++at;
		}
		if (rank != order[at]) {
			ordered = false;
			break;
		}
	}
	check("ranks climb the ladder in order", ordered && at == 17,
		"stopped at " + std::string(order[at]));

	// The whole estimate: an empty run says nothing, a plausible S-rank run
	// lands near the S band, attack alone stands in for a missing VS.
	check("an empty run has no rank",
		std::string(rating::estimate(0., 0., 0.).rank).empty());
	const rating::Estimate mid = rating::estimate(60., 1.5, 105.);
	check("a 105 VS run rates near the S band",
		std::string(mid.rank).substr(0, 1) == "S"
		|| std::string(mid.rank) == "A+", mid.rank);
	const rating::Estimate blind = rating::estimate(60., 1.5, 0.);
	check("no VS leans on APM instead", blind.glicko > 1000., "");
	check("its TR sits inside the scale", blind.tr > 0. && blind.tr < 25000.);

	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
