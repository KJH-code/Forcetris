// The other board: a bot playing its own graded sim, the wire between the
// two, and the match around the rounds.
//
// The player's session stays exactly where it always was (app.session);
// this owns only the opponent - a second Session, so the bot's bag and
// garbage holes are dealt the same way the player's are - and the driver
// typing into it, plus the first-to-N bookkeeping.
#pragma once

#include <memory>
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

	// One foe in the room. A duel has one; a raid has a roomful, all of
	// them playing at once the way a multiplayer lobby does rather than
	// queueing up to be fought one at a time.
	struct Foe {
		Session sim;
		bot::Driver driver;
		int rank_index = 0;
		bool down = false;      // Topped out; its board stops where it is.
		std::vector<std::string> blade;
		Foe (const SimConfig& config, unsigned seed,
			const replay::Meta& meta, int rank,
			std::vector<std::string> worn)
			: sim(config, seed, meta),
			  driver(seed, bot::ranks()[static_cast<size_t>(rank)]),
			  rank_index(rank), blade(std::move(worn)) {}
	};
	// Held by pointer because a Session is not a thing to shuffle about in
	// a vector while three of them are mid-game.
	std::vector<std::unique_ptr<Foe>> foes;

	// Which foe the player's garbage is aimed at. A room of three is only
	// a strategy if the player chooses who to bury first, so the target is
	// theirs to move; it steps past anyone already down on its own.
	int target = 0;

	bool armed () const { return !foes.empty(); }
	Foe& lead () { return *foes.front(); }
	const Foe& lead () const { return *foes.front(); }
	// How many are still playing, and the one taking the player's fire.
	int standing () const;
	Foe* aimed ();
	const Foe* aimed () const;
	// Move the aim to the next foe still standing, wrapping. A no-op when
	// there is only one thing in the room to hit.
	void aim_next ();

	// The boss's skills: telegraphed, periodic, armed only for campaign
	// bosses and minibosses (arm_skills below). Each one warns over the
	// player's board, then lands, and the timed ones lift themselves when
	// their spell runs out. Every decision a skill makes is a field on
	// this struct rather than a reach into the screen, so the whole system
	// is graded headlessly and only the pixels are not.
	struct Skill {
		std::string id;
		const char* warning = "";  // What the telegraph shouts.
		long period = 0;           // Frames between firings.
		long duration = 0;         // Frames the effect holds; 0 = instant.
		// How long the warning runs before the blow. The heavy ones take
		// longer to wind up, and the wind-up is the dread.
		long telegraph = 100;
		long next_fire = 0;
		long active_until = -1;
		long landed_at = -1;       // The frame it struck; -1 for never.
		bool telegraphing = false;
	};
	std::vector<Skill> skills;

	// What the skills are doing to the player's board this frame, read by
	// the screen. An imposition is a thing done TO you for a while - the
	// dark, the smoke, the weight, the barred tongs - and only one may be
	// live at a time, or a fat kit stacks into an unreadable soup.
	bool imposed_dark = false;
	bool imposed_fog = false;
	bool imposed_hold_bar = false;
	int imposed_gravity = 0;        // Frames per row; 0 = leave it alone.
	int imposed_weight = 0;         // What the sim was last told, so the
	                                // tick only reaches in on a change.

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
	// Which skill is announcing itself, and the two frames the plate
	// counts between - so the screen can draw a wind-up that actually
	// reaches its end as the blow lands.
	std::string skill_caster;
	long skill_warned_at = -1;
	long skill_fires_at = -1;
	// The last stretch of the wind-up, in frames: the screen spends it
	// flying the blow across from the foe's well to the player's, so the
	// thing that hits you is the thing you watched coming. Presentation
	// only - the effect still lands on `skill_fires_at` to the frame.
	static constexpr long kFlight = 20;
	// Who the player is fighting, for the plate's upper line and for every
	// place the screen used to print a league letter. Set by the screen
	// from the stage it launched; a Training Yard duel leaves it empty and
	// the screen falls back to naming the bot.
	std::string foe_name;

	// The foe named the way the player should read it: the strength as a
	// word in front of the name, never a league letter. `foe_name` when
	// the road supplied one, "the Bot" when it did not.
	std::string foe_title () const;
	// Sound cues the skills fired this tick, drained by the frame the way
	// a session's cues are.
	std::vector<std::string> skill_cues;

	// Arm the boss's kit for a campaign stage id; anything unknown - a
	// plain duel, the daily - arms nothing.
	void arm_skills (const std::string& stage_id);

	// What a blow off this kit is worth. One number, set at the door from
	// the fire the player chose (campaign::skill_scale), and applied to
	// every row a skill throws, every share of the gauge it takes and
	// every second it holds a gimmick down. The recipe writes the shape of
	// a boss; this writes how hard it hits. Left at one by the trainer's
	// own duels, which have no fire to read.
	double skill_scale = 1.0;

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
