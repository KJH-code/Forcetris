// The munch: MinoMuncher's statistics, re-derived for this trainer's own
// replay records. The formulas follow the MIT-licensed minomuncher-core -
// clear-type buckets, the three-way attack-per-line split, the Gaussian
// mixture over placement PPS, the four-deep validated well rule, the surge
// accounting with its floor of four, the up/downstack segmentation and the
// cheesiness sigmoid - computed here from placements and board snapshots
// instead of a re-simulated multiplayer replay. What genuinely needs the
// other players' wire (sent-garbage sizes after cancellation, received
// batches, deaths) is approximated where honest and skipped where not:
// cheesiness is scored over raw attack values, and the death and tanking
// charts are not pretended at.
#include "forcetris/munch.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "forcetris/board.hpp"
#include "forcetris/replay.hpp"

namespace forcetris {
namespace munch {

namespace {

struct Meaning {
	const char* id;
	const char* label;
};

const std::array<Meaning, 31> kMeanings = {{
	{"ct_pc", "Perfect clears"},
	{"ct_quad", "Quads"},
	{"ct_tst", "T-spin triples"},
	{"ct_tsd", "T-spin doubles"},
	{"ct_tss", "T-spin singles"},
	{"ct_allspin", "All-spins"},
	{"ct_triple", "Triples"},
	{"ct_double", "Doubles"},
	{"ct_single", "Singles"},
	{"eff_t", "T efficiency %"},
	{"eff_i", "I efficiency %"},
	{"eff_allspin", "All-spin efficiency %"},
	{"app", "Attack per piece"},
	{"apl", "Attack per line"},
	{"apl_up", "Upstack APL"},
	{"apl_ds", "Downstack APL"},
	{"apl_cheese", "Cheese APL"},
	{"cheesiness", "Attack cheesiness"},
	{"kpp", "Keys per piece"},
	{"kps", "Keys per second"},
	{"burst_pps", "Burst PPS"},
	{"plonk_pps", "Plonk PPS"},
	{"pps_var", "PPS variance"},
	{"pps_up", "Upstack PPS"},
	{"pps_ds", "Downstack PPS"},
	{"ds_ratio", "Downstacking ratio"},
	{"well_col", "Preferred well column"},
	{"well_share", "Well loyalty %"},
	{"surge_len", "Surge length"},
	{"surge_apl", "Surge APL"},
	{"surge_rate", "Surge conversion %"},
}};

constexpr int kSurgeFloor = 4;   // SURGE_BTB_FLOOR, as minomuncher holds it.

// A padded snapshot's garbage runs, scanned bottom-up the way the muncher
// scans them: a run is consecutive rows that are all garbage but one hole,
// sharing that hole's column. Returns run lengths bottom-first, and the
// topmost run's hole column.
struct GarbageRuns {
	std::vector<int> heights;
	int top_hole = -1;
};

GarbageRuns garbage_runs (const std::vector<std::string>& padded) {
	GarbageRuns runs;
	int hole = -2;
	for (int y = static_cast<int>(padded.size()) - 1; y >= 0; --y) {
		const std::string& row = padded[y];
		int this_hole = -1;
		bool garbage_row = !row.empty();
		for (int x = 0; x < static_cast<int>(row.size()); ++x) {
			if (row[x] == '.') {
				if (this_hole >= 0) {
					garbage_row = false;
					break;
				}
				this_hole = x;
			} else if (row[x] != '7') {
				garbage_row = false;
				break;
			}
		}
		if (!garbage_row || this_hole < 0) {
			break;
		}
		if (this_hole == hole) {
			runs.heights.back() += 1;
		} else {
			runs.heights.push_back(1);
			hole = this_hole;
		}
		runs.top_hole = this_hole;
	}
	return runs;
}

int garbage_lines (const GarbageRuns& runs) {
	int total = 0;
	for (const int height : runs.heights) {
		total += height;
	}
	return total;
}

// Cheese on the board, by the muncher's rule: the topmost garbage run is
// shorter than a clean four.
bool cheese_on_board (const GarbageRuns& runs) {
	return !runs.heights.empty() && runs.heights.back() < 4;
}

// The muncher's well: the column whose stack is lowest, validated by rows
// above it being complete but for that column, at least four of them, and
// not all of them garbage.
int well_column (const std::vector<std::string>& padded) {
	const int height = static_cast<int>(padded.size());
	if (height == 0) {
		return -1;
	}
	const int width = static_cast<int>(padded[0].size());
	int lowest = -1;
	int lowest_top = -1;   // Largest y index whose column is empty above.
	for (int x = 0; x < width; ++x) {
		int top = height;
		for (int y = 0; y < height; ++y) {
			if (padded[y][x] != '.') {
				top = y;
				break;
			}
		}
		if (top > lowest_top) {
			lowest_top = top;
			lowest = x;
		}
	}
	if (lowest < 0) {
		return -1;
	}
	int count = 0;
	int garbage = 0;
	for (int y = lowest_top - 1; y >= 0; --y) {
		bool complete = true;
		bool has_garbage = false;
		for (int x = 0; x < width; ++x) {
			if (x == lowest) {
				continue;
			}
			if (padded[y][x] == '.') {
				complete = false;
				break;
			}
			has_garbage = has_garbage || padded[y][x] == '7';
		}
		if (!complete) {
			break;
		}
		++count;
		garbage += has_garbage ? 1 : 0;
	}
	if (count < 4 || garbage == count) {
		return -1;
	}
	return lowest;
}

// A three-part one-dimensional Gaussian mixture over the per-placement
// PPS samples - the muncher's burst (fastest mean) and plonk (slowest
// mean), fit by a deterministic EM: means seeded at the sample's 10th,
// 50th and 90th percentiles.
struct Mixture {
	double burst = 0.;
	double plonk = 0.;
};

Mixture fit_mixture (std::vector<double> samples) {
	Mixture out;
	if (samples.empty()) {
		return out;
	}
	std::sort(samples.begin(), samples.end());
	const auto percentile = [&] (double p) {
		return samples[static_cast<size_t>(p * (samples.size() - 1))];
	};
	std::array<double, 3> mean
		= {percentile(0.1), percentile(0.5), percentile(0.9)};
	std::array<double, 3> variance;
	const double spread = std::max(0.05, mean[2] - mean[0]);
	variance.fill(spread * spread / 4.);
	std::array<double, 3> weight = {1 / 3., 1 / 3., 1 / 3.};
	for (int pass = 0; pass < 40; ++pass) {
		std::array<double, 3> sum{};
		std::array<double, 3> sq{};
		std::array<double, 3> mass{};
		for (const double x : samples) {
			std::array<double, 3> p{};
			double total = 0.;
			for (int k = 0; k < 3; ++k) {
				const double d = x - mean[k];
				p[k] = weight[k]
					* std::exp(-d * d / (2. * variance[k]))
					/ std::sqrt(variance[k]);
				total += p[k];
			}
			if (total <= 0.) {
				continue;
			}
			for (int k = 0; k < 3; ++k) {
				const double r = p[k] / total;
				mass[k] += r;
				sum[k] += r * x;
				sq[k] += r * x * x;
			}
		}
		for (int k = 0; k < 3; ++k) {
			if (mass[k] < 1e-9) {
				continue;
			}
			mean[k] = sum[k] / mass[k];
			variance[k] = std::max(
				1e-4, sq[k] / mass[k] - mean[k] * mean[k]);
			weight[k] = mass[k] / samples.size();
		}
	}
	out.burst = *std::max_element(mean.begin(), mean.end());
	out.plonk = *std::min_element(mean.begin(), mean.end());
	return out;
}

// The cheesiness scorer, weights and window as the muncher carries them
// (Blockfish-derived), fed here with raw per-clear attack instead of the
// post-cancellation sends a trainer does not have.
struct CheeseScorer {
	std::vector<int> history;
	double score = 0.;
	int lines = 0;

	void feed (int sent) {
		static const double kWeights[4]
			= {1.87177053, 1.44428749, 1.31233034, 1.16560664};
		static const double kMin = 1.16560664;
		static const double kMax = 1.87177053;
		if (sent <= 0) {
			return;
		}
		const int count = std::min(sent, 4);
		double raw;
		if (history.empty()) {
			raw = kWeights[count - 1] * count;
		} else {
			raw = kWeights[*std::max_element(
				history.begin(), history.end()) - 1] * count;
		}
		if (count == 4) {
			history.clear();
		}
		history.push_back(count);
		if (history.size() > 3) {
			history.erase(history.begin());
		}
		score += (raw - kMin * count) * count / (kMax - kMin);
		lines += sent;
	}

	double cheesiness () const {
		if (lines == 0) {
			return 0.;
		}
		const double x = score / lines;
		const double top = 1. / (1. + std::exp(-10. * (x - 0.3)))
			- 1. / (1. + std::exp(3.));
		const double bottom = 1. / (1. + std::exp(-7.))
			- 1. / (1. + std::exp(3.));
		return std::clamp(top / bottom, 0., 1.);
	}
};

// The muncher's stacking segmentation: at least seven quiet placements in
// a row are upstacking; a garbage-digging stretch is downstacking when the
// hole column moved at least once (drilling one well is not digging).
struct StackSpeed {
	double up_seconds = 0.;
	int up_pieces = 0;
	double down_seconds = 0.;
	int down_pieces = 0;

	int quiet_run = 0;
	double quiet_seconds = 0.;
	int dig_run = 0;
	double dig_seconds = 0.;
	double pending_seconds = 0.;
	int pending = 0;
	int shifts = 0;
	int last_hole = -2;
	int since_dig = 0;

	void quiet (double seconds, bool cleared) {
		if (cleared) {
			flush_up();
		} else {
			++quiet_run;
			quiet_seconds += seconds;
		}
		if (dig_run > 0) {
			pending_seconds += seconds;
			++pending;
			if (++since_dig >= 7) {
				flush_down(1);
			}
		}
	}

	void dig (double seconds, int hole) {
		flush_up();
		if (dig_run > 0) {
			dig_seconds += pending_seconds;
			dig_run += pending;
		}
		pending_seconds = 0.;
		pending = 0;
		since_dig = 0;
		if (last_hole >= -1 && hole != last_hole) {
			++shifts;
		}
		last_hole = hole;
		++dig_run;
		dig_seconds += seconds;
	}

	void flush_up () {
		if (quiet_run >= 7) {
			up_pieces += quiet_run;
			up_seconds += quiet_seconds;
		}
		quiet_run = 0;
		quiet_seconds = 0.;
	}

	void flush_down (int need) {
		if (dig_run > 0 && shifts >= need) {
			down_pieces += dig_run;
			down_seconds += dig_seconds;
		}
		dig_run = 0;
		dig_seconds = 0.;
		pending_seconds = 0.;
		pending = 0;
		shifts = 0;
		last_hole = -2;
		since_dig = 0;
	}
};

} // namespace

double Stats::get (const std::string& id, double fallback) const {
	for (const Group& group : groups) {
		for (const Stat& stat : group.stats) {
			if (id == stat.id) {
				return stat.value;
			}
		}
	}
	return fallback;
}

const std::vector<const char*>& order () {
	static const std::vector<const char*> ids = [] {
		std::vector<const char*> all;
		for (const Meaning& meaning : kMeanings) {
			all.push_back(meaning.id);
		}
		return all;
	}();
	return ids;
}

const char* label (const std::string& id) {
	for (const Meaning& meaning : kMeanings) {
		if (id == meaning.id) {
			return meaning.label;
		}
	}
	return "";
}

Stats crunch (const replay::Replay& game) {
	Stats stats;
	const auto& places = game.placements;
	const int pieces = static_cast<int>(places.size());
	if (pieces == 0) {
		return stats;
	}

	// Clear-type buckets, the muncher's nine.
	int pc = 0, quad = 0, tst = 0, tsd = 0, tss = 0, allspin = 0;
	int triple = 0, twice = 0, single = 0;
	int t_pieces = 0, i_pieces = 0, all_pieces = 0;
	int attack = 0, lines = 0, presses = 0;
	int ds_attack = 0, cheese_attack = 0;
	int ds_cleared = 0, cheese_cleared = 0;
	int chain = 0;
	int surge_attack = 0, surge_lines = 0, surge_b2b = 0;
	int surge_chains = 0, surge_fails = 0;
	std::array<int, 16> wells{};
	int welled = 0;
	CheeseScorer cheeser;
	StackSpeed speed;
	std::vector<double> pps_samples;

	double last_at = 0.;
	std::vector<std::string> before
		= replay::padded(std::vector<std::string>{});
	for (int i = 0; i < pieces; ++i) {
		const replay::Placement& place = places[i];
		const GarbageRuns runs_before = garbage_runs(before);
		const std::vector<std::string> after = replay::padded(place.rows);
		const GarbageRuns runs_after = garbage_runs(after);
		const int dug = std::max(
			0, garbage_lines(runs_before) - garbage_lines(runs_after));
		const double gap = std::max(0.02, place.elapsed - last_at);
		last_at = place.elapsed;
		pps_samples.push_back(std::min(10., 1. / gap));
		presses += static_cast<int>(place.presses.size());
		attack += place.attack;
		lines += place.lines;
		t_pieces += place.form == T ? 1 : 0;
		i_pieces += place.form == I ? 1 : 0;
		if (place.form != I && place.form != T && place.form != O
			&& place.lines > 0) {
			++all_pieces;
		}

		const bool mini = place.spin.rfind("MINI", 0) == 0;
		const bool spun = !place.spin.empty();
		const bool t_full = spun && !mini && place.form == T;
		bool keeps = false;
		if (place.lines > 0) {
			if (place.perfect) {
				++pc;
				keeps = true;
			} else if (spun && !t_full) {
				++allspin;
				keeps = true;
			} else if (t_full) {
				(place.lines >= 3 ? tst : place.lines == 2 ? tsd : tss) += 1;
				keeps = true;
			} else if (place.lines >= 4) {
				++quad;
				keeps = true;
			} else if (place.lines == 3) {
				++triple;
			} else if (place.lines == 2) {
				++twice;
			} else {
				++single;
			}
			cheeser.feed(place.attack);
			if (dug > 0) {
				ds_attack += place.attack;
				ds_cleared += dug;
				if (cheese_on_board(runs_before)) {
					cheese_attack += place.attack;
					cheese_cleared += dug;
				}
			}
			// The surge walk: b2b clears grow the chain, any other clear
			// closes it - a real surge from the floor up, a fail below it.
			if (keeps) {
				++chain;
			} else {
				if (chain >= kSurgeFloor) {
					++surge_chains;
					surge_b2b += chain;
				} else if (chain > 0) {
					surge_fails += chain;
				}
				chain = 0;
			}
		}
		if (dug > 0) {
			speed.dig(gap, runs_before.top_hole);
		} else {
			speed.quiet(gap, place.lines > 0);
		}
		const int well = well_column(after);
		if (well >= 0 && well < static_cast<int>(wells.size())) {
			++wells[well];
			++welled;
		}
		before = after;
	}
	// An unfinished chain at the game's end counts only when it reached
	// the floor; a short one closed by the topout is not a failed surge.
	if (chain >= kSurgeFloor) {
		++surge_chains;
		surge_b2b += chain;
	}
	speed.flush_up();
	speed.flush_down(3);

	// A second pass for the surge's own attack and lines, now that chains
	// are cheap to re-walk: sum attack and lines over every placement of
	// every chain that reached the floor, breaker included, as the muncher
	// counts them.
	{
		int run_attack = 0;
		int run_lines = 0;
		int run = 0;
		for (const replay::Placement& place : places) {
			if (place.lines == 0) {
				continue;
			}
			const bool spun = !place.spin.empty();
			const bool keeps = place.perfect || spun || place.lines >= 4;
			run_attack += place.attack;
			run_lines += place.lines;
			if (keeps) {
				++run;
			} else {
				if (run >= kSurgeFloor) {
					surge_attack += run_attack;
					surge_lines += run_lines;
				}
				run_attack = 0;
				run_lines = 0;
				run = 0;
			}
		}
		if (run >= kSurgeFloor) {
			surge_attack += run_attack;
			surge_lines += run_lines;
		}
	}

	const double seconds = std::max(0.02, places.back().elapsed);
	const Mixture mixture = fit_mixture(pps_samples);
	const double pps = pieces / seconds;
	double variance = 0.;
	for (const double sample : pps_samples) {
		variance += (sample - pps) * (sample - pps);
	}
	variance /= pps_samples.size();
	int well_col = 0;
	for (int x = 1; x < static_cast<int>(wells.size()); ++x) {
		if (wells[x] > wells[well_col]) {
			well_col = x;
		}
	}

	const auto stat = [] (const char* id, double value) {
		return Stat{id, label(id), value};
	};
	Group clears{"Clears", {
		stat("ct_pc", pc), stat("ct_quad", quad), stat("ct_tst", tst),
		stat("ct_tsd", tsd), stat("ct_tss", tss),
		stat("ct_allspin", allspin), stat("ct_triple", triple),
		stat("ct_double", twice), stat("ct_single", single)}};
	Group efficiency{"Efficiency", {
		stat("eff_t", t_pieces > 0
			? 100. * (tss + tsd + tst) / t_pieces : 0.),
		stat("eff_i", i_pieces > 0 ? 100. * quad / i_pieces : 0.),
		stat("eff_allspin", all_pieces > 0
			? 100. * allspin / all_pieces : 0.)}};
	const int up_lines = lines - ds_cleared;
	Group offense{"Attack", {
		stat("app", double(attack) / pieces),
		stat("apl", lines > 0 ? double(attack) / lines : 0.),
		stat("apl_up", up_lines > 0
			? double(attack - ds_attack) / up_lines : 0.),
		stat("apl_ds", ds_cleared > 0
			? double(ds_attack) / ds_cleared : 0.),
		stat("apl_cheese", cheese_cleared > 0
			? double(cheese_attack) / cheese_cleared : 0.),
		stat("cheesiness", 100. * cheeser.cheesiness())}};
	Group pace{"Pace", {
		stat("kpp", double(presses) / pieces),
		stat("kps", presses / seconds),
		stat("burst_pps", mixture.burst),
		stat("plonk_pps", mixture.plonk),
		stat("pps_var", variance),
		stat("pps_up", speed.up_seconds > 0.
			? speed.up_pieces / speed.up_seconds : 0.),
		stat("pps_ds", speed.down_seconds > 0.
			? speed.down_pieces / speed.down_seconds : 0.)}};
	Group stack{"Stack", {
		stat("ds_ratio", speed.up_seconds + speed.down_seconds > 0.
			? 100. * speed.down_seconds
				/ (speed.up_seconds + speed.down_seconds) : 0.),
		stat("well_col", well_col),
		stat("well_share", welled > 0
			? 100. * wells[well_col] / welled : 0.)}};
	Group surge{"Surge", {
		stat("surge_len", surge_chains > 0
			? double(surge_b2b) / surge_chains : 0.),
		stat("surge_apl", surge_lines > 0
			? double(surge_attack) / surge_lines : 0.),
		stat("surge_rate", surge_chains + surge_fails > 0
			? 100. * surge_chains / (surge_chains + surge_fails) : 0.)}};

	stats.groups = {clears, efficiency, offense, pace, stack, surge};
	return stats;
}

} // namespace munch
} // namespace forcetris
