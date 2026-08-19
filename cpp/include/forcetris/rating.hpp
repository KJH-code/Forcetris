// An estimated Tetra League standing, from nothing but the run's own figures.
//
// There is no opponent in a trainer, so there is no real rating to compute:
// Glicko needs games against rated players. What there is instead is the
// actual shape of Tetra League - the real average APM, PPS, VS and TR
// reported per rank on TETR.IO's own leaderboard - and a run's figures can
// be placed against that curve. The estimate is exactly that and no more:
// an interpolation over the reported per-rank averages, entertainment-grade,
// labelled as such wherever it is shown.
//
// The TR conversion is exact: TETR.IO's published formula from a Glicko
// rating and deviation to the 0..25000 Tetra Rating scale. Since the curve
// above already lands on real per-rank TR figures, the "estimated Glicko"
// this module reports is the formula run backwards - the Glicko that would
// produce the interpolated TR at a settled player's deviation - rather than
// a second, independently fitted number that might disagree with it.
#pragma once

namespace forcetris {
namespace rating {

struct Estimate {
	double glicko = 0.;     // The estimated Glicko-2 rating.
	double tr = 0.;         // 0..25000, by the official conversion.
	const char* rank = ""; // "D" .. "X+", by approximate TR cutoffs.
};

// The strength signal the curve is walked with: VS where the run produced
// any, an APM-derived stand-in where it did not (a run with no attack and no
// downstack has a VS of zero without being a zero-strength run).
double strength (double apm, double pps, double vs);

// The estimated Glicko for a strength value: piecewise-linear over the
// per-rank averages, clamped to the curve's ends.
double glicko_for (double strength);

// TETR.IO's TR formula, verbatim: 25000 / (1 + 10^x) with
// x = ((1500 - glicko) * pi) / sqrt(3 ln(10)^2 rd^2 + 2500 (64 pi^2 + 147 ln(10)^2)).
double tr_for (double glicko, double rd = 60.);

// tr_for's inverse: the Glicko that converts to a given TR, found by
// bisection since tr_for has no convenient closed form to invert by hand.
double glicko_from_tr (double tr, double rd = 60.);

// The rank an estimated TR lands in, by fixed approximate cutoffs.
const char* rank_for (double tr);

// The whole estimate for a run's figures. A run too small to mean anything
// (no pieces, no time) comes back rank "" - the screen says so instead.
Estimate estimate (double apm, double pps, double vs);

} // namespace rating
} // namespace forcetris
