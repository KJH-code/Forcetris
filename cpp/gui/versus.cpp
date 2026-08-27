#include "versus.hpp"

#include "forcetris/temper.hpp"

namespace forcetris {
namespace gui {

void VersusMatch::begin_round (unsigned seed, const replay::Meta& player_meta,
                               const SimConfig& bot_base,
                               const std::vector<std::string>& blade) {
	// The bot plays under its own base rules - the player's config in a
	// plain duel, the stage's bare terms for a campaign boss (never the
	// player's Anvil metal) - with its blade forged in *before* the
	// trainer's chrome comes off, so the belt below still guarantees the
	// planner the naive clears it searches with. No flat forced drop
	// slamming its slow ranks (the fuse, when on, burns for both sides
	// alike - the driver types inside it), no finesse retry handing its
	// pieces back, instant soft drop so a planned sonic drop is one press,
	// and DAS parked out of reach of its tap pairs.
	SimConfig config = temper::tempered(bot_base, blade);
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.cleartype = 0;   // The belt: blades never carry collapse either.
	// Its recording carries the same stamp and rules, with the handling it
	// actually played under and the blade written down the way a drafted
	// build would have been - so an embedded bot side analyses truthfully.
	replay::Meta meta = player_meta;
	meta.gametype = player_meta.gametype;
	meta.forced_delay = config.forced_delay;
	meta.finesse = config.finesse_rule;
	meta.cleartype = config.cleartype;
	meta.das = config.das_ms;
	meta.sdf = config.sdf;
	meta.tempers = blade;
	bot.emplace(config, seed, meta);
	driver.emplace(seed, bot::ranks()[rank_index]);
	bot_tempers = blade;
	phase = Phase::Playing;
	phase_frames = 0;
	round_player_won = false;
	round_draw = false;
}

bool VersusMatch::step (Session& player) {
	if (phase != Phase::Playing || !bot.has_value()) {
		return false;
	}
	const auto event = driver->next(bot->sim());
	if (event.has_value()) {
		bot->key(event->key, event->down);
	}
	const bool bot_alive = bot->step();
	bot->take_cues();   // The bot's sounds stay on its side of the table.
	// Heat pressure, both ways: an Overdrive burning on one board makes
	// the other board's fuse burn faster. Igniting is an attack.
	player.sim_mutable().set_pressure(bot->sim().overdrive());
	bot->sim_mutable().set_pressure(player.sim().overdrive());
	const bool player_alive = !player.over();
	// The wire: both sides' outgoing first, then both deliveries, so this
	// frame's attack cannot cancel against itself in flight.
	const int from_player = player.take_outgoing();
	const int from_bot = bot->take_outgoing();
	if (from_player > 0) {
		bot->receive_attack(from_player);
		wire_to_bot += from_player;
	}
	if (from_bot > 0) {
		player.receive_attack(from_bot);
		wire_to_player += from_bot;
	}
	if (player_alive && bot_alive) {
		return true;
	}
	// The round is over. Both toppling on the same tick is a draw: no win
	// counted, the round replayed.
	round_draw = !player_alive && !bot_alive;
	round_player_won = player_alive && !bot_alive;
	if (!round_draw) {
		(round_player_won ? player_wins : bot_wins) += 1;
	}
	phase = (player_wins >= first_to || bot_wins >= first_to)
		? Phase::MatchOver : Phase::RoundOver;
	phase_frames = 0;
	return false;
}

} // namespace gui
} // namespace forcetris
