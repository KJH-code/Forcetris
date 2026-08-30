#include "forcetris/campaign.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "forcetris/bot.hpp"
#include "forcetris/temper.hpp"

namespace forcetris {
namespace campaign {

namespace fs = std::filesystem;

namespace {

// Comma-separated ids out of a recipe string.
std::vector<std::string> split_ids (const char* text) {
	std::vector<std::string> ids;
	if (text == nullptr) {
		return ids;
	}
	std::istringstream in(text);
	std::string id;
	while (std::getline(in, id, ',')) {
		if (!id.empty()) {
			ids.push_back(id);
		}
	}
	return ids;
}

} // namespace

// The chapters and the stage recipes live in stages.cpp - content on its
// own page, so growing the road never touches the machinery here.

const std::vector<Upgrade>& anvil () {
	// Levels are deliberately shallow: the Anvil is a leg up the road, not
	// a treadmill. Wick/bank/bellows land on the stage's rules in
	// apply-order below; sense and preheat are read by the GUI.
	static const std::vector<Upgrade> forge = {
		// The first two were bought with slag by players who are still
		// playing, so the ids stay and the metal moves with the game: the
		// wick used to lengthen a clock most rooms no longer carry, and
		// the bank used to refill it. They feed the gauge now, which is
		// live in every room on the road.
		{"wick", "Forged Wick", "the fire holds its heat in stages", 3, 25},
		{"bank", "Deep Bank", "digging charges the gauge in stages", 2, 30},
		{"bellows", "Great Bellows", "Overdrive lasts longer in stages", 2, 30},
		{"sense", "Ember Sense", "earn more embers in stages", 2, 35},
		{"preheat", "Preheat", "start every stage with a free draft", 1, 60},
		// The V2.1d pair, read by begin_run: a life for the forged runs,
		// and coin in the purse before the first fight.
		{"lifeblood", "Forged Lifeblood",
			"forged runs carry one more life", 1, 70},
		{"warchest", "War Chest",
			"runs set out with embers in the purse", 2, 40},
	};
	return forge;
}

const Upgrade* upgrade (const std::string& id) {
	for (const Upgrade& entry : anvil()) {
		if (id == entry.id) {
			return &entry;
		}
	}
	return nullptr;
}

std::string path (const std::string& root) {
	if (const char* forced = std::getenv("FORCETRIS_CAMPAIGN")) {
		return forced;
	}
	return (fs::path(root) / "data" / "campaign.dat").string();
}

State load (const std::string& path) {
	State state;
	std::ifstream source(path);
	if (!source) {
		return state;
	}
	std::string line;
	while (std::getline(source, line)) {
		if (line.empty() || line[0] == '#') {
			continue;
		}
		std::istringstream in(line);
		std::string key;
		in >> key;
		if (key == "stage") {
			std::string id;
			int stars = 0;
			in >> id >> stars;
			if (!id.empty() && !in.fail()) {
				state.stars[id] = std::clamp(stars, 0, 3);
			}
		} else if (key == "slag") {
			in >> state.slag;
			state.slag = std::max(0, state.slag);
		} else if (key == "forge") {
			std::string id;
			int level = 0;
			in >> id >> level;
			if (!id.empty() && !in.fail()) {
				// Clamp to what the Anvil actually sells; an unknown id is
				// kept as bought - a newer build's upgrade must survive a
				// round trip through this one.
				const Upgrade* sold = upgrade(id);
				state.forge[id] = sold != nullptr
					? std::clamp(level, 0, sold->levels)
					: std::max(0, level);
			}
		} else if (key == "difficulty") {
			std::string name;
			in >> name;
			state.run.difficulty = difficulty_from(name);
		} else if (key == "run_chapter") {
			in >> state.run.chapter;
			state.run.chapter = std::clamp(state.run.chapter, 0,
				static_cast<int>(chapters().size()) - 1);
		} else if (key == "run_seed") {
			// The seed's presence is what says a climb is under way: every
			// other run key without it is leftovers, not a run.
			in >> state.run.seed;
			state.run.active = !in.fail();
		} else if (key == "run_depth") {
			in >> state.run.depth;
			state.run.depth = std::clamp(state.run.depth, 0, kMapDepth);
		} else if (key == "run_path") {
			std::string csv;
			in >> csv;
			state.run.path.clear();
			std::istringstream nodes(csv);
			std::string one;
			while (std::getline(nodes, one, ',')) {
				if (!one.empty()) {
					state.run.path.push_back(
						std::max(0, std::atoi(one.c_str())));
				}
			}
		} else if (key == "run_tempers") {
			std::string csv;
			in >> csv;
			state.run.tempers = split_ids(csv.c_str());
		} else if (key == "run_oils") {
			std::string csv;
			in >> csv;
			state.run.oils = split_ids(csv.c_str());
		} else if (key == "run_embers") {
			in >> state.run.embers;
			state.run.embers = std::max(0, state.run.embers);
		} else if (key == "run_seconds") {
			in >> state.run.seconds;
		} else if (key == "run_deaths") {
			in >> state.run.deaths;
		} else if (key == "run_lives") {
			in >> state.run.lives;
			state.run.lives = std::clamp(state.run.lives, 0, 9);
		} else if (key == "run_endless") {
			int flag = 0;
			in >> flag;
			state.run.endless = flag != 0;
		} else if (key == "run_ring") {
			in >> state.run.ring;
			state.run.ring = std::clamp(state.run.ring, 0, 999);
		} else if (key == "endless_best") {
			in >> state.endless_best;
			state.endless_best = std::max(0, state.endless_best);
		} else {
			state.unknown.push_back(line);
		}
	}
	if (!state.run.active) {
		// Leftover run keys with no seed reset to a clean no-run state, so
		// a half-erased file can never revive a ghost climb.
		state.run = Run{};
	} else {
		// The depth is the number of rows already fought, which is also
		// the path's length - held to it, so a mangled file cannot claim
		// a height it never climbed.
		state.run.depth = std::min(state.run.depth,
			static_cast<int>(state.run.path.size()));
	}
	return state;
}

bool save (const std::string& path, const State& state) {
	std::error_code ignored;
	fs::create_directories(fs::path(path).parent_path(), ignored);
	std::ofstream out(path, std::ios::trunc);
	if (!out) {
		return false;
	}
	out << "# forcetris campaign 1\n";
	out << "slag " << state.slag << "\n";
	if (state.endless_best > 0) {
		out << "endless_best " << state.endless_best << "\n";
	}
	for (const auto& [id, stars] : state.stars) {
		out << "stage " << id << " " << stars << "\n";
	}
	for (const auto& [id, level] : state.forge) {
		out << "forge " << id << " " << level << "\n";
	}
	if (state.run.active) {
		// The run rides in the same file; when there is none the keys are
		// simply not written, which is how ending a run erases it.
		out << "difficulty " << difficulty_name(state.run.difficulty) << "\n";
		out << "run_chapter " << state.run.chapter << "\n";
		out << "run_seed " << state.run.seed << "\n";
		out << "run_depth " << state.run.depth << "\n";
		if (!state.run.path.empty()) {
			out << "run_path ";
			for (size_t i = 0; i < state.run.path.size(); ++i) {
				out << (i > 0 ? "," : "") << state.run.path[i];
			}
			out << "\n";
		}
		if (!state.run.tempers.empty()) {
			out << "run_tempers ";
			for (size_t i = 0; i < state.run.tempers.size(); ++i) {
				out << (i > 0 ? "," : "") << state.run.tempers[i];
			}
			out << "\n";
		}
		if (!state.run.oils.empty()) {
			out << "run_oils ";
			for (size_t i = 0; i < state.run.oils.size(); ++i) {
				out << (i > 0 ? "," : "") << state.run.oils[i];
			}
			out << "\n";
		}
		out << "run_embers " << state.run.embers << "\n";
		out << "run_lives " << state.run.lives << "\n";
		out << "run_seconds " << state.run.seconds << "\n";
		out << "run_deaths " << state.run.deaths << "\n";
		if (state.run.endless) {
			out << "run_endless 1\n";
			out << "run_ring " << state.run.ring << "\n";
		}
	}
	for (const std::string& line : state.unknown) {
		out << line << "\n";
	}
	return out.good();
}

bool open (const State& state, size_t stage) {
	if (stage >= stages().size()) {
		return false;
	}
	if (stage == 0) {
		return true;
	}
	const auto before = state.stars.find(stages()[stage - 1].id);
	return before != state.stars.end() && before->second > 0;
}

// --- Reading the road by role. ------------------------------------------
// The table used to be read by position - the chapter's last recipe was
// its boss, the one before it the miniboss - which meant a chapter could
// hold exactly one of each. These five ask the recipes what they are
// instead, so a chapter can field as many concept pairs as it likes.
int chapter_base (int chapter) {
	int base = 0;
	for (int c = 0; c < chapter && c < static_cast<int>(chapters().size());
		++c) {
		base += chapters()[static_cast<size_t>(c)].stages;
	}
	return base;
}

namespace {

// Every flat index of one chapter, in table order.
std::vector<int> chapter_span (int chapter) {
	std::vector<int> span;
	if (chapter < 0 || chapter >= static_cast<int>(chapters().size())) {
		return span;
	}
	const int base = chapter_base(chapter);
	const int count = chapters()[static_cast<size_t>(chapter)].stages;
	for (int i = 0; i < count; ++i) {
		span.push_back(base + i);
	}
	return span;
}

} // namespace

std::vector<int> chapter_rooms (int chapter) {
	std::vector<int> rooms;
	for (const int at : chapter_span(chapter)) {
		if (stages()[static_cast<size_t>(at)].role == kRoom) {
			rooms.push_back(at);
		}
	}
	return rooms;
}

std::vector<int> chapter_pairs (int chapter) {
	std::vector<int> pairs;
	for (const int at : chapter_span(chapter)) {
		const Stage& stage = stages()[static_cast<size_t>(at)];
		if (stage.role != kBoss) {
			continue;
		}
		if (std::find(pairs.begin(), pairs.end(), stage.pair)
			== pairs.end()) {
			pairs.push_back(stage.pair);
		}
	}
	return pairs;
}

int pair_miniboss (int chapter, int pair) {
	for (const int at : chapter_span(chapter)) {
		const Stage& stage = stages()[static_cast<size_t>(at)];
		if (stage.role == kMiniboss && stage.pair == pair) {
			return at;
		}
	}
	return -1;
}

int pair_boss (int chapter, int pair) {
	for (const int at : chapter_span(chapter)) {
		const Stage& stage = stages()[static_cast<size_t>(at)];
		if (stage.role == kBoss && stage.pair == pair) {
			return at;
		}
	}
	return -1;
}

std::vector<int> chapter_bosses (int chapter) {
	std::vector<int> bosses;
	for (const int at : chapter_span(chapter)) {
		if (stages()[static_cast<size_t>(at)].role == kBoss) {
			bosses.push_back(at);
		}
	}
	return bosses;
}

bool chapter_open (const State& state, int chapter) {
	if (chapter < 0 || chapter >= static_cast<int>(chapters().size())) {
		return false;
	}
	if (chapter == 0) {
		return true;
	}
	// A star on ANY of the previous chapter's bosses opens the door: a run
	// only ever meets the one its map rolled, so beating that one is
	// beating the chapter.
	for (const int boss : chapter_bosses(chapter - 1)) {
		const auto held
			= state.stars.find(stages()[static_cast<size_t>(boss)].id);
		if (held != state.stars.end() && held->second > 0) {
			return true;
		}
	}
	return false;
}

int slag_percent (int difficulty) {
	return difficulty == kWhite ? 200 : difficulty == kForged ? 150 : 100;
}

namespace {

// "12000" -> "12,000". A five-figure target is unreadable without it.
std::string grouped_count (long long value) {
	std::string digits = std::to_string(value);
	for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) {
		digits.insert(static_cast<size_t>(at), ",");
	}
	return digits;
}

} // namespace

std::string goal_line (const Stage& stage) {
	if (stage.mode == 5) {
		if (stage.raid != nullptr) {
			return "Put down every foe in the room.";
		}
		return stage.first_to > 1
			? "Win " + std::to_string(stage.first_to) + " rounds."
			: "Win one round.";
	}
	if (stage.mode == 4) {
		// A watch is held on the clock when it has one, and dug out from
		// under the rising floor when it does not.
		if (stage.survive_seconds > 0) {
			return "Survive " + std::to_string(stage.survive_seconds)
				+ " seconds. Clearing lines is optional.";
		}
		return "Clear " + std::to_string(stage.quota)
			+ " lines before the floor reaches the top.";
	}
	if (stage.mode == 3) {
		return "Dig through " + std::to_string(stage.quota)
			+ " rows of rubble.";
	}
	if (stage.score_quota > 0) {
		return "Score " + grouped_count(stage.score_quota)
			+ " points. Lines alone will not do it.";
	}
	return "Clear " + std::to_string(stage.quota) + " lines.";
}

Verdict grade_run (const Run& run, bool won) {
	// Four terms, and the letter is only ever the sum of them - a grade a
	// player cannot take apart is a grade they cannot chase.
	//
	// Progress dominates because a climb is about how far it got. Blood is
	// next: the roguelite's whole tension is spending lives, so a clean run
	// has to be visibly worth more than a bought one. Pace is last and
	// deliberately gentle - this is a casual-first game and a player who
	// thinks about their stack is not playing it wrong. Nothing here can
	// push a slow, bloody, short run above a fast, clean, deep one.
	Verdict out;
	out.rows = std::max(0, run.endless
		? run.ring * kMapDepth + run.depth : run.depth);
	out.deaths = std::max(0, run.deaths);
	out.seconds = std::max(0, run.seconds);
	out.finished = won && !run.endless && run.depth >= kMapDepth;

	// Progress, out of 55. Endless has no end, so its rows score against a
	// road's worth of climbing per ring and simply saturate.
	const double reach = run.endless
		? std::min(1., out.rows / static_cast<double>(kMapDepth * 4))
		: std::min(1., out.rows / static_cast<double>(kMapDepth));
	double score = 55. * reach;

	// Blood, out of 25.
	score += std::max(0., 25. - 8. * out.deaths);

	// Pace, out of 20: full value under par, nothing left at three times
	// it. A run with no rows behind it has no pace to judge and scores
	// none - it cannot earn the term by ending early.
	if (out.rows > 0) {
		const double par = 150. * out.rows;
		const double ratio = out.seconds <= 0 ? 1. : out.seconds / par;
		score += 20. * std::clamp(1.5 - 0.5 * ratio, 0., 1.);
	}

	// The fire the wager was made at.
	score *= run.difficulty == kWhite ? 1.15
		: run.difficulty == kForged ? 1.0 : 0.85;
	out.score = static_cast<int>(std::clamp(score, 0., 100.));

	const char* grade = out.score >= 90 ? "S"
		: out.score >= 75 ? "A"
		: out.score >= 60 ? "B"
		: out.score >= 40 ? "C" : "D";
	out.grade[0] = grade[0];
	out.grade[1] = '\0';
	return out;
}

int rank_for (int rank, int difficulty) {
	// Forged is the recipe as written - the honest middle it was balanced
	// as - and white is one rung above it. A rung is a real step: the bot
	// thinks wider and drops faster with each, so both are felt without a
	// recipe being rewritten.
	//
	// Mild is not one rung below. It used to be, and one rung below a
	// ladder that climbs to SS still ended the road on S, which is a fight
	// nobody learning the game can win. The gentlest fire is for someone
	// whose own play is around the bottom of the league, so it drops two
	// rungs AND refuses to go above B however high the recipe climbs. That
	// puts every mild duel in the F-to-B band and the last boss of the road
	// at B - a hard fight for a beginner, and a possible one.
	const int last = static_cast<int>(bot::ranks().size()) - 1;
	if (difficulty == kMild) {
		return std::clamp(std::min(rank - 2, kMildCeiling), 0, last);
	}
	return std::clamp(rank + (difficulty == kWhite ? 1 : 0), 0, last);
}

const char* difficulty_name (int difficulty) {
	return difficulty == kWhite ? "white"
		: difficulty == kForged ? "forged" : "mild";
}

int difficulty_from (const std::string& name) {
	return name == "white" ? kWhite : name == "forged" ? kForged : kMild;
}

namespace {

// The stage's own overrides, shared by both sides of a boss fight.
SimConfig overridden (const Stage& stage, SimConfig config) {
	// The fuse is a stage gimmick, whatever the Rules tab says: most rooms
	// play the board pure - the forced drop was the beginners' wall - and
	// only the recipes that name the burn still burn. Duels used to be
	// carved out as well, on the argument that the clock was the duel's
	// tension; playing it says otherwise. A clock that is always there is
	// not tension, it is the rule the beginner already lost to, and it
	// made the one fight the road builds towards feel like the trainer.
	// The duel's tension is the foe: its attack, its blade, and the
	// skills it telegraphs at you.
	config.fuse = stage.fuse;
	config.fuse_base *= stage.fuse_scale;
	config.fuse_base = std::max(config.fuse_min, config.fuse_base);
	if (stage.fall_delay >= 1) {
		config.fall_delay = stage.fall_delay;
	}
	if (stage.cheese_holes >= 1) {
		config.cheese_holes = stage.cheese_holes;
	}
	if (stage.cheese_messiness >= 0) {
		config.cheese_messiness = stage.cheese_messiness;
	}
	if (stage.cheese_period >= 1) {
		config.cheese_period = stage.cheese_period;
	}
	if (stage.no_kicks) {
		config.kicks = false;
	}
	if (stage.cleartype >= 0) {
		config.cleartype = stage.cleartype;
	}
	if (stage.spin_rule >= 0) {
		config.spin_rule = stage.spin_rule;
	}
	if (stage.sealed != 0) {
		config.sealed = stage.sealed;
	}
	if (stage.cold_iron) {
		config.cold_iron = true;
	}
	if (stage.mode == 3) {
		config.cheese_total = stage.quota;
	} else if (stage.mode != 5 && stage.quota > 0
		&& stage.survive_seconds == 0) {
		// Under a watch the quota is the star bar, not a finish line.
		config.line_quota = stage.quota;
	}
	if (stage.mode != 5 && stage.score_quota > 0) {
		config.score_quota = stage.score_quota;
	}
	if (stage.mode != 5 && stage.survive_seconds > 0) {
		config.survive_ms = stage.survive_seconds * 1000;
	}
	return config;
}

} // namespace

SimConfig stage_config (const Stage& stage, SimConfig base,
		const std::map<std::string, int>& forge) {
	SimConfig config = overridden(stage, base);
	config = temper::tempered(config, split_ids(stage.tempers));
	// The Anvil, last: permanent metal on top of the stage's terms.
	const auto level = [&forge] (const char* id) {
		const auto found = forge.find(id);
		return found != forge.end() ? found->second : 0;
	};
	config.flow_keep += 0.08 * level("wick");
	config.flow_gain_dig += 0.5 * level("bank");
	config.overdrive_secs += 0.5 * level("bellows");
	return config;
}

SimConfig bot_config (const Stage& stage, SimConfig base) {
	// The stage's terms and nothing of the player's: no pre-applied
	// tempers, no Anvil. The blade goes on in the versus wiring.
	return overridden(stage, base);
}

std::vector<std::string> blade_of (const Stage& stage) {
	if (stage.blade != nullptr) {
		return split_ids(stage.blade);
	}
	return temper::blade_for(std::max(0, stage.rank));
}

std::vector<std::string> board_rows (const Stage& stage) {
	std::vector<std::string> rows;
	if (stage.board == nullptr) {
		return rows;
	}
	std::istringstream in(stage.board);
	std::string row;
	while (std::getline(in, row)) {
		if (!row.empty()) {
			rows.push_back(row);
		}
	}
	return rows;
}

bool endless_open (const State& state) {
	// The Deep Forge's master must have fallen at least once - whichever of
	// its masters the run happened to climb to. The climb draws on every
	// chapter's rooms, so it waits for the shipped road's end, not the
	// White Heart's, which is late-game of its own.
	for (const int boss : chapter_bosses(1)) {
		const auto held
			= state.stars.find(stages()[static_cast<size_t>(boss)].id);
		if (held != state.stars.end() && held->second > 0) {
			return true;
		}
	}
	return false;
}

int priced (int base, const Run& run) {
	if (base <= 0 || !run.active) {
		return std::max(0, base);
	}
	// The rungs behind you. A campaign run counts the chapters it has left
	// behind as full maps, so the price carries across a chapter break the
	// way the build does; a climb counts its rings the same way.
	const int rungs = run.endless ? endless_rows(run)
		: run.chapter * kMapDepth + run.depth;
	// Eight percent a rung, and never past three times the door price -
	// the ceiling is what keeps the last chapter a shop rather than a
	// museum. Integer arithmetic throughout: an ember is not divisible,
	// and a price the screen rounds differently from the till is a bug
	// waiting on a player to find it.
	const int scaled = base + base * 8 * std::max(0, rungs) / 100;
	return std::min(scaled, base * 3);
}

int endless_rows (const Run& run) {
	return run.ring * kMapDepth + run.depth;
}

SimConfig endless_scaled (SimConfig config, int ring) {
	// The climb only ever tightens. Gravity gains two frames a ring down
	// to a floor a human can still read; the finish lines stretch; the
	// flood quickens. campaigncheck holds the monotonicity.
	ring = std::max(0, ring);
	config.fall_delay = std::max(8, config.fall_delay - 2 * ring);
	if (config.line_quota > 0) {
		config.line_quota += 3 * ring;
	}
	if (config.score_quota > 0) {
		config.score_quota += config.score_quota * ring / 4;
	}
	if (config.survive_ms > 0) {
		config.survive_ms += ring * 12000;
	}
	config.cheese_period = std::max(60, config.cheese_period - 20 * ring);
	return config;
}

// What a rank costs the climb once it runs out of rungs to spend.
//
// Half a rung a ring is fine until the promotion hits the top of the
// ladder, and after that every further ring used to buy nothing at all -
// the foe stopped growing around ring eight while the player's build went
// on collecting a card a node forever. So the promotion that cannot be
// paid in rungs is paid in steel instead: what the foe sends is scaled by
// the rungs it was owed and never got. Unbounded on purpose. This and the
// curses are the two dials that still climb when everything else has hit
// its floor.
int endless_rank_owed (int rank, int ring) {
	return std::max(0, rank) + std::max(0, ring) / 2;
}

int endless_rank (int rank, int ring) {
	const int last = static_cast<int>(bot::ranks().size()) - 1;
	return std::min(last, endless_rank_owed(rank, ring));
}

SimConfig endless_edge (SimConfig foe, int rank, int ring) {
	const int last = static_cast<int>(bot::ranks().size()) - 1;
	const int over = endless_rank_owed(rank, ring) - last;
	if (over > 0) {
		foe.attack_scale += 0.12 * over;
	}
	return foe;
}

double skill_scale (int difficulty, bool endless, int ring) {
	// The three fires, and then the climb on top of the hottest of them.
	// A climb is always played at white heat, so its ring rides on 1.4
	// rather than starting over.
	double scale = difficulty == kMild ? 0.6
		: difficulty == kWhite ? 1.4 : 1.0;
	if (endless) {
		scale = 1.4 + 0.08 * std::max(0, ring);
	}
	return scale;
}

int skill_rows (int rows, double scale) {
	if (rows <= 0) {
		return 0;
	}
	// Never nothing: a blow the player watched arrive for two seconds and
	// then felt nothing from is worse than no blow at all.
	return std::max(1, py_round(rows * std::max(0., scale)));
}

long skill_frames (long frames, double scale) {
	if (frames <= 0) {
		return 0;
	}
	return std::max(50L,
		static_cast<long>(frames * std::max(0., scale)));
}

SimConfig endless_press (SimConfig mine, int ring) {
	// And the flood on the player's own board gets heavier every ring, from
	// the first one. A rung of rank is a foe that plays better; this is the
	// room itself leaning harder, and it is what a build that has outgrown
	// the ladder still has to hold back.
	mine.garbage_scale += 0.05 * std::max(0, ring);
	return mine;
}

int solo_stars (bool won, double seconds, int par_seconds, int forced,
		bool fused) {
	if (!won) {
		return 0;
	}
	int stars = 1;
	if (par_seconds > 0 && seconds <= static_cast<double>(par_seconds)) {
		++stars;
	}
	// The third star: in a burn room, an untouched run - the fuse never
	// once slammed a piece down. In a pure room there is no fuse to dodge,
	// so the mastery mark is pace instead: well inside the par.
	if (fused ? forced == 0
		: par_seconds > 0 && seconds <= par_seconds * 0.75) {
		++stars;
	}
	return stars;
}

int survive_stars (bool won, int lines, int bar) {
	// A watch has no par to race - the clock is fixed - so the mastery
	// marks are what was cleared while holding on: the bar for the
	// second star, twice the bar for the third.
	if (!won) {
		return 0;
	}
	return 1 + (bar > 0 && lines >= bar ? 1 : 0)
		+ (bar > 0 && lines >= bar * 2 ? 1 : 0);
}

int boss_stars (bool won, bool sweep, bool ignited) {
	if (!won) {
		return 0;
	}
	return 1 + (sweep ? 1 : 0) + (sweep && ignited ? 1 : 0);
}

int slag_award (const Stage& stage, bool first_clear, bool won, int stars,
		int embers_left) {
	if (won) {
		return (first_clear ? stage.slag_first : stage.slag_repeat)
			+ stars * 5;
	}
	// The prestige remnant: what the run did not spend renders down. The
	// embers already scale with how deep the run got - they were earned by
	// its lines and its attack - so depth needs no second term.
	return std::max(0, embers_left) / 5;
}

int ember_bonus_percent (const std::map<std::string, int>& forge) {
	const auto found = forge.find("sense");
	return found != forge.end() ? found->second * 25 : 0;
}

int free_drafts (const std::map<std::string, int>& forge) {
	const auto found = forge.find("preheat");
	return found != forge.end() && found->second > 0 ? 1 : 0;
}

int upgrade_cost (const Upgrade& upgrade, int level) {
	return upgrade.cost_base * std::max(1, level);
}

} // namespace campaign
} // namespace forcetris
