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
	double last = -1.;
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
		rating::glicko_for(0.) >= 400. && rating::glicko_for(10000.) <= 3200.);

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
