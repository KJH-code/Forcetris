// The embedded opponent, pinned: a versus file carries the bot's side under
// an optional top-level key, both engines' readers being the tolerant kind
// that makes that safe. Round-tripped through save and load, absent when
// absent, ignored when malformed - and the recorder's length gate waivable
// for a side that lives inside another file.
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "forcetris/replay.hpp"

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

replay::Placement placement (int form, int x, double elapsed,
                             const std::string& row) {
	replay::Placement place;
	place.form = form;
	place.state = 0;
	place.x = x;
	place.y = 20;
	place.trail = {{0, x, 20}};
	place.presses = {"HARD"};
	place.elapsed = elapsed;
	place.rows = {row};
	place.queue = {1, 2, 3};
	return place;
}

} // namespace

int main () {
	const fs::path folder
		= fs::temp_directory_path() / "forcetris-opponentcheck";
	std::error_code ignored;
	fs::remove_all(folder, ignored);

	// The recorder's length gate, both ways: three placements are too few
	// for a file of their own, but a waived gate hands them over anyway.
	{
		replay::Recorder recorder;
		replay::Meta meta;
		meta.played = "2026-08-20T12:00:00";
		recorder.begin(meta);
		for (int i = 0; i < 3; ++i) {
			recorder.add(placement(i, i, i + 1., ".........."));
		}
		replay::Recorder gated = recorder;
		check("three placements are below the gate",
			!gated.finish(10, 0, 0, 3.).has_value());
		const auto waived = recorder.finish(10, 0, 0, 3., true);
		check("but the waived gate keeps them",
			waived.has_value() && waived->placements.size() == 3);
	}

	// The round trip: a replay with an opponent saves and loads back with
	// the other side intact - meta and placements alike.
	{
		replay::Replay game;
		game.meta.played = "2026-08-20T12:00:01";
		game.meta.gametype = "versus";
		game.meta.das = 100;
		game.meta.sdf = 25;
		for (int i = 0; i < 6; ++i) {
			game.placements.push_back(placement(i, i, i + 1., ".012......"));
		}
		replay::Opponent other;
		other.meta = game.meta;
		other.meta.das = 330;
		other.meta.sdf = 40;
		other.meta.finesse = 0;
		other.placements.push_back(placement(2, 4, 1.52, "33........"));
		other.placements.push_back(placement(5, 7, 2.04, "..44......"));
		game.opponent = other;
		check("a replay with an opponent saves",
			replay::save(game, folder.string()));
		const auto loaded = replay::load(game.path);
		check("and loads back", loaded.has_value());
		check("with the opponent aboard",
			loaded.has_value() && loaded->opponent.has_value());
		if (loaded.has_value() && loaded->opponent.has_value()) {
			const replay::Opponent& theirs = *loaded->opponent;
			check("its handling is its own",
				theirs.meta.das == 330 && theirs.meta.sdf == 40
				&& theirs.meta.gametype == "versus");
			check("its placements survive whole",
				theirs.placements.size() == 2
				&& theirs.placements[0].form == 2
				&& theirs.placements[0].elapsed == 1.52
				&& theirs.placements[0].rows
					== std::vector<std::string>{"33........"}
				&& theirs.placements[1].x == 7
				&& theirs.placements[1].elapsed == 2.04);
			check("and the player's side is untouched",
				loaded->placements.size() == 6
				&& loaded->meta.das == 100);
		}
	}

	// A file without the key has no opponent.
	{
		replay::Replay game;
		game.meta.played = "2026-08-20T12:00:02";
		for (int i = 0; i < 6; ++i) {
			game.placements.push_back(placement(i, i, i + 1., ".........."));
		}
		check("a plain replay saves", replay::save(game, folder.string()));
		const auto loaded = replay::load(game.path);
		check("and loads with no opponent",
			loaded.has_value() && !loaded->opponent.has_value());
	}

	// Malformed opponents are not errors, just not opponents: the readers
	// here stay as tolerant as the rest of the format.
	{
		const auto write = [&] (const char* name, const std::string& body) {
			const fs::path path = folder / name;
			std::ofstream out(path);
			out << body;
			return path.string();
		};
		const std::string skeleton
			= R"("meta": {"gametype": "versus"}, "placements": [)"
			R"({"form": 0, "state": 0, "x": 4, "y": 20, "trail": [[0, 4, 20]],)"
			R"( "presses": ["HARD"], "rows": [".........."], "elapsed": 1.0}])";
		const auto number = replay::load(write("aaaa-number.json",
			"{\"format\": 3, " + skeleton + ", \"opponent\": 12}"));
		check("a numeric opponent is ignored",
			number.has_value() && !number->opponent.has_value());
		const auto empty = replay::load(write("aaab-empty.json",
			"{\"format\": 3, " + skeleton
			+ ", \"opponent\": {\"meta\": {}, \"placements\": []}}"));
		check("an empty opponent counts as absent",
			empty.has_value() && !empty->opponent.has_value());
		const auto rowless = replay::load(write("aaac-rowless.json",
			"{\"format\": 3, " + skeleton + ", \"opponent\": {\"meta\": {}}}"));
		check("a placementless opponent too",
			rowless.has_value() && !rowless->opponent.has_value());
	}

	fs::remove_all(folder, ignored);
	if (failures > 0) {
		std::printf("\n%d check(s) failed.\n", failures);
		return 1;
	}
	std::printf("\nAll checks passed.\n");
	return 0;
}
