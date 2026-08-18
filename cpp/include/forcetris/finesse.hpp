// Finesse: the fewest key presses a placement could have been made in.
//
// A port of engine/finesse.py. The minimums are searched out from the spawn
// position over an empty field rather than hard-coded, which is what the
// published tables are, and it keeps the two implementations derivable from the
// same rules instead of from the same list of numbers.
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "forcetris/piece.hpp"

namespace forcetris {
namespace finesse {

// The presses a route is made of, in the order the search prefers them when two
// routes cost the same: a held key before a tap, a single turn before a 180.
enum class Move : int {
	DasLeft, DasRight, Left, Right, Cw, Ccw, Flip,
};

// The names the Python side uses, so a route can be compared across the two.
const char* name (Move move);
std::optional<Move> from_name (const std::string& text);
// What the screen calls it.
const char* label (Move move);

using Route = std::vector<Move>;

// True if the piece sits inside the walls at this column. Only the walls are
// tested: finesse is measured on an empty field by definition.
bool fits (int form, int state, int x);

// What a placement *is*, as opposed to how the piece is being held. Two ways of
// holding a piece that cover the same cells are one placement, which is what
// folds the duplicate orientations of I, S, Z and O together.
using Placement = std::vector<Offset>;
Placement placement (int form, int state, int x);

// Cheapest route from spawn to every placement a piece can be dropped in.
const std::map<Placement, Route>& table (int form);

// The presses this placement should have taken, or nothing if it is off the
// table - which is the same set of placements that go unjudged.
std::optional<Route> route (int form, int state, int x);

// Fewest presses it could have been made in.
std::optional<int> optimal (int form, int state, int x);

// Walk a list of presses from spawn and say where the piece is after each. A
// press the walls refuse leaves the piece where it was, so the result is always
// as long as the presses that produced it.
std::vector<std::pair<int, int>> follow (int form, const Route& presses);

// A route written out for a person to read.
std::string describe (const Route& presses);

} // namespace finesse
} // namespace forcetris
