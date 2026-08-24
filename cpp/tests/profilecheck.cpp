// The profile history, pinned: games append one line each, load returns
// them oldest first, unknown keys ride along in `stats`, and a spoiled
// line - or a spoiled pair - takes nothing else down with it.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "forcetris/profile.hpp"

using namespace forcetris;
namespace fs = std::filesystem;

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
	const fs::path folder = fs::temp_directory_path() / "forcetris-profile";
	std::error_code ignored;
	fs::remove_all(folder, ignored);
	fs::create_directories(folder, ignored);
	const std::string file = (folder / "profile.dat").string();

	// Two games in, two games out, in order, numbers intact.
	{
		profile::GameRecord first;
		first.played = "2026-08-20T12:00:00";
		first.gametype = "free";
		first.seconds = 61.5;
		first.pieces = 120;
		first.lines = 44;
		first.score = 123456;
		first.attack = 51;
		first.downstack = 3;
		first.pps = 1.95;
		first.apm = 49.8;
		first.vs = 88.2;
		first.finesse = 84.5;
		first.tr = 7421.5;
		first.stats["apl"] = 0.92;
		profile::GameRecord second;
		second.played = "2026-08-20T12:05:00";
		second.gametype = "versus";
		second.won = 1;
		second.pieces = 80;
		check("the first game appends", profile::append(file, first));
		check("and the second", profile::append(file, second));
		const auto loaded = profile::load(file);
		check("both come back in order", loaded.size() == 2
			&& loaded[0].played == first.played
			&& loaded[1].played == second.played);
		if (loaded.size() == 2) {
			check("with the numbers intact",
				loaded[0].pieces == 120 && loaded[0].score == 123456
				&& loaded[0].tr > 7421. && loaded[0].tr < 7422.
				&& loaded[0].finesse > 84.4 && loaded[0].finesse < 84.6);
			check("the munch stat rides along",
				loaded[0].stats.count("apl") == 1
				&& loaded[0].stats.at("apl") > 0.91);
			check("the versus verdict too",
				loaded[1].won == 1 && loaded[1].gametype == "versus"
				&& loaded[0].won == -1);
		}
	}

	// A future build's keys are carried, a spoiled line is dropped alone,
	// and a spoiled pair spoils only itself.
	{
		std::ofstream out(file, std::ios::app);
		out << "game v=9 played=2026-08-20T12:10:00 mode=free "
			"newfangled=3.5 pieces=notanumber lines=7\n";
		out << "this line is not a game at all\n";
		out.close();
		const auto loaded = profile::load(file);
		check("the future line still loads", loaded.size() == 3);
		if (loaded.size() == 3) {
			check("its unknown key lands in stats",
				loaded[2].stats.count("newfangled") == 1);
			check("its one bad pair spoils only itself",
				loaded[2].pieces == 0 && loaded[2].lines == 7);
		}
	}

	// No file is just no history.
	{
		const auto none = profile::load((folder / "missing.dat").string());
		check("a missing file reads as empty", none.empty());
	}

	fs::remove_all(folder, ignored);
	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
