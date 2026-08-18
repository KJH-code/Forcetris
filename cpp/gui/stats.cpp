#include "stats.hpp"

#include <cstdio>

#include "forcetris/attack.hpp"

namespace forcetris {
namespace gui {

namespace {

std::string fmt (const char* pattern, double value) {
	char text[32];
	std::snprintf(text, sizeof text, pattern, value);
	return text;
}

std::string count (int value) {
	return std::to_string(value);
}

// The rates guard their own zero: a game one frame old divides by nothing.
double per_second (int total, double seconds) {
	return seconds > 0. ? total / seconds : 0.;
}

} // namespace

const std::vector<StatDef>& all_stats () {
	static const std::vector<StatDef> stats = {
		{"time", "Time", [] (const Session& s) {
			const double t = s.seconds();
			const int minutes = static_cast<int>(t) / 60;
			char text[32];
			std::snprintf(text, sizeof text, "%d:%04.1f", minutes, t - minutes * 60);
			return std::string(text);
		}},
		{"pieces", "Pieces", [] (const Session& s) { return count(s.pieces()); }},
		{"pps", "PPS", [] (const Session& s) {
			return fmt("%.2f", per_second(s.pieces(), s.seconds()));
		}},
		{"lines", "Lines", [] (const Session& s) {
			return count(s.sim().lines_cleared());
		}},
		{"attack", "Attack", [] (const Session& s) {
			return count(s.sim().attack_sent());
		}},
		{"apm", "APM", [] (const Session& s) {
			return fmt("%.1f", attack::apm(s.sim().attack_sent(), s.seconds()));
		}},
		{"aps", "APS", [] (const Session& s) {
			return fmt("%.2f", per_second(s.sim().attack_sent(), s.seconds()));
		}},
		{"vs", "VS", [] (const Session& s) {
			// No garbage arrives in the trainer, so the downstack half is zero.
			return fmt("%.1f", attack::vs_score(s.sim().attack_sent(), 0, s.seconds()));
		}},
		{"b2b", "B2B", [] (const Session& s) {
			const int now = s.sim().b2b() - 1;
			return count(now > 0 ? now : 0) + " / " + count(s.best_b2b());
		}},
		{"combo", "Combo", [] (const Session& s) {
			const int now = s.sim().combo() - 1;
			return count(now > 0 ? now : 0) + " / " + count(s.best_combo());
		}},
		{"finesse", "Finesse", [] (const Session& s) {
			if (s.judged() == 0) {
				return std::string("100%");
			}
			return fmt("%.0f%%", 100. * (s.judged() - s.faults()) / s.judged());
		}},
		{"faults", "Faults", [] (const Session& s) { return count(s.faults()); }},
		{"kpp", "Keys/piece", [] (const Session& s) {
			return fmt("%.2f", s.pieces() > 0
				? static_cast<double>(s.presses()) / s.pieces() : 0.);
		}},
		{"spins", "Spins", [] (const Session& s) { return count(s.spins()); }},
		{"perfects", "Perfect clears", [] (const Session& s) {
			return count(s.perfects());
		}},
		{"forced", "Forced drops", [] (const Session& s) { return count(s.forced()); }},
	};
	return stats;
}

} // namespace gui
} // namespace forcetris
