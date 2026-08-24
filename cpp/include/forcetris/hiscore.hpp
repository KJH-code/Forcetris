// The high score table: the same data/hiscore.dat the Python game keeps.
//
// A port of engine/filehandler.py's SFH and the flow around it. The file is
// thirty 24-byte big-endian records - ten each for arcade, timed and free -
// with a mirror copy in data/back/hiscore.bak, no header and no obfuscation.
// Names are eight raw bytes, space padded, never NUL-terminated. The timer
// column is in centiseconds of elapsed play.
//
// Two quirks are load-bearing and ported deliberately: the sort ranks score
// down, then time up, then *fewer* lines better; and the qualification probe
// ranks a tied newcomer first while the write ranks it last, so a tie can be
// announced one place higher than it is stored. See the Python side.
#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace forcetris {
namespace hiscore {

constexpr int kPerTable = 10;
constexpr int kTables = 3;          // arcade, timed, free - in file order.
constexpr int kRecordBytes = 24;
constexpr int kFileBytes = kTables * kPerTable * kRecordBytes;

struct Entry {
	std::array<char, 8> name{' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
	std::uint64_t score = 0;
	std::uint32_t lines = 0;
	std::uint32_t timer = 0;        // Centiseconds of elapsed play.
};

using Table = std::array<Entry, kPerTable>;
using Tables = std::array<Table, kTables>;

// 'arcade' is table 0, 'timed' is 1, and anything else is free - the same
// fall-through the Python side applies in both of its copies of the mapping.
int table_for (const std::string& gametype);

// True if `a` outranks `b`: score down, then time up, then fewer lines.
bool outranks (const Entry& a, const Entry& b);

// The default table: thirty rows of Pajitnov with nothing to his name.
Tables fresh ();

// Read the table out of `folder`/hiscore.dat, falling back to the backup and
// then to the defaults, the way SFH repairs itself. Never fails.
Tables load (const std::string& folder);

// Add one entry to its game type's table - the worst of the resulting eleven
// is dropped, which may be the newcomer - and write the whole file plus its
// backup. Returns false only if the file could not be written.
bool submit (const std::string& folder, const std::string& gametype, Entry entry);

// Where the newcomer would place, 0-based, ranking a tie *above* the sitting
// entries as the game's announcement does. kPerTable means it missed.
int place (const Tables& tables, const std::string& gametype, const Entry& probe);

// The variant's own tables: the same record codec in its own file
// (fusescore.dat, mirrored like the SFH one), six tables in file order -
// ignition, blaze, inferno, meltdown, bunker, duel. A fuse-rules score
// never touches the trainer's byte-compatible file, and the trainer's
// never touches this one.
constexpr int kFuseTables = 6;
using FuseTables = std::array<Table, kFuseTables>;

// The table index for a variant gametype name, or -1 for anything else -
// unlike table_for there is no fall-through, because "anything else" here
// means a file that must not be written.
int fuse_table_for (const std::string& gametype);

FuseTables fresh_fuse ();
FuseTables load_fuse (const std::string& folder);
bool submit_fuse (const std::string& folder, const std::string& gametype,
	Entry entry);
int place_fuse (const FuseTables& tables, const std::string& gametype,
	const Entry& probe);

// The folder holding hiscore.dat: FORCETRIS_HISCORE if set, else root/data.
std::string folder (const std::string& root);

// A stored name, trailing spaces trimmed for display.
std::string shown_name (const Entry& entry);

// The timer column as the screens print it: minutes:seconds:centiseconds.
std::string shown_timer (std::uint32_t centiseconds);

} // namespace hiscore
} // namespace forcetris
