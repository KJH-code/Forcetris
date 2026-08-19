#include "forcetris/hiscore.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <vector>

namespace forcetris {
namespace hiscore {

namespace fs = std::filesystem;

namespace {

void put_be (std::string& out, std::uint64_t value, int bytes) {
	for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
		out.push_back(static_cast<char>((value >> shift) & 0xFF));
	}
}

std::uint64_t get_be (const std::string& data, size_t at, int bytes) {
	std::uint64_t value = 0;
	for (int i = 0; i < bytes; ++i) {
		value = (value << 8) | static_cast<unsigned char>(data[at + i]);
	}
	return value;
}

std::string packed (const Tables& tables) {
	std::string out;
	out.reserve(kFileBytes);
	for (const Table& table : tables) {
		for (const Entry& entry : table) {
			out.append(entry.name.data(), entry.name.size());
			put_be(out, entry.score, 8);
			put_be(out, entry.lines, 4);
			put_be(out, entry.timer, 4);
		}
	}
	return out;
}

bool unpack (const std::string& data, Tables& tables) {
	if (data.size() != kFileBytes) {
		return false;
	}
	size_t at = 0;
	for (Table& table : tables) {
		for (Entry& entry : table) {
			std::copy(data.begin() + at, data.begin() + at + 8, entry.name.begin());
			entry.score = get_be(data, at + 8, 8);
			entry.lines = static_cast<std::uint32_t>(get_be(data, at + 16, 4));
			entry.timer = static_cast<std::uint32_t>(get_be(data, at + 20, 4));
			at += kRecordBytes;
		}
	}
	return true;
}

bool read_file (const fs::path& path, std::string& data) {
	std::ifstream source(path, std::ios::binary);
	if (!source) {
		return false;
	}
	data.assign(std::istreambuf_iterator<char>(source),
		std::istreambuf_iterator<char>());
	return true;
}

bool write_file (const fs::path& path, const std::string& data) {
	std::error_code ignored;
	fs::create_directories(path.parent_path(), ignored);
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	if (!out) {
		return false;
	}
	out.write(data.data(), static_cast<std::streamsize>(data.size()));
	return out.good();
}

fs::path data_path (const std::string& folder) {
	return fs::path(folder) / "hiscore.dat";
}

fs::path back_path (const std::string& folder) {
	return fs::path(folder) / "back" / "hiscore.bak";
}

} // namespace

int table_for (const std::string& gametype) {
	if (gametype == "arcade") {
		return 0;
	}
	if (gametype == "timed") {
		return 1;
	}
	return 2;
}

bool outranks (const Entry& a, const Entry& b) {
	// The engine's key: (-score, timer, lines) ascending. Fewer lines really
	// does rank better; the comment in filehandler.py says it on purpose.
	if (a.score != b.score) {
		return a.score > b.score;
	}
	if (a.timer != b.timer) {
		return a.timer < b.timer;
	}
	return a.lines < b.lines;
}

Tables fresh () {
	Tables tables{};
	const char* father = "Pajitnov";
	for (Table& table : tables) {
		for (Entry& entry : table) {
			std::copy(father, father + 8, entry.name.begin());
		}
	}
	return tables;
}

Tables load (const std::string& folder) {
	// SFH's repair ladder, without its files-open-forever bookkeeping: the
	// data file if it is whole, else the backup, else the defaults.
	Tables tables = fresh();
	std::string data;
	if (read_file(data_path(folder), data) && unpack(data, tables)) {
		return tables;
	}
	if (read_file(back_path(folder), data) && unpack(data, tables)) {
		return tables;
	}
	return tables;
}

bool submit (const std::string& folder, const std::string& gametype, Entry entry) {
	Tables tables = load(folder);
	Table& table = tables[table_for(gametype)];
	// encode: append the newcomer, stable sort, drop the worst of eleven -
	// which ranks a tied newcomer *below* the sitting entries.
	std::vector<Entry> eleven(table.begin(), table.end());
	eleven.push_back(entry);
	std::stable_sort(eleven.begin(), eleven.end(), outranks);
	std::copy(eleven.begin(), eleven.begin() + kPerTable, table.begin());
	const std::string data = packed(tables);
	if (!write_file(data_path(folder), data)) {
		return false;
	}
	write_file(back_path(folder), data);
	return true;
}

int place (const Tables& tables, const std::string& gametype, const Entry& probe) {
	// The announcement's arithmetic: bisect_left over the sorted table, which
	// ranks a tied newcomer *above* the sitting entries - one place kinder
	// than submit will actually be. Both halves are the game's.
	const Table& table = tables[table_for(gametype)];
	std::vector<Entry> sorted(table.begin(), table.end());
	std::stable_sort(sorted.begin(), sorted.end(), outranks);
	int at = 0;
	for (const Entry& sitting : sorted) {
		if (outranks(sitting, probe)) {
			++at;
		} else {
			break;
		}
	}
	return at;
}

std::string folder (const std::string& root) {
	if (const char* forced = std::getenv("FORCETRIS_HISCORE")) {
		return forced;
	}
	return (fs::path(root) / "data").string();
}

std::string shown_name (const Entry& entry) {
	std::string name(entry.name.begin(), entry.name.end());
	while (!name.empty() && name.back() == ' ') {
		name.pop_back();
	}
	return name;
}

std::string shown_timer (std::uint32_t centiseconds) {
	char text[32];
	std::snprintf(text, sizeof text, "%u:%02u:%02u",
		centiseconds / 6000, centiseconds / 100 % 60, centiseconds % 100);
	return text;
}

} // namespace hiscore
} // namespace forcetris
