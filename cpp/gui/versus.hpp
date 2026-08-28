// The other board: a bot playing its own graded sim, the wire between the
// two, and the match around the rounds.
//
// The player's session stays exactly where it always was (app.session);
// this owns only the opponent - a second Session, so the bot's bag and
// garbage holes are dealt the same way the player's are - and the driver
// typing into it, plus the first-to-N bookkeeping.
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "forcetris/bot.hpp"
#include "forcetris/temper.hpp"
#include "session.hpp"

namespace forcetris {
namespace gui {

struct VersusMatch {
	enum class Phase { Playing, RoundOver, MatchOver };

	int rank_index = 4;
	int first_to = 1;
	int player_wins = 0;
	int bot_wins = 0;
	// What crossed the wire this frame, for the streaks the GUI draws:
	// read and zeroed by the frame that draws them.
	int wire_to_bot = 0;
	int wire_to_player = 0;
	int round = 1;
	Phase phase = Phase::Playing;
	long phase_frames = 0;         // Frames spent showing the round's end.
	bool round_player_won = false;
	bool round_draw = false;

	std::optional<Session> bot;
	std::optional<bot::Driver> driver;

	// The boss's skills: telegraphed, periodic, armed only for campaign
	// bosses (arm_skills below). Each one warns for two seconds over the
	// player's board, then lands - rust thrown as garbage, a column
	// sealed, the iron cold-snapped, a heat wave on the fuse - and the
	// timed ones lift themselves when their spell runs out.
	struct Skill {
		std::string id;            // rustfall / sealgate / coldsnap / heatwave
		const char* warning = "";  // What the telegraph shouts.
		long period = 0;           // Frames between firings.
		long duration = 0;         // Frames the effect holds; 0 = instant.
		long next_fire = 0;
		long active_until = -1;
		bool telegraphing = false;
	};
	std::vector<Skill> skills;

	// A raid: a gauntlet of lesser foes fought back to back in one node.
	// The rank list is walked one round per foe, each armed with its
	// rank's standard blade; the player must down them all (first_to is
	// the gauntlet's length) and a single loss ends the raid - the bot
	// side needs one win, never first_to.
	std::vector<int> raid_ranks;
	bool raid () const { return !raid_ranks.empty(); }
	bool skill_pressure = false;    // A heat wave holding the fuse hot.
	// The gimmicks currently imposed on the player's board, so the tick
	// only calls into the sim when the wanted state changes. The seal
	// waits for the falling piece to leave the column before it lands.
	int imposed_sealed = 0;
	bool imposed_cold = false;
	std::string skill_banner;       // Drawn over the player's board...
	long skill_banner_until = -1;   // ...until this player-sim frame.
	// Sound cues the skills fired this tick, drained by the frame the way
	// a session's cues are.
	std::vector<std::string> skill_cues;

	// Arm the boss's kit for a campaign stage id; anything unknown - a
	// plain duel, the daily - arms nothing.
	void arm_skills (const std::string& stage_id);

	// The blade the bot carries: a fixed build applied to its rules at
	// round start rather than drafted mid-round - a duel is real time, and
	// the freeze that lets a hand read cards has no business in one. Kept
	// here so the panel can print it under the bot's board.
	std::vector<std::string> bot_tempers;

	VersusMatch (int rank, int ft) : rank_index(rank), first_to(ft) {}

	// A fresh board and a fresh driver for the next round; the match
	// counters stay. `player_meta` seeds the bot's own recording - same
	// stamp and rules, the handling overridden below. `bot_base` is the
	// rules the BOT plays under before its blade - for a plain duel the
	// player's config, for a campaign boss the stage's terms with none of
	// the player's permanent metal - and `blade` is what it carries in.
	void begin_round (unsigned seed, const replay::Meta& player_meta,
	                  const SimConfig& bot_base,
	                  const std::vector<std::string>& blade);

	// One 20ms tick, called right after the player's session stepped: drive
	// the bot, step it, ferry the attack both ways, run the boss's skills,
	// and notice the round ending. Returns true while the round is live.
	bool step (Session& player);

	// The skills' own slice of the tick: telegraphs, firings, and the
	// timed effects lifting themselves.
	void tick_skills (Session& player);
};

} // namespace gui
} // namespace forcetris
