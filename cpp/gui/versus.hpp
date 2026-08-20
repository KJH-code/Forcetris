// The other board: a bot playing its own graded sim, the wire between the
// two, and the match around the rounds.
//
// The player's session stays exactly where it always was (app.session);
// this owns only the opponent - a second Session, so the bot's bag and
// garbage holes are dealt the same way the player's are - and the driver
// typing into it, plus the first-to-N bookkeeping.
#pragma once

#include <optional>

#include "forcetris/bot.hpp"
#include "session.hpp"

namespace forcetris {
namespace gui {

struct VersusMatch {
	enum class Phase { Playing, RoundOver, MatchOver };

	int rank_index = 4;
	int first_to = 1;
	int player_wins = 0;
	int bot_wins = 0;
	int round = 1;
	Phase phase = Phase::Playing;
	long phase_frames = 0;         // Frames spent showing the round's end.
	bool round_player_won = false;
	bool round_draw = false;

	std::optional<Session> bot;
	std::optional<bot::Driver> driver;

	VersusMatch (int rank, int ft) : rank_index(rank), first_to(ft) {}

	// A fresh board and a fresh driver for the next round; the match
	// counters stay. `player_meta` seeds the bot's own recording - same
	// stamp and rules, the handling overridden below.
	void begin_round (const SimConfig& player_config, unsigned seed,
	                  const replay::Meta& player_meta);

	// One 20ms tick, called right after the player's session stepped: drive
	// the bot, step it, ferry the attack both ways, and notice the round
	// ending. Returns true while the round is live.
	bool step (Session& player);
};

} // namespace gui
} // namespace forcetris
