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
			// Timed mode shows what is left of the clock; the others show how
			// long the run has gone on.
			char text[32];
			if (s.sim().config().gametype == 1) {
				const long ms = s.sim().timer_ms();
				std::snprintf(text, sizeof text, "%ld:%02ld:%02ld",
					ms / 60000, ms / 1000 % 60, ms % 1000 / 10);
				return std::string(text);
			}
			const double t = s.seconds();
			const int minutes = static_cast<int>(t) / 60;
			std::snprintf(text, sizeof text, "%d:%04.1f", minutes, t - minutes * 60);
			return std::string(text);
		}},
		{"level", "Level", [] (const Session& s) {
			return count(s.sim().level());
		}},
		{"cheese", "Cheese", [] (const Session& s) {
			// The race counts what is left to dig, standing rows included;
			// survival counts what has risen so far.
			const int gametype = s.sim().config().gametype;
			if (gametype == 3) {
				return count(s.sim().cheese_left()
					+ s.sim().board().garbage_rows());
			}
			if (gametype == 4) {
				return count(s.sim().board().garbage_rows());
			}
			return std::string("-");
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
			// Arcade's dug garbage rows count as downstack, the same figure
			// the replay summary of the run reports.
			return fmt("%.1f", attack::vs_score(
				s.sim().attack_sent(), s.sim().downstack(), s.seconds()));
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
