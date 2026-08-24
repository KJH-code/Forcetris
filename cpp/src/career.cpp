#include "forcetris/career.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace forcetris {
namespace career {

namespace fs = std::filesystem;

std::string path (const std::string& root) {
	if (const char* forced = std::getenv("FORCETRIS_CAREER")) {
		return forced;
	}
	return (fs::path(root) / "data" / "career.dat").string();
}

State load (const std::string& path) {
	State state;
	std::ifstream source(path);
	if (!source) {
		return state;
	}
	std::string line;
	while (std::getline(source, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream in(line);
		std::string key;
		in >> key;
		if (key == "stage") {
			std::string rank;
			int stars = 0;
			in >> rank >> stars;
			if (!rank.empty() && !in.fail()) {
				state.stars[rank] = std::clamp(stars, 0, 3);
			}
		} else if (key == "daily") {
			std::string date;
			long long score = -1;
			in >> date >> score;
			if (!date.empty()) {
				state.daily_date = date;
				state.daily_score = in.fail() ? -1 : score;
			}
		} else {
			state.unknown.push_back(line);
		}
	}
	return state;
}

bool save (const std::string& path, const State& state) {
	std::error_code ignored;
	fs::create_directories(fs::path(path).parent_path(), ignored);
	std::ofstream out(path, std::ios::trunc);
	if (!out) {
		return false;
	}
	out << "# forcetris career 1\n";
	for (const auto& [rank, stars] : state.stars) {
		out << "stage " << rank << " " << stars << "\n";
	}
	if (!state.daily_date.empty()) {
		out << "daily " << state.daily_date << " " << state.daily_score
			<< "\n";
	}
	for (const std::string& line : state.unknown) {
		out << line << "\n";
	}
	return out.good();
}

bool open (const State& state, const std::vector<std::string>& ladder,
		size_t stage) {
	if (stage >= ladder.size()) {
		return false;
	}
	if (stage == 0) {
		return true;
	}
	const auto before = state.stars.find(ladder[stage - 1]);
	return before != state.stars.end() && before->second > 0;
}

} // namespace career
} // namespace forcetris
