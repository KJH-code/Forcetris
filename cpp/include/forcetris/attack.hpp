// The attack numbers: how much garbage a placement would send.
//
// A port of engine/attack.py, graded against it entry by entry. The figures are
// TETR.IO's, because APM and VS - the rates the stat panel exists to show - are
// defined in terms of them.
#pragma once

namespace forcetris {
namespace attack {

enum SpinKind : int { NOT_SPIN = 0, SPIN_MINI = 1, SPIN_FULL = 2 };

// Garbage sent by one placement. `combo` is the count as shown on the HUD, so
// the first clear of a run is 0. `b2b` is whether this clear extended a back to
// back run - the caller decides, since only it knows the chain.
int attack_for (int lines, SpinKind spin = NOT_SPIN, bool b2b = false,
                int combo = 0, bool perfect = false);

// Attack per minute, and the VS score: attack plus garbage dug out, per
// hundred seconds.
double apm (int attack, double seconds);
double vs_score (int attack, int downstack, double seconds);

} // namespace attack
} // namespace forcetris
