#include "forcetris/rating.hpp"

#include <algorithm>
#include <cmath>

namespace forcetris {
namespace rating {

namespace {

// The curve: a strength value (VS-shaped) against the Glicko a Tetra League
// player of that strength typically holds. The anchors are rounded community
// averages of league play at each rank - coarse on purpose, since the
// estimate cannot honestly be finer than the population data behind it.
struct Anchor {
	double strength;
	double glicko;
};

constexpr Anchor kCurve[] = {
	{15., 700.},     // D
	{30., 1100.},    // C
	{50., 1450.},    // B
	{75., 1750.},    // A
	{105., 2050.},   // S
	{150., 2350.},   // SS
	{200., 2650.},   // U
	{260., 2900.},   // X
	{320., 3200.},   // X+
};

// The rank an estimated TR lands in. Real ranks are percentile-sliced and
// the cutoffs drift with the population; these are the commonly quoted
// stand-ins, good enough for a labelled estimate.
struct Cutoff {
	double tr;
	const char* rank;
};

constexpr Cutoff kRanks[] = {
	{24420., "X+"}, {23600., "X"}, {23000., "U"}, {22000., "SS"},
	{20500., "S+"}, {19000., "S"}, {18000., "S-"}, {16800., "A+"},
	{15200., "A"}, {13800., "A-"}, {12400., "B+"}, {11000., "B"},
	{9600., "B-"}, {8200., "C+"}, {6900., "C"}, {5600., "C-"},
	{4200., "D+"}, {0., "D"},
};

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
	constexpr int count = sizeof kCurve / sizeof kCurve[0];
	if (strength <= kCurve[0].strength) {
		// Below the curve's foot the estimate slides toward the floor of
		// the rating ladder rather than sticking at rank D's average.
		const double part = std::max(0., strength) / kCurve[0].strength;
		return 400. + (kCurve[0].glicko - 400.) * part;
	}
	for (int i = 1; i < count; ++i) {
		if (strength <= kCurve[i].strength) {
			const double span = kCurve[i].strength - kCurve[i - 1].strength;
			const double part = (strength - kCurve[i - 1].strength) / span;
			return kCurve[i - 1].glicko
				+ (kCurve[i].glicko - kCurve[i - 1].glicko) * part;
		}
	}
	return kCurve[count - 1].glicko;
}

double tr_for (double glicko, double rd) {
	const double pi = 3.14159265358979323846;
	const double ln10 = std::log(10.);
	const double x = ((1500. - glicko) * pi)
		/ std::sqrt(3. * ln10 * ln10 * rd * rd
			+ 2500. * (64. * pi * pi + 147. * ln10 * ln10));
	return 25000. / (1. + std::pow(10., x));
}

const char* rank_for (double tr) {
	for (const Cutoff& cutoff : kRanks) {
		if (tr >= cutoff.tr) {
			return cutoff.rank;
		}
	}
	return "D";
}

Estimate estimate (double apm, double pps, double vs) {
	Estimate out;
	if (apm <= 0. && pps <= 0. && vs <= 0.) {
		return out;
	}
	out.glicko = glicko_for(strength(apm, pps, vs));
	out.tr = tr_for(out.glicko);
	out.rank = rank_for(out.tr);
	return out;
}

} // namespace rating
} // namespace forcetris
