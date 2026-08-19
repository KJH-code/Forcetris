#include "forcetris/rating.hpp"

#include <algorithm>
#include <cmath>

namespace forcetris {
namespace rating {

namespace {

// The curve: a strength value (VS-shaped) against the TR a Tetra League
// player of that strength actually holds. These are not guesses - they are
// the real per-rank averages TETR.IO's own leaderboard reports (APM, PPS,
// VS and the rank's actual TR), so the estimate is anchored to the
// population it is trying to place a run against rather than to a shape
// that merely looked plausible.
struct Anchor {
	double strength;
	double tr;
	const char* rank;
};

constexpr Anchor kCurve[] = {
	{14.09, 0., "D"},
	{19.79, 430.11, "D+"},
	{22.31, 1032.46, "C-"},
	{26.93, 1914.17, "C"},
	{31.35, 2904.97, "C+"},
	{35.61, 3973.10, "B-"},
	{40.85, 5450.39, "B"},
	{46.53, 7124.54, "B+"},
	{53.35, 8816.94, "A-"},
	{61.04, 10415.09, "A"},
	{71.29, 12061.27, "A+"},
	{83.98, 13873.06, "S-"},
	{98.81, 15310.48, "S"},
	{118.76, 16738.00, "S+"},
	{149.78, 18426.53, "SS"},
	{199.49, 20521.20, "U"},
	{268.89, 22792.44, "X"},
	{342.58, 24062.15, "X+"},
};
constexpr int kCurveCount = sizeof kCurve / sizeof kCurve[0];

// The TR a given strength interpolates to: piecewise-linear between the
// real anchors, extrapolated by the end segments' own slope past either
// tip and clamped to the scale.
double tr_for_strength (double strength) {
	if (strength <= kCurve[0].strength) {
		const double slope = (kCurve[1].tr - kCurve[0].tr)
			/ (kCurve[1].strength - kCurve[0].strength);
		return std::max(0.,
			kCurve[0].tr + slope * (strength - kCurve[0].strength));
	}
	for (int i = 1; i < kCurveCount; ++i) {
		if (strength <= kCurve[i].strength) {
			const double span = kCurve[i].strength - kCurve[i - 1].strength;
			const double part = (strength - kCurve[i - 1].strength) / span;
			return kCurve[i - 1].tr + (kCurve[i].tr - kCurve[i - 1].tr) * part;
		}
	}
	const int last = kCurveCount - 1;
	const double slope = (kCurve[last].tr - kCurve[last - 1].tr)
		/ (kCurve[last].strength - kCurve[last - 1].strength);
	return std::min(24999.,
		kCurve[last].tr + slope * (strength - kCurve[last].strength));
}

} // namespace

double strength (double apm, double pps, double vs) {
	if (vs > 0.) {
		return vs;
	}
	// VS is attack plus downstack per second, scaled by a hundred; a run
	// that sent attack but dug nothing sits near 1.5x its APM, and a run
	// with neither leans on raw speed alone - slight, as it should be.
	if (apm > 0.) {
		return apm * 1.5;
	}
	return pps * 10.;
}

double glicko_for (double strength) {
	// The public API keeps returning "an estimated Glicko" - it is now
	// derived, not fitted: the strength interpolates to a real TR first,
	// and the official formula is inverted to find the Glicko that would
	// produce it, so the two never disagree with each other.
	return glicko_from_tr(tr_for_strength(strength), 60.);
}

double tr_for (double glicko, double rd) {
	const double pi = 3.14159265358979323846;
	const double ln10 = std::log(10.);
	const double x = ((1500. - glicko) * pi)
		/ std::sqrt(3. * ln10 * ln10 * rd * rd
			+ 2500. * (64. * pi * pi + 147. * ln10 * ln10));
	return 25000. / (1. + std::pow(10., x));
}

double glicko_from_tr (double tr, double rd) {
	// tr_for is monotonically increasing in glicko with no closed-form
	// inverse worth deriving by hand, so it is bisected instead. The
	// bracket comfortably holds the whole 0..25000 TR scale: tr_for(-3000)
	// and tr_for(6000) sit within a hundredth of a point of the scale's
	// two ends.
	double lo = -3000.;
	double hi = 6000.;
	for (int step = 0; step < 60; ++step) {
		const double mid = (lo + hi) / 2.;
		if (tr_for(mid, rd) < tr) {
			lo = mid;
		} else {
			hi = mid;
		}
	}
	return (lo + hi) / 2.;
}

const char* rank_for (double tr) {
	for (int i = kCurveCount - 1; i >= 0; --i) {
		if (tr >= kCurve[i].tr) {
			return kCurve[i].rank;
		}
	}
	return "D";
}

Estimate estimate (double apm, double pps, double vs) {
	Estimate out;
	if (apm <= 0. && pps <= 0. && vs <= 0.) {
		return out;
	}
	out.tr = tr_for_strength(strength(apm, pps, vs));
	out.glicko = glicko_from_tr(out.tr, 60.);
	out.rank = rank_for(out.tr);
	return out;
}

} // namespace rating
} // namespace forcetris
