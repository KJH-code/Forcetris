#include "versus.hpp"

#include "forcetris/temper.hpp"

namespace forcetris {
namespace gui {

namespace {

// Two seconds of warning before every skill lands.
constexpr long kTelegraph = 100;

// The right-hand column, where the Gatekeeper's seal falls.
constexpr int kSealMask = 1 << (kWidth - 1);

} // namespace

void VersusMatch::arm_skills (const std::string& stage_id) {
	skills.clear();
	const auto add = [this] (const char* id, const char* warning,
			long period_s, long duration_s) {
		Skill skill;
		skill.id = id;
		skill.warning = warning;
		skill.period = period_s * 50;
		skill.duration = duration_s * 50;
		skill.next_fire = skill.period;
		skills.push_back(std::move(skill));
	};
	// The kit reads the room: a miniboss carries one trick, a boss two.
	if (stage_id == "c1m1") {
		add("rustfall", "RUST ON THE WIND", 16, 0);
	} else if (stage_id == "c1s8") {
		add("rustfall", "RUST ON THE WIND", 14, 0);
		add("sealgate", "THE GATE SWINGS SHUT", 27, 8);
	} else if (stage_id == "c2m1") {
		add("coldsnap", "COLD SNAP", 21, 10);
	} else if (stage_id == "c2s8") {
		add("coldsnap", "COLD SNAP", 19, 9);
		add("heatwave", "HEAT WAVE", 28, 6);
	} else if (stage_id == "c3m1") {
		add("sealgate", "THE VAULT SEALS", 20, 8);
	} else if (stage_id == "c3s9") {
		// The final boss alone carries three - the one fight allowed to
		// break the one-per-miniboss, two-per-boss rule, because it is
		// the road's whole curriculum turned hostile.
		add("coldsnap", "COLD SNAP", 17, 8);
		add("heatwave", "HEAT WAVE", 24, 6);
		add("rustfall", "RUST ON THE WIND", 13, 0);
	}
}

void VersusMatch::tick_skills (Session& player) {
	if (skills.empty()) {
		return;
	}
	const long frame = player.sim().frame();
	bool want_cold = false;
	bool want_seal = false;
	bool want_pressure = false;
	for (Skill& skill : skills) {
		if (!skill.telegraphing && frame >= skill.next_fire - kTelegraph) {
			skill.telegraphing = true;
			skill_banner = skill.warning;
			skill_banner_until = skill.next_fire + 50;
			skill_cues.push_back("fusewarn");
		}
		if (skill.telegraphing && frame >= skill.next_fire) {
			skill.telegraphing = false;
			skill.active_until = frame + skill.duration;
			skill.next_fire += skill.period;
			if (skill.id == "rustfall") {
				// Two rows of rust thrown at the player's floor, riding
				// the same pending-garbage rail an attack does.
				player.receive_attack(2);
				skill_cues.push_back("hit");
			} else if (skill.id == "coldsnap") {
				skill_cues.push_back("freeze");
			} else if (skill.id == "sealgate") {
				skill_cues.push_back("forced");
			} else if (skill.id == "heatwave") {
				skill_cues.push_back("pressure");
			}
		}
		if (frame < skill.active_until) {
			want_cold = want_cold || skill.id == "coldsnap";
			want_seal = want_seal || skill.id == "sealgate";
			want_pressure = want_pressure || skill.id == "heatwave";
		}
	}
	skill_pressure = want_pressure;
	// The seal never falls on a piece that stands in the column: it waits,
	// still wanted, until the piece is clear - and lifts the moment its
	// spell ends.
	int seal = imposed_sealed;
	if (want_seal && imposed_sealed == 0) {
		bool clear_of = true;
		if (player.sim().entry()) {
			for (const Offset cell : cells_of(player.sim().piece())) {
				if (kSealMask >> cell.x & 1) {
					clear_of = false;
					break;
				}
			}
		}
		if (clear_of) {
			seal = kSealMask;
		}
	} else if (!want_seal) {
		seal = 0;
	}
	if (seal != imposed_sealed || want_cold != imposed_cold) {
		imposed_sealed = seal;
		imposed_cold = want_cold;
		player.sim_mutable().impose_gimmick(imposed_sealed, imposed_cold);
	}
}

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
	// A raid walks its rank list one foe per round, each carrying its own
	// rank's standard blade; a plain match keeps the rank and blade it
	// was given.
	if (raid()) {
		const size_t foe = std::min(static_cast<size_t>(round - 1),
			raid_ranks.size() - 1);
		rank_index = raid_ranks[foe];
	}
	const std::vector<std::string> worn
		= raid() ? temper::blade_for(rank_index) : blade;
	SimConfig config = temper::tempered(bot_base, worn);
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
	meta.tempers = worn;
	bot.emplace(config, seed, meta);
	driver.emplace(seed, bot::ranks()[rank_index]);
	bot_tempers = worn;
	phase = Phase::Playing;
	phase_frames = 0;
	round_player_won = false;
	round_draw = false;
	// The skills re-arm with the round: the player's board is fresh, its
	// clock back at zero, and nothing imposed survives into it.
	for (Skill& skill : skills) {
		skill.next_fire = skill.period;
		skill.active_until = -1;
		skill.telegraphing = false;
	}
	skill_pressure = false;
	imposed_sealed = 0;
	imposed_cold = false;
	skill_banner.clear();
	skill_banner_until = -1;
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
	// The boss's skills run on the player's clock, before the pressure
	// line reads what they want.
	tick_skills(player);
	// Heat pressure, both ways: an Overdrive burning on one board makes
	// the other board's fuse burn faster - and a heat wave holds it hot
	// from the skill side. Igniting is an attack.
	player.sim_mutable().set_pressure(
		bot->sim().overdrive() || skill_pressure);
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
	// A raid ends on the player's first fall; a match runs to first_to
	// both ways.
	phase = (player_wins >= first_to
			|| bot_wins >= (raid() ? 1 : first_to))
		? Phase::MatchOver : Phase::RoundOver;
	phase_frames = 0;
	return false;
}

} // namespace gui
} // namespace forcetris
