#include "versus.hpp"

#include <algorithm>

#include "forcetris/temper.hpp"

namespace forcetris {
namespace gui {

namespace {

// The right-hand column, where the Gatekeeper's seal falls.
constexpr int kSealMask = 1 << (kWidth - 1);

} // namespace

void VersusMatch::arm_skills (const std::string& stage_id) {
	skills.clear();
	const auto add = [this] (const char* id, const char* warning,
			long period_s, long duration_s, long telegraph_s = 2) {
		Skill skill;
		skill.id = id;
		skill.warning = warning;
		skill.period = period_s * 50;
		skill.duration = duration_s * 50;
		skill.telegraph = telegraph_s * 50;
		skill.next_fire = skill.period;
		skills.push_back(std::move(skill));
	};
	// The kits. A Warden miniboss carries two and a Warden boss three; the
	// Hammers carry two heavy ones rather than a handful of fiddly ones,
	// because heavy is their whole character; the Tricksters carry the
	// fiddly ones, three and four. The road's own Warden closes with four.
	// Never darkness and smoke together on one kit - that is not hard, it
	// is unreadable.
	if (stage_id == "c1m1") {
		add("rustfall", "RUST ON THE WIND", 16, 0);
		add("tongslock", "THE TONGS ARE TAKEN", 21, 5);
	} else if (stage_id == "c1s8") {
		add("rustfall", "RUST ON THE WIND", 14, 0);
		add("sealgate", "THE GATE SWINGS SHUT", 27, 8);
		add("vaultdark", "THE LAMPS GO OUT", 30, 7);
	} else if (stage_id == "c2m1") {
		add("coldsnap", "COLD SNAP", 21, 10);
		add("smokescreen", "SMOKE IN THE RAFTERS", 26, 8);
	} else if (stage_id == "c2s8") {
		add("coldsnap", "COLD SNAP", 19, 9);
		add("heatwave", "HEAT WAVE", 24, 6);
		add("pincer", "THE WALLS CLOSE IN", 31, 7);
	} else if (stage_id == "c3m1") {
		add("sealgate", "THE VAULT SEALS", 20, 8);
		add("deadweight", "THE HAMMER FALLS", 28, 6);
	} else if (stage_id == "c3s9") {
		// The road's own Warden closes with four - the one fight allowed
		// to break the three-per-boss rule, because it is the whole
		// curriculum turned hostile.
		add("coldsnap", "COLD SNAP", 17, 8);
		add("heatwave", "HEAT WAVE", 24, 6);
		add("rustfall", "RUST ON THE WIND", 13, 0);
		add("forgestrike", "THE ANVIL RISES", 34, 0, 5);
	// --- The Hammers: two heavy blows, no tricks. ------------------------
	} else if (stage_id == "c1m2") {
		add("deadweight", "THE HAMMER FALLS", 26, 6);
	} else if (stage_id == "c1b2") {
		add("deadweight", "THE HAMMER FALLS", 24, 6);
		add("forgestrike", "THE ANVIL RISES", 36, 0, 5);
	} else if (stage_id == "c2m2") {
		add("deadweight", "THE HAMMER FALLS", 24, 7);
	} else if (stage_id == "c2b2") {
		add("deadweight", "THE HAMMER FALLS", 22, 7);
		add("forgestrike", "THE ANVIL RISES", 33, 0, 5);
	} else if (stage_id == "c3m2") {
		add("deadweight", "THE HAMMER FALLS", 22, 7);
	} else if (stage_id == "c3b2") {
		add("deadweight", "THE HAMMER FALLS", 20, 8);
		add("forgestrike", "THE ANVIL RISES", 30, 0, 5);
	// --- The Tricksters: the fiddly ones, and more of them. --------------
	} else if (stage_id == "c1m3") {
		add("rustfall", "RUST ON THE WIND", 18, 0);
		add("sealgate", "THE LAMP GOES OUT", 30, 7);
		add("tongslock", "THE TONGS ARE TAKEN", 23, 5);
	} else if (stage_id == "c1b3") {
		add("rustfall", "RUST ON THE WIND", 16, 0);
		add("sealgate", "THE GATE SWINGS SHUT", 29, 7);
		add("heatwave", "HEAT WAVE", 34, 5);
		add("vaultdark", "THE LAMPS GO OUT", 27, 6);
	} else if (stage_id == "c2m3") {
		add("coldsnap", "COLD SNAP", 23, 9);
		add("sealgate", "THE CHOIR CLOSES RANKS", 31, 7);
		add("smokescreen", "SMOKE IN THE RAFTERS", 26, 7);
	} else if (stage_id == "c2b3") {
		add("coldsnap", "COLD SNAP", 21, 9);
		add("heatwave", "HEAT WAVE", 30, 5);
		add("sealgate", "THE QUENCH TANK SEALS", 36, 7);
		add("smokescreen", "SMOKE IN THE RAFTERS", 25, 7);
	} else if (stage_id == "c3m3") {
		add("heatwave", "HEAT WAVE", 22, 6);
		add("rustfall", "RUST ON THE WIND", 17, 0);
		add("tongslock", "THE TONGS ARE TAKEN", 20, 5);
	} else if (stage_id == "c3b3") {
		add("heatwave", "HEAT WAVE", 20, 6);
		add("rustfall", "RUST ON THE WIND", 15, 0);
		add("coldsnap", "COLD SNAP", 27, 8);
		add("pincer", "THE WALLS CLOSE IN", 32, 7);
	}
}

namespace {

// Which skills are impositions - a thing done TO the board for a while, as
// against a blow that lands and is over. Only one may be live at a time.
bool holds_the_board (const std::string& id) {
	return id == "sealgate" || id == "coldsnap" || id == "pincer"
		|| id == "vaultdark" || id == "smokescreen" || id == "deadweight"
		|| id == "tongslock" || id == "heatwave";
}

} // namespace

int VersusMatch::standing () const {
	int up = 0;
	for (const std::unique_ptr<Foe>& foe : foes) {
		up += foe->down ? 0 : 1;
	}
	return up;
}

VersusMatch::Foe* VersusMatch::aimed () {
	if (foes.empty()) {
		return nullptr;
	}
	// An aim left on someone already down would swallow the player's whole
	// output, so it walks to whoever is still up before it is read.
	if (target < 0 || target >= static_cast<int>(foes.size())
		|| foes[static_cast<size_t>(target)]->down) {
		for (size_t at = 0; at < foes.size(); ++at) {
			if (!foes[at]->down) {
				target = static_cast<int>(at);
				return foes[at].get();
			}
		}
		return nullptr;
	}
	return foes[static_cast<size_t>(target)].get();
}

const VersusMatch::Foe* VersusMatch::aimed () const {
	// The reading half of the same walk, for the screen: it reports who
	// would be hit without moving the aim to say so.
	if (foes.empty()) {
		return nullptr;
	}
	if (target >= 0 && target < static_cast<int>(foes.size())
		&& !foes[static_cast<size_t>(target)]->down) {
		return foes[static_cast<size_t>(target)].get();
	}
	for (const std::unique_ptr<Foe>& foe : foes) {
		if (!foe->down) {
			return foe.get();
		}
	}
	return nullptr;
}

void VersusMatch::aim_next () {
	if (foes.size() < 2) {
		return;
	}
	for (size_t step = 1; step <= foes.size(); ++step) {
		const size_t at = (static_cast<size_t>(target) + step) % foes.size();
		if (!foes[at]->down) {
			target = static_cast<int>(at);
			return;
		}
	}
}

std::string VersusMatch::foe_title () const {
	const std::string might = bot::might_of(rank_index);
	const std::string name = foe_name.empty() ? "Bot" : foe_name;
	// The road's names mostly carry their own article, and an epithet
	// belongs inside it: "The Keen Underwarden", not "Keen The Underwarden".
	if (name.rfind("The ", 0) == 0) {
		return "The " + might + ' ' + name.substr(4);
	}
	return might + ' ' + name;
}

void VersusMatch::tick_skills (Session& player) {
	if (skills.empty()) {
		return;
	}
	const long frame = player.sim().frame();
	// What is already holding the board, so a second imposition defers
	// rather than piling on. A fat kit with six-second windows on
	// twenty-second periods would otherwise settle into a permanent
	// fog-dark-narrow-heavy soup, which is not difficulty, it is mud.
	bool held = false;
	for (const Skill& skill : skills) {
		held = held || (frame < skill.active_until
			&& holds_the_board(skill.id));
	}
	for (Skill& skill : skills) {
		if (!skill.telegraphing && frame >= skill.next_fire - skill.telegraph) {
			// An imposition that cannot land yet does not even warn: the
			// telegraph is a promise, and a promise the fight breaks
			// teaches the player to ignore it.
			if (held && holds_the_board(skill.id)) {
				skill.next_fire += 100;
				continue;
			}
			skill.telegraphing = true;
			skill_banner = skill.warning;
			skill_banner_until = skill.next_fire + 50;
			skill_caster = skill.id;
			skill_warned_at = frame;
			skill_fires_at = skill.next_fire;
			skill_cues.push_back("skillwarn");
		}
		// The launch, a beat before the blow: the bolt is away and the
		// screen has the flight to show it. Only the announced skill gets
		// one - a deferred spell is not in the air.
		if (skill.telegraphing && skill_caster == skill.id
			&& frame == skill.next_fire - kFlight) {
			skill_cues.push_back("skillcast");
		}
		if (skill.telegraphing && frame >= skill.next_fire) {
			// Two warnings can begin before either has landed, so the
			// board is asked again at the blow itself. A spell that would
			// pile on waits its turn - and the warning goes with it, so
			// the plate never promises something the fight then swallows.
			if (held && holds_the_board(skill.id)) {
				skill.telegraphing = false;
				skill.next_fire += 100;
				if (skill_caster == skill.id) {
					skill_banner.clear();
					skill_banner_until = -1;
					skill_caster.clear();
				}
				continue;
			}
			skill.telegraphing = false;
			skill.active_until = frame + skill.duration;
			skill.landed_at = frame;
			skill.next_fire += skill.period;
			held = held || holds_the_board(skill.id);
			if (skill.id == "rustfall") {
				// Three rows of rust thrown at the player's floor, riding
				// the same pending-garbage rail an attack does.
				player.receive_attack(3);
				skill_cues.push_back("hit");
			} else if (skill.id == "forgestrike") {
				// The long wind-up, and the blow to match it.
				player.receive_attack(6);
				skill_cues.push_back("hit");
			} else if (skill.id == "heatwave") {
				// The gauge, taken. With no clock in a duel, Overdrive is
				// the one resource a fight is fought over - so taking it
				// is the pressure the wick used to be.
				player.sim_mutable().drain_flow(0.5);
				skill_cues.push_back("pressure");
			} else if (skill.id == "coldsnap") {
				skill_cues.push_back("freeze");
			} else if (skill.id == "vaultdark"
				|| skill.id == "smokescreen") {
				skill_cues.push_back("skilldark");
			} else if (skill.id == "deadweight") {
				skill_cues.push_back("skillheavy");
			} else {
				skill_cues.push_back("skillseal");
			}
		}
	}
	// What is live this frame, gathered before any of it is imposed.
	int want_seal = 0;
	bool want_cold = false;
	bool want_pressure = false;
	bool want_dark = false;
	bool want_fog = false;
	bool want_bar = false;
	int want_gravity = 0;
	for (const Skill& skill : skills) {
		if (frame >= skill.active_until) {
			continue;
		}
		if (skill.id == "sealgate") {
			want_seal |= kSealMask;
		} else if (skill.id == "pincer") {
			want_seal |= kSealMask | 1;
		} else if (skill.id == "coldsnap") {
			want_cold = true;
		} else if (skill.id == "heatwave") {
			want_pressure = true;
		} else if (skill.id == "vaultdark") {
			want_dark = true;
		} else if (skill.id == "smokescreen") {
			want_fog = true;
		} else if (skill.id == "tongslock") {
			want_bar = true;
		} else if (skill.id == "deadweight") {
			// Twice the weight, floored where a piece is still playable.
			want_gravity = std::max(6, player.sim().config().fall_delay / 2);
		}
	}
	skill_pressure = want_pressure;
	imposed_dark = want_dark;
	imposed_fog = want_fog;
	imposed_hold_bar = want_bar;
	imposed_gravity = want_gravity;
	// The seal never falls on a piece that stands in the column: it waits,
	// still wanted, until the piece is clear - and lifts the moment its
	// spell ends.
	int seal = imposed_sealed;
	if (want_seal != 0 && imposed_sealed == 0) {
		bool clear_of = true;
		if (player.sim().entry()) {
			for (const Offset cell : cells_of(player.sim().piece())) {
				if (want_seal >> cell.x & 1) {
					clear_of = false;
					break;
				}
			}
		}
		if (clear_of) {
			seal = want_seal;
		}
	} else if (want_seal == 0) {
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
	// The roster. A duel is one foe at the rank it was given; a raid is
	// the whole rank list at once - a room, not a queue. They used to be
	// fought one per round, which is a gauntlet and not what three foes on
	// a screen looks like anywhere else.
	std::vector<int> roster
		= raid() ? raid_ranks : std::vector<int>{rank_index};
	if (raid()) {
		// The headline rank is the strongest thing in the room, which is
		// what the room should be called after.
		rank_index = *std::max_element(roster.begin(), roster.end());
	}
	foes.clear();
	target = 0;
	for (size_t at = 0; at < roster.size(); ++at) {
		const int rung = roster[at];
		// Each foe carries its own rung's standard blade in a raid; a lone
		// foe keeps the blade the road handed it.
		const std::vector<std::string> worn
			= raid() ? temper::blade_for(rung) : blade;
		SimConfig config = temper::tempered(bot_base, worn);
		config.forced_delay = 0.;
		config.finesse_rule = 0;
		config.sdf = 40;
		config.das_ms = 330;
		config.cleartype = 0;   // The belt: blades never carry collapse.
		// Its recording carries the same stamp and rules, with the handling
		// it actually played under and the blade written down the way a
		// drafted build would have been - so an embedded bot side analyses
		// truthfully.
		replay::Meta meta = player_meta;
		meta.gametype = player_meta.gametype;
		meta.forced_delay = config.forced_delay;
		meta.finesse = config.finesse_rule;
		meta.cleartype = config.cleartype;
		meta.das = config.das_ms;
		meta.sdf = config.sdf;
		meta.tempers = worn;
		// A shared seed would have the whole room playing the same game in
		// unison, which reads as one foe drawn three times. Each gets its
		// own stream, still derived from the round's seed so the fight
		// replays.
		const unsigned own = seed + static_cast<unsigned>(at) * 7919u;
		foes.push_back(std::make_unique<Foe>(config, own, meta, rung, worn));
	}
	bot_tempers = foes.front()->blade;
	phase = Phase::Playing;
	phase_frames = 0;
	round_player_won = false;
	round_draw = false;
	// The skills re-arm with the round: the player's board is fresh, its
	// clock back at zero, and nothing imposed survives into it.
	for (Skill& skill : skills) {
		skill.next_fire = skill.period;
		skill.active_until = -1;
		skill.landed_at = -1;
		skill.telegraphing = false;
	}
	skill_pressure = false;
	imposed_sealed = 0;
	imposed_cold = false;
	imposed_dark = false;
	imposed_fog = false;
	imposed_hold_bar = false;
	imposed_gravity = 0;
	imposed_weight = 0;
	skill_banner.clear();
	skill_banner_until = -1;
	skill_caster.clear();
	skill_warned_at = -1;
	skill_fires_at = -1;
}

bool VersusMatch::step (Session& player) {
	if (phase != Phase::Playing || foes.empty()) {
		return false;
	}
	// Every foe still up plays this frame. One of them going down does not
	// end the round in a raid - it just stops that board and takes it off
	// the wire; the room is beaten when the last of them falls.
	for (const std::unique_ptr<Foe>& foe : foes) {
		if (foe->down) {
			continue;
		}
		const auto event = foe->driver.next(foe->sim.sim());
		if (event.has_value()) {
			foe->sim.key(event->key, event->down);
		}
		if (!foe->sim.step()) {
			foe->down = true;
		}
		// Their sounds stay on their side of the table.
		foe->sim.take_cues();
	}
	const bool bot_alive = standing() > 0;
	// The boss's skills run on the player's clock, before the pressure
	// line reads what they want.
	tick_skills(player);
	// The weight, applied here rather than by the screen: gravity is read
	// from a member the sim derived at birth, and a skill that only moved
	// the tuning would be a banner with nothing behind it. Lifted the
	// moment the spell ends.
	const int weight = imposed_gravity != 0 ? imposed_gravity
		: player.sim().config().fall_delay;
	if (weight != imposed_weight) {
		imposed_weight = weight;
		player.sim_mutable().impose_gravity(weight);
	}
	// Heat pressure, both ways: an Overdrive burning on one board makes
	// the other board's fuse burn faster - and a heat wave holds it hot
	// from the skill side. Igniting is an attack.
	bool any_burning = false;
	for (const std::unique_ptr<Foe>& foe : foes) {
		any_burning = any_burning
			|| (!foe->down && foe->sim.sim().overdrive());
	}
	player.sim_mutable().set_pressure(any_burning || skill_pressure);
	for (const std::unique_ptr<Foe>& foe : foes) {
		if (!foe->down) {
			foe->sim.sim_mutable().set_pressure(player.sim().overdrive());
		}
	}
	const bool player_alive = !player.over();
	// The wire: every side's outgoing first, then every delivery, so this
	// frame's attack cannot cancel against itself in flight.
	//
	// The player's fire all goes to one foe. Spreading it would be easier
	// to write and duller to play: with three boards in the room, choosing
	// who to bury first - the one closest to topping out, or the one
	// hitting hardest - is the only decision that makes a raid a fight
	// rather than three times the garbage.
	const int from_player = player.take_outgoing();
	int from_foes = 0;
	for (const std::unique_ptr<Foe>& foe : foes) {
		if (!foe->down) {
			from_foes += foe->sim.take_outgoing();
		}
	}
	if (from_player > 0) {
		if (Foe* mark = aimed()) {
			mark->sim.receive_attack(from_player);
			wire_to_bot += from_player;
		}
	}
	if (from_foes > 0) {
		player.receive_attack(from_foes);
		wire_to_player += from_foes;
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
	// A raid is one round: the room is cleared or the player falls in it,
	// and either way there is nothing to replay. A match runs to first_to
	// both ways.
	phase = (raid() || player_wins >= first_to
			|| bot_wins >= (raid() ? 1 : first_to))
		? Phase::MatchOver : Phase::RoundOver;
	phase_frames = 0;
	return false;
}

} // namespace gui
} // namespace forcetris
