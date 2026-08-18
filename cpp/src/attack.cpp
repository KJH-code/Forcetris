#include "forcetris/attack.hpp"

#include <algorithm>

namespace forcetris {
namespace attack {
namespace {

// The tables, exactly as engine/attack.py holds them. The equivalence test
// compares every entry, so a change on either side that is not made on the
// other fails the build rather than quietly forking the numbers.
constexpr int kBase[] = {0, 0, 1, 2, 4};
constexpr int kSpin[] = {0, 2, 4, 6, 10};
constexpr int kMini[] = {0, 0, 1, 2, 4};
constexpr int kCombo[] = {0, 0, 1, 1, 1, 2, 2, 3, 3, 4, 4, 4, 5};
constexpr int kComboLast = static_cast<int>(sizeof(kCombo) / sizeof(kCombo[0])) - 1;
constexpr int kB2b = 1;
constexpr int kPerfect = 10;

} // namespace

int attack_for (int lines, SpinKind spin, bool b2b, int combo, bool perfect) {
	if (lines <= 0) {
		// A placement that cleared nothing sends nothing, spin or not.
		return 0;
	}
	// Cascade clearing can take more than four rows in one placement; the table
	// tops out at a quad rather than inventing numbers TETR.IO never defined.
	const int step = std::min(lines, 4);
	const int* table = spin == SPIN_FULL ? kSpin : spin == SPIN_MINI ? kMini : kBase;
	int sent = table[step];
	if (b2b) {
		sent += kB2b;
	}
	sent += kCombo[std::clamp(combo, 0, kComboLast)];
	if (perfect) {
		sent += kPerfect;
	}
	return sent;
}

double apm (int attack, double seconds) {
	return seconds <= 0. ? 0. : attack * 60. / seconds;
}

double vs_score (int attack, int downstack, double seconds) {
	return seconds <= 0. ? 0. : (attack + downstack) * 100. / seconds;
}

} // namespace attack
} // namespace forcetris
