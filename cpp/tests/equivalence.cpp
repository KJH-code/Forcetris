// Grade the C++ core against the Python engine.
//
// The Python side is the one with years of tests behind it, so rather than
// writing a second set of assertions by hand this reads what it actually does -
// dumped by tools/dump_reference.py - and checks the port gives the same
// answers. A port that merely looks right is worth nothing; one that agrees
// placement by placement with the implementation being replaced is worth
// something.
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cmath>

#include "forcetris/attack.hpp"
#include "forcetris/board.hpp"
#include "forcetris/spins.hpp"
#include "forcetris/finesse.hpp"
#include "forcetris/kicks.hpp"
#include "forcetris/piece.hpp"

namespace {

using namespace forcetris;

struct Report {
	int checked = 0;
	int failed = 0;
	std::vector<std::string> first;

	void ok () { ++checked; }

	void bad (const std::string& detail) {
		++checked;
		++failed;
		if (first.size() < 5) {
			first.push_back(detail);
		}
	}
};

struct Section {
	std::string name;
	Report report{};
};

std::string join (const finesse::Route& presses) {
	if (presses.empty()) {
		return "-";
	}
	std::string text;
	for (size_t i = 0; i < presses.size(); ++i) {
		if (i != 0) {
			text += ' ';
		}
		text += finesse::name(presses[i]);
	}
	return text;
}

std::string key_of (const finesse::Placement& where) {
	std::string text;
	for (size_t i = 0; i < where.size(); ++i) {
		if (i != 0) {
			text += ',';
		}
		text += std::to_string(where[i].x) + ":" + std::to_string(where[i].y);
	}
	return text;
}

} // namespace

int main (int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "usage: equivalence <reference.txt>\n";
		return 2;
	}
	std::ifstream source(argv[1]);
	if (!source) {
		std::cerr << "cannot read " << argv[1] << "\n";
		return 2;
	}

	Section geometry{"the board and spawn position match"};
	Section shapes{"every piece has the same cells in every orientation"};
	Section counts{"every piece has the same number of placements"};
	Section routes{"every finesse route is the same route"};
	Section rotations{"every rotation lands in the same place"};
	Section drops{"every piece falls to the same row"};
	Section attacks{"every attack table entry sends the same garbage"};
	Section rates{"APM and VS come out to the same numbers"};
	Section spun{"every spin verdict is the same verdict"};

	std::vector<std::string> board_rows;
	std::string board_name;
	std::vector<std::pair<std::string, Board>> boards;
	int rows_wanted = 0;

	const auto board_named = [&boards] (const std::string& want) -> const Board* {
		for (const auto& [name, board] : boards) {
			if (name == want) {
				return &board;
			}
		}
		return nullptr;
	};

	std::string line;
	while (std::getline(source, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream in(line);
		std::string kind;
		in >> kind;

		if (kind == "geometry") {
			int width = 0, height = 0, spawn_x = 0, spawn_y = 0;
			in >> width >> height >> spawn_x >> spawn_y;
			const bool same = width == kWidth && height == kHeight
				&& spawn_x == kSpawnX && spawn_y == kSpawnY;
			if (same) {
				geometry.report.ok();
			} else {
				geometry.report.bad("python " + std::to_string(width) + "x"
					+ std::to_string(height) + " spawn " + std::to_string(spawn_x) + ","
					+ std::to_string(spawn_y));
			}
		} else if (kind == "offsets") {
			int form = 0, state = 0;
			in >> form >> state;
			Cells wanted{};
			for (int i = 0; i < kCells; ++i) {
				in >> wanted[i].x >> wanted[i].y;
			}
			const Cells& got = offsets(form, state);
			if (std::equal(wanted.begin(), wanted.end(), got.begin())) {
				shapes.report.ok();
			} else {
				std::string detail = "form " + std::to_string(form) + " state "
					+ std::to_string(state) + ": got";
				for (const Offset cell : got) {
					detail += " " + std::to_string(cell.x) + "," + std::to_string(cell.y);
				}
				shapes.report.bad(detail);
			}
		} else if (kind == "finesse_count") {
			int form = 0, count = 0;
			in >> form >> count;
			const int got = static_cast<int>(finesse::table(form).size());
			if (got == count) {
				counts.report.ok();
			} else {
				counts.report.bad("form " + std::to_string(form) + ": "
					+ std::to_string(got) + " against " + std::to_string(count));
			}
		} else if (kind == "finesse") {
			int form = 0, cost = 0;
			std::string cells;
			in >> form >> cells >> cost;
			std::string presses;
			std::string word;
			while (in >> word) {
				if (word == "-") {
					continue;
				}
				if (!presses.empty()) {
					presses += ' ';
				}
				presses += word;
			}
			if (presses.empty()) {
				presses = "-";
			}
			// Find the placement by the cells it covers, the same key Python uses.
			bool found = false;
			for (const auto& [where, route] : finesse::table(form)) {
				if (key_of(where) != cells) {
					continue;
				}
				found = true;
				const std::string got = join(route);
				if (got == presses && static_cast<int>(route.size()) == cost) {
					routes.report.ok();
				} else {
					routes.report.bad("form " + std::to_string(form) + " at " + cells
						+ ": got [" + got + "] against [" + presses + "]");
				}
				break;
			}
			if (!found) {
				routes.report.bad("form " + std::to_string(form) + " has no placement "
					+ cells);
			}
		} else if (kind == "board") {
			in >> board_name >> rows_wanted;
			board_rows.clear();
			if (rows_wanted == 0) {
				// An empty board is written as no rows at all, so it has to be
				// registered here rather than waiting for a row that never comes.
				boards.push_back({board_name, Board::from_rows(board_rows)});
			}
		} else if (kind == "row") {
			std::string row;
			in >> row;
			board_rows.push_back(row);
			if (static_cast<int>(board_rows.size()) == rows_wanted) {
				boards.push_back({board_name, Board::from_rows(board_rows)});
			}
		} else if (kind == "rotate") {
			std::string which;
			int kicks = 0, form = 0, state = 0, column = 0, depth = 0, turns = 0;
			int want_state = 0, want_x = 0, want_y = 0;
			in >> which >> kicks >> form >> state >> column >> depth >> turns
			   >> want_state >> want_x >> want_y;
			const Board* board = board_named(which);
			if (board == nullptr) {
				rotations.report.bad("no board called " + which);
				continue;
			}
			const Piece before{form, state, column, depth};
			const Rotation after = rotate(*board, before, turns, kicks != 0);
			const Piece& got = after.piece;
			if (got.state == want_state && got.x == want_x && got.y == want_y) {
				rotations.report.ok();
			} else {
				rotations.report.bad(
					which + " kicks=" + std::to_string(kicks) + " form " + std::to_string(form)
					+ " state " + std::to_string(state) + " at " + std::to_string(column) + ","
					+ std::to_string(depth) + " turns " + std::to_string(turns)
					+ ": got " + std::to_string(got.state) + " @" + std::to_string(got.x) + ","
					+ std::to_string(got.y) + " against " + std::to_string(want_state) + " @"
					+ std::to_string(want_x) + "," + std::to_string(want_y));
			}
		} else if (kind == "attack") {
			int lines = 0, spin = 0, b2b = 0, combo = 0, perfect = 0, sent = 0;
			in >> lines >> spin >> b2b >> combo >> perfect >> sent;
			const int got = attack::attack_for(
				lines, static_cast<attack::SpinKind>(spin), b2b != 0, combo, perfect != 0);
			if (got == sent) {
				attacks.report.ok();
			} else {
				attacks.report.bad("lines " + std::to_string(lines) + " spin "
					+ std::to_string(spin) + " b2b " + std::to_string(b2b) + " combo "
					+ std::to_string(combo) + " pc " + std::to_string(perfect) + ": got "
					+ std::to_string(got) + " against " + std::to_string(sent));
			}
		} else if (kind == "rate") {
			int attack_n = 0, downstack = 0;
			double seconds = 0., want_apm = 0., want_vs = 0.;
			in >> attack_n >> downstack >> seconds >> want_apm >> want_vs;
			const double got_apm = attack::apm(attack_n, seconds);
			const double got_vs = attack::vs_score(attack_n, downstack, seconds);
			if (std::abs(got_apm - want_apm) < 1e-9 && std::abs(got_vs - want_vs) < 1e-9) {
				rates.report.ok();
			} else {
				rates.report.bad("attack " + std::to_string(attack_n) + " over "
					+ std::to_string(seconds) + "s: apm " + std::to_string(got_apm)
					+ " vs " + std::to_string(got_vs));
			}
		} else if (kind == "spin") {
			std::string which, marks;
			int form = 0, state = 0, column = 0, depth = 0;
			in >> which >> form >> state >> column >> depth >> marks;
			const Board* board = board_named(which);
			if (board == nullptr || marks.size() != 8) {
				spun.report.bad("bad record for board " + which);
				continue;
			}
			const Piece piece{form, state, column, depth};
			std::string got;
			for (int rule = 0; rule < 4; ++rule) {
				for (int kicked = 0; kicked < 2; ++kicked) {
					const auto verdict = spins::judge(
						*board, piece, static_cast<spins::Rule>(rule), true, kicked != 0);
					got += !verdict.has_value() ? '-' : verdict->full ? 'F' : 'M';
				}
			}
			if (got == marks) {
				spun.report.ok();
			} else {
				spun.report.bad(which + " form " + std::to_string(form) + " state "
					+ std::to_string(state) + " at " + std::to_string(column) + ","
					+ std::to_string(depth) + ": got " + got + " against " + marks);
			}
		} else if (kind == "drop") {
			std::string which;
			int form = 0, column = 0, want_x = 0, want_y = 0;
			in >> which >> form >> column >> want_x >> want_y;
			const Board* board = board_named(which);
			if (board == nullptr) {
				drops.report.bad("no board called " + which);
				continue;
			}
			const Piece landed = board->dropped(Piece{form, 0, column, kSpawnY});
			if (landed.x == want_x && landed.y == want_y) {
				drops.report.ok();
			} else {
				drops.report.bad(which + " form " + std::to_string(form) + " column "
					+ std::to_string(column) + ": got " + std::to_string(landed.x) + ","
					+ std::to_string(landed.y) + " against " + std::to_string(want_x) + ","
					+ std::to_string(want_y));
			}
		}
	}

	int failed = 0;
	for (const Section* section : {&geometry, &shapes, &counts, &routes, &rotations, &drops, &attacks, &rates, &spun}) {
		const Report& report = section->report;
		const bool good = report.failed == 0 && report.checked > 0;
		std::cout << (good ? "PASS " : "FAIL ") << section->name << " -- "
		          << report.checked << " checked";
		if (report.failed != 0) {
			std::cout << ", " << report.failed << " differ";
		}
		std::cout << "\n";
		for (const std::string& detail : report.first) {
			std::cout << "       " << detail << "\n";
		}
		if (!good) {
			++failed;
		}
	}
	std::cout << "\n";
	if (failed != 0) {
		std::cout << failed << " section(s) disagree with the Python engine.\n";
		return 1;
	}
	std::cout << "The C++ core agrees with the Python engine everywhere it was asked.\n";
	return 0;
}
