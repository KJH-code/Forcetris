#include "forcetris/profile.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace forcetris {
namespace profile {

namespace {

namespace fs = std::filesystem;

// The fixed keys a record writes. Anything else in `stats` is written as
// itself, and anything unknown on read lands back in `stats` - so a stat
// added later flows through an older build unharmed.
constexpr const char* kVersion = "1";

std::string number (double value) {
	std::ostringstream out;
	out.precision(6);
	out << value;
	return out.str();
}

} // namespace

std::string path (const std::string& root) {
	if (const char* forced = std::getenv("FORCETRIS_PROFILE")) {
		return forced;
	}
	return (fs::path(root) / "data" / "profile.dat").string();
}

bool append (const std::string& path, const GameRecord& record) {
	std::error_code ignored;
	fs::create_directories(fs::path(path).parent_path(), ignored);
	std::ofstream out(path, std::ios::app);
	if (!out) {
		return false;
	}
	out << "game v=" << kVersion;
	// The identity pair first; spaces inside `played` would split the
	// line, so the stamp keeps its ISO shape (no spaces by construction).
	out << " played=" << record.played;
	out << " mode=" << record.gametype;
	out << " seconds=" << number(record.seconds);
	out << " pieces=" << record.pieces;
	out << " lines=" << record.lines;
	out << " score=" << record.score;
	out << " attack=" << record.attack;
	out << " downstack=" << record.downstack;
	out << " pps=" << number(record.pps);
	out << " apm=" << number(record.apm);
	out << " vs=" << number(record.vs);
	out << " finesse=" << number(record.finesse);
	if (record.tr >= 0.) {
		out << " tr=" << number(record.tr);
	}
	if (record.won >= 0) {
		out << " won=" << record.won;
	}
	for (const auto& [key, value] : record.stats) {
		out << " " << key << "=" << number(value);
	}
	out << "\n";
	return out.good();
}

std::vector<GameRecord> load (const std::string& path) {
	std::vector<GameRecord> records;
	std::ifstream source(path);
	if (!source) {
		return records;
	}
	std::string line;
	while (std::getline(source, line)) {
		std::istringstream in(line);
		std::string word;
		in >> word;
		if (word != "game") {
			continue;
		}
		GameRecord record;
		bool stamped = false;
		while (in >> word) {
			const size_t eq = word.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const std::string key = word.substr(0, eq);
			const std::string value = word.substr(eq + 1);
			try {
				if (key == "v") { /* format version, currently one */ }
				else if (key == "played") { record.played = value; stamped = true; }
				else if (key == "mode") { record.gametype = value; }
				else if (key == "seconds") { record.seconds = std::stod(value); }
				else if (key == "pieces") { record.pieces = std::stoi(value); }
				else if (key == "lines") { record.lines = std::stoi(value); }
				else if (key == "score") { record.score = std::stoll(value); }
				else if (key == "attack") { record.attack = std::stoi(value); }
				else if (key == "downstack") { record.downstack = std::stoi(value); }
				else if (key == "pps") { record.pps = std::stod(value); }
				else if (key == "apm") { record.apm = std::stod(value); }
				else if (key == "vs") { record.vs = std::stod(value); }
				else if (key == "finesse") { record.finesse = std::stod(value); }
				else if (key == "tr") { record.tr = std::stod(value); }
				else if (key == "won") { record.won = std::stoi(value); }
				else { record.stats[key] = std::stod(value); }
			} catch (...) {
				// One spoiled pair does not spoil the line.
			}
		}
		if (stamped) {
			records.push_back(std::move(record));
		}
	}
	return records;
}

} // namespace profile
} // namespace forcetris
