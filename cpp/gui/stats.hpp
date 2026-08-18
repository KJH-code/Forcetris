// The stats a player can pin next to the board, by name.
//
// Each stat is a label and a way to read its value out of the session. The
// settings screen offers exactly this list, so adding an entry here is all it
// takes to make a new figure available to the layout.
#pragma once

#include <string>
#include <vector>

#include "session.hpp"

namespace forcetris {
namespace gui {

struct StatDef {
	const char* id;        // The key the config file stores.
	const char* label;     // What the panel and the settings screen show.
	std::string (*value) (const Session& session);
};

const std::vector<StatDef>& all_stats ();

} // namespace gui
} // namespace forcetris
