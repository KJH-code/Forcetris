#include "versus.hpp"

namespace forcetris {
namespace gui {

void VersusMatch::begin_round (const SimConfig& player_config, unsigned seed,
                               const replay::Meta& player_meta) {
	// The bot plays under the same rules, minus the trainer's chrome: no
	// flat forced drop slamming its slow ranks (the fuse, when on, burns
	// for both sides alike - the driver types inside it), no finesse retry
	// handing its pieces back, instant soft drop so a planned sonic drop
	// is one press, and DAS parked out of reach of its tap pairs.
	SimConfig config = player_config;
	config.forced_delay = 0.;
	config.finesse_rule = 0;
	config.sdf = 40;
	config.das_ms = 330;
	config.cleartype = 0;
	// Its recording carries the same stamp and rules, with the handling it
	// actually played under - so an embedded bot side analyses truthfully.
	replay::Meta meta = player_meta;
	meta.gametype = player_meta.gametype;
	meta.forced_delay = config.forced_delay;
	meta.finesse = config.finesse_rule;
	meta.cleartype = config.cleartype;
	meta.das = config.das_ms;
	meta.sdf = config.sdf;
	bot.emplace(config, seed, meta);
	driver.emplace(seed, bot::ranks()[rank_index]);
	// The bot's draft starts over with the board: its build is captured
	// from the config it *actually* plays under - overrides included - so
	// a rebuilt rule set can never hand the planner a clearing style it
	// was not searching with.
	bot_start = config;
	bot_tempers.clear();
	bot_heat = 0;
	bot_seed = seed;
	bot_rng.seed(seed ^ 0x74656d70u);
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
	// The bot's side of the forge: when its counter crosses a heat it is
	// dealt the same three cards a player would see and takes one at once -
	// no freeze on its side, because the freeze exists for a hand, not a
	// planner. The pick itself is the core's, rank-tempered, and it never
	// takes Collapse; a heat with nothing acceptable passes by bare.
	if (bot->sim().config().fuse) {
		const int forged = temper::heats_done(bot->sim().lines_cleared(),
			bot->sim().downstack(), false);
		if (forged > bot_heat) {
			const std::vector<std::string> cards
				= temper::offer(bot_seed, bot_heat, bot_tempers);
			const int at = temper::bot_pick(cards, rank_index, bot_rng);
			if (at >= 0) {
				bot_tempers.push_back(cards[static_cast<size_t>(at)]);
				bot->draft(temper::tempered(bot_start, bot_tempers),
					bot_tempers.back());
			}
			++bot_heat;
		}
	}
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
