// The career file: stars and the daily latch round-trip, the gate opens
// one rung at a time, unknown lines survive a rewrite, and a missing file
// is an empty career rather than an error.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "forcetris/career.hpp"

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
	const std::string file = "careercheck.dat";
	std::filesystem::remove(file);
	const std::vector<std::string> ladder
		= {"D", "C", "B", "A", "S", "SS", "U", "X"};

	// A missing file is an empty career.
	{
		const career::State state = career::load(file);
		check("a missing file is an empty career",
			state.stars.empty() && state.daily_date.empty());
		check("only the first rung is open",
			career::open(state, ladder, 0) && !career::open(state, ladder, 1));
	}

	// Stars and the daily latch round-trip; the gate follows the stars.
	{
		career::State state;
		state.stars["D"] = 3;
		state.stars["C"] = 1;
		state.daily_date = "2026-08-24";
		state.daily_score = 48200;
		state.unknown.push_back("mystery from the future 7");
		check("the career saves", career::save(file, state));
		const career::State back = career::load(file);
		check("stars round-trip", back.stars.at("D") == 3
			&& back.stars.at("C") == 1 && back.stars.size() == 2);
		check("the daily latch rides along",
			back.daily_date == "2026-08-24" && back.daily_score == 48200);
		check("the future's line survives the rewrite",
			back.unknown.size() == 1
				&& back.unknown[0] == "mystery from the future 7");
		check("a starred rung opens the next",
			career::open(back, ladder, 1) && career::open(back, ladder, 2)
				&& !career::open(back, ladder, 3));
		check("and past the ladder is closed",
			!career::open(back, ladder, ladder.size()));
	}

	// A burned attempt with no finish keeps its -1; damaged lines spoil
	// only themselves.
	{
		std::ofstream out(file, std::ios::trunc);
		out << "stage X not-a-number\n";
		out << "daily 2026-08-25\n";
		out << "stage S 2\n";
		out.close();
		const career::State state = career::load(file);
		check("a damaged star line spoils only itself",
			state.stars.count("X") == 0 && state.stars.at("S") == 2);
		check("a dateonly daily reads as burned unfinished",
			state.daily_date == "2026-08-25" && state.daily_score == -1);
	}

	std::filesystem::remove(file);
	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
