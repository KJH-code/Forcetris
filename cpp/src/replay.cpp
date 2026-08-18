#include "forcetris/replay.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "forcetris/attack.hpp"
#include "forcetris/finesse.hpp"
#include "forcetris/piece.hpp"
#include "forcetris/sim.hpp"

namespace forcetris {
namespace replay {

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

// Tolerant readers: a missing or oddly typed field comes back as the
// default, the way Python's fields.get(name) comes back as None. That is
// what lets a version 2 file open without its queue and hold.
int int_or (const json& data, const char* key, int fallback) {
	const auto found = data.find(key);
	return found != data.end() && found->is_number()
		? found->get<int>() : fallback;
}

double num_or (const json& data, const char* key, double fallback) {
	const auto found = data.find(key);
	return found != data.end() && found->is_number()
		? found->get<double>() : fallback;
}

bool bool_or (const json& data, const char* key, bool fallback) {
	const auto found = data.find(key);
	if (found == data.end()) {
		return fallback;
	}
	if (found->is_boolean()) {
		return found->get<bool>();
	}
	if (found->is_number()) {
		return found->get<double>() != 0.;
	}
	return fallback;
}

std::string text_or (const json& data, const char* key) {
	const auto found = data.find(key);
	return found != data.end() && found->is_string()
		? found->get<std::string>() : std::string();
}

Placement read_placement (const json& data) {
	Placement place;
	place.form = int_or(data, "form", 0);
	place.state = int_or(data, "state", 0);
	place.x = int_or(data, "x", 0);
	place.y = int_or(data, "y", 0);
	place.held = bool_or(data, "held", false);
	if (const auto found = data.find("presses");
		found != data.end() && found->is_array()) {
		for (const json& press : *found) {
			if (press.is_string()) {
				place.presses.push_back(press.get<std::string>());
			}
		}
	}
	if (const auto found = data.find("trail");
		found != data.end() && found->is_array()) {
		for (const json& stop : *found) {
			if (stop.is_array() && stop.size() == 3) {
				place.trail.push_back({stop[0].get<int>(),
					stop[1].get<int>(), stop[2].get<int>()});
			}
		}
	}
	if (const auto found = data.find("best");
		found != data.end() && found->is_number()) {
		place.best = found->get<int>();
	}
	place.judged = bool_or(data, "judged", false);
	place.forced = bool_or(data, "forced", false);
	place.lines = int_or(data, "lines", 0);
	place.spin = text_or(data, "spin");
	place.perfect = bool_or(data, "perfect", false);
	place.combo = int_or(data, "combo", 0);
	place.b2b = int_or(data, "b2b", 0);
	place.score = int_or(data, "score", 0);
	place.elapsed = num_or(data, "elapsed", 0.);
	if (const auto found = data.find("rows");
		found != data.end() && found->is_array()) {
		for (const json& row : *found) {
			if (row.is_string()) {
				place.rows.push_back(row.get<std::string>());
			}
		}
	}
	if (const auto found = data.find("queue");
		found != data.end() && found->is_array()) {
		for (const json& form : *found) {
			if (form.is_number()) {
				place.queue.push_back(form.get<int>());
			}
		}
	}
	place.stored = int_or(data, "stored", 7);
	place.attack = int_or(data, "attack", 0);
	return place;
}

json write_placement (const Placement& place) {
	// Every slot the Python reader knows, in its own order.
	json out;
	out["form"] = place.form;
	out["state"] = place.state;
	out["x"] = place.x;
	out["y"] = place.y;
	out["held"] = place.held;
	out["presses"] = place.presses;
	json trail = json::array();
	for (const auto& stop : place.trail) {
		trail.push_back({stop[0], stop[1], stop[2]});
	}
	out["trail"] = trail;
	if (place.best.has_value()) {
		out["best"] = *place.best;
	} else {
		out["best"] = nullptr;
	}
	out["judged"] = place.judged;
	out["forced"] = place.forced;
	out["lines"] = place.lines;
	out["spin"] = place.spin;
	out["perfect"] = place.perfect;
	out["combo"] = place.combo;
	out["b2b"] = place.b2b;
	out["score"] = place.score;
	out["elapsed"] = place.elapsed;
	out["rows"] = place.rows;
	out["queue"] = place.queue;
	out["stored"] = place.stored;
	out["attack"] = place.attack;
	return out;
}

Meta read_meta (const json& data) {
	Meta meta;
	if (!data.is_object()) {
		return meta;
	}
	meta.played = text_or(data, "played");
	const std::string gametype = text_or(data, "gametype");
	if (!gametype.empty()) {
		meta.gametype = gametype;
	}
	meta.forced_delay = num_or(data, "forced_delay", 0.);
	meta.finesse = int_or(data, "finesse", 0);
	meta.spinrule = int_or(data, "spinrule", 0);
	meta.cleartype = int_or(data, "cleartype", 0);
	meta.das = int_or(data, "das", 0);
	meta.arr = int_or(data, "arr", 0);
	meta.dcd = int_or(data, "dcd", 0);
	meta.sdf = int_or(data, "sdf", 0);
	meta.are = int_or(data, "are", 0);
	meta.score = int_or(data, "score", 0);
	meta.lines = int_or(data, "lines", 0);
	meta.downstack = int_or(data, "downstack", 0);
	meta.seconds = num_or(data, "seconds", 0.);
	return meta;
}

json write_meta (const Meta& meta) {
	json out;
	out["played"] = meta.played;
	out["gametype"] = meta.gametype;
	out["forced_delay"] = meta.forced_delay;
	out["finesse"] = meta.finesse;
	out["spinrule"] = meta.spinrule;
	out["cleartype"] = meta.cleartype;
	out["das"] = meta.das;
	out["arr"] = meta.arr;
	out["dcd"] = meta.dcd;
	out["sdf"] = meta.sdf;
	out["are"] = meta.are;
	out["score"] = meta.score;
	out["lines"] = meta.lines;
	out["downstack"] = meta.downstack;
	out["seconds"] = meta.seconds;
	return out;
}

// Sortable, and free of anything Windows refuses in a path.
std::string filename (const Replay& replay) {
	std::string stamp = replay.meta.played;
	std::string cleaned;
	for (const char letter : stamp) {
		if (letter == ':' || letter == '-') {
			continue;
		}
		cleaned.push_back(letter == 'T' ? '-' : letter);
	}
	if (cleaned.empty()) {
		cleaned = "replay";
	}
	return cleaned + "-" + replay.meta.gametype + ".json";
}

void prune (const std::string& folder) {
	// Keep the newest kKeep files and drop the rest.
	std::vector<std::string> names;
	std::error_code ignored;
	for (const auto& entry : fs::directory_iterator(folder, ignored)) {
		const std::string name = entry.path().filename().string();
		if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
			names.push_back(name);
		}
	}
	std::sort(names.begin(), names.end());
	if (names.size() <= static_cast<size_t>(kKeep)) {
		return;
	}
	for (size_t i = 0; i + kKeep < names.size(); ++i) {
		fs::remove(fs::path(folder) / names[i], ignored);
	}
}

} // namespace

int Placement::wasted () const {
	if (!judged || !best.has_value()) {
		return 0;
	}
	return std::max(0, static_cast<int>(presses.size()) - *best);
}

std::vector<std::array<int, 3>> Placement::steps (bool fixed) const {
	const std::array<int, 3> spawn{0, kSpawnX, kSpawnY};
	const std::array<int, 3> landed{state, x, y};
	std::vector<std::array<int, 3>> stops{spawn};
	std::optional<finesse::Route> route;
	if (fixed && judged) {
		route = finesse::route(form, state, x);
	}
	if (route.has_value()) {
		// The route is walked over an empty field, which is where finesse is
		// measured, so every stop on it is at the spawn row.
		for (const auto& [rstate, rx] : finesse::follow(form, *route)) {
			stops.push_back({rstate, rx, kSpawnY});
		}
	} else {
		for (const auto& stop : trail) {
			stops.push_back(stop);
		}
	}
	if (stops.back() != landed) {
		stops.push_back(landed);
	}
	return stops;
}

std::vector<std::string> Placement::presses_shown (bool fixed) const {
	if (fixed && judged) {
		if (const auto route = finesse::route(form, state, x)) {
			std::vector<std::string> names;
			for (const finesse::Move move : *route) {
				names.push_back(finesse::name(move));
			}
			return names;
		}
	}
	return presses;
}

Placement from_locked (const Locked& lock, std::vector<std::string> rows) {
	static const char* kLetters = "IOTSZJL";
	Placement place;
	place.form = lock.form;
	place.state = lock.state;
	place.x = lock.x;
	place.y = lock.y;
	place.held = lock.held;
	place.presses = lock.presses;
	place.trail = lock.trail;
	if (lock.best >= 0) {
		place.best = lock.best;
		place.judged = true;
	}
	place.forced = lock.forced;
	place.lines = lock.lines;
	if (lock.spin != 0 && lock.form >= 0 && lock.form <= 6) {
		place.spin = std::string(lock.spin == 1 ? "MINI " : "")
			+ kLetters[lock.form] + "-SPIN";
	}
	place.perfect = lock.perfect;
	place.combo = std::max(0, lock.combo - 1);
	place.b2b = std::max(0, lock.b2b - 1);
	place.score = lock.score;
	place.attack = lock.attack;
	// The sim's clock is its frame count; the engine rounds to hundredths.
	place.elapsed = std::round(lock.frame * 0.02 * 100.) / 100.;
	place.rows = std::move(rows);
	for (const int form : lock.queue3) {
		if (form >= 0) {
			place.queue.push_back(form);
		}
	}
	place.stored = lock.stored < 0 ? 7 : lock.stored;
	return place;
}

std::vector<std::string> padded (
	const std::vector<std::string>& rows, int height) {
	const size_t wide = rows.empty() ? 10 : rows.front().size();
	std::vector<std::string> full;
	for (int i = 0; i < height - static_cast<int>(rows.size()); ++i) {
		full.push_back(std::string(wide, '.'));
	}
	full.insert(full.end(), rows.begin(), rows.end());
	return full;
}

std::vector<std::string> Replay::before (size_t index) const {
	if (index > 0 && index < placements.size()) {
		return placements[index - 1].rows;
	}
	return {};
}

std::string Replay::title () const {
	std::string stamp = meta.played.substr(0, 16);
	for (char& letter : stamp) {
		if (letter == 'T') {
			letter = ' ';
		}
	}
	if (stamp.empty()) {
		stamp = "unknown";
	}
	std::string type = meta.gametype;
	if (!type.empty()) {
		type[0] = static_cast<char>(std::toupper(type[0]));
	}
	return stamp + "  " + type + "  " + std::to_string(meta.score) + " pts";
}

Summary Replay::summary (bool fixed) const {
	Summary out;
	out.placements = static_cast<int>(placements.size());
	for (const Placement& place : placements) {
		if (place.judged) {
			++out.judged;
			if (!fixed && place.fault()) {
				++out.faults;
				out.wasted += place.wasted();
			}
		}
		// As the Python side counts it: any placement with a best uses it
		// under the correction, judged or not - the two only ever differ in
		// a hand-corrupted file, and the reference behaviour wins there too.
		out.presses += fixed && place.best.has_value()
			? *place.best : static_cast<int>(place.presses.size());
		out.lines += place.lines;
		if (place.lines > 0) {
			++out.clears[place.lines];
		}
		if (!place.spin.empty()) {
			++out.spins;
		}
		if (place.perfect) {
			++out.perfects;
		}
		out.best_b2b = std::max(out.best_b2b, place.b2b);
		out.best_combo = std::max(out.best_combo, place.combo);
		out.attack += place.attack;
	}
	out.rate = out.judged == 0
		? 1. : static_cast<double>(out.judged - out.faults) / out.judged;
	out.ppp = out.placements == 0
		? 0. : static_cast<double>(out.presses) / out.placements;
	out.seconds = meta.seconds;
	out.pps = out.seconds <= 0. ? 0. : out.placements / out.seconds;
	out.score = meta.score;
	out.apm = attack::apm(out.attack, out.seconds);
	out.vs = attack::vs_score(out.attack, meta.downstack, out.seconds);
	return out;
}

void Recorder::begin (const Meta& meta) {
	meta_ = meta;
	placements_.clear();
}

std::optional<Replay> Recorder::finish (
	int score, int lines, int downstack, double seconds) {
	if (static_cast<int>(placements_.size()) < kMinPlacements) {
		return std::nullopt;
	}
	Replay replay;
	replay.meta = meta_;
	replay.meta.score = score;
	replay.meta.lines = lines;
	replay.meta.downstack = downstack;
	replay.meta.seconds = std::round(seconds * 100.) / 100.;
	replay.placements = placements_;
	return replay;
}

std::string folder (const std::string& root) {
	if (const char* forced = std::getenv("FORCETRIS_REPLAYS")) {
		return forced;
	}
	return (fs::path(root) / "data" / "replays").string();
}

std::optional<Replay> load (const std::string& path) {
	std::ifstream source(path);
	if (!source) {
		return std::nullopt;
	}
	json data = json::parse(source, nullptr, false);
	if (data.is_discarded() || !data.is_object()) {
		return std::nullopt;
	}
	// A file from the future is declined outright: there is no telling what
	// its fields mean.
	const auto version = data.find("format");
	if (version == data.end() || !version->is_number_integer()
		|| version->get<int>() < kMinFormat || version->get<int>() > kFormat) {
		return std::nullopt;
	}
	const auto rows = data.find("placements");
	if (rows == data.end() || !rows->is_array()) {
		return std::nullopt;
	}
	Replay replay;
	replay.meta = read_meta(data.value("meta", json::object()));
	for (const json& row : *rows) {
		if (row.is_object()) {
			replay.placements.push_back(read_placement(row));
		}
	}
	replay.path = path;
	return replay;
}

bool save (Replay& replay, const std::string& folder) {
	// Best effort, like every other write in the game: a replay that cannot
	// be stored is not a reason to take the game down the moment it ends.
	std::error_code ignored;
	fs::create_directories(folder, ignored);
	const std::string path = (fs::path(folder) / filename(replay)).string();
	std::ofstream out(path);
	if (!out) {
		return false;
	}
	json data;
	data["format"] = kFormat;
	data["meta"] = write_meta(replay.meta);
	json placements = json::array();
	for (const Placement& place : replay.placements) {
		placements.push_back(write_placement(place));
	}
	data["placements"] = placements;
	out << data.dump();
	if (!out.good()) {
		return false;
	}
	out.close();
	replay.path = path;
	prune(folder);
	return true;
}

std::vector<Replay> listing (const std::string& folder) {
	std::vector<std::string> names;
	std::error_code ignored;
	for (const auto& entry : fs::directory_iterator(folder, ignored)) {
		const std::string name = entry.path().filename().string();
		if (name.size() > 5 && name.substr(name.size() - 5) == ".json") {
			names.push_back(entry.path().string());
		}
	}
	std::sort(names.begin(), names.end(), std::greater<>());
	std::vector<Replay> found;
	for (const std::string& path : names) {
		if (auto loaded = load(path)) {
			found.push_back(std::move(*loaded));
		}
	}
	return found;
}

} // namespace replay
} // namespace forcetris
