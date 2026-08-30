// The content of the Forge Road: the chapters, and every stage recipe.
//
// This file is deliberately nothing but tables - the mechanisms that read
// them (config assembly, the economy, the save file) live in campaign.cpp,
// so growing the game means editing here and only here.
//
// To add a stage: append a recipe to its chapter's stretch of the list and
// raise that chapter's count in chapters() - campaigncheck holds the two in
// agreement. Pick an id that has never been used and never reuse or rename
// one: the id is the key campaign.dat stores stars under, so a shipped id is
// frozen forever. Everything else - names, numbers, boards, blades - may be
// rebalanced freely. A stage needs: a mode the GUI already starts (0 quota,
// 3 dig, 4 survive, 5 boss), a finish line, and whichever rule overrides
// make its gimmick; see the Stage struct in campaign.hpp for every knob.
//
// To add a chapter: append to chapters() and give its stages to the list
// below, ending on a boss - campaigncheck checks that every chapter does.
#include "forcetris/campaign.hpp"

namespace forcetris {
namespace campaign {

const std::vector<Chapter>& chapters () {
	static const std::vector<Chapter> road = {
		{"c1", "The Outer Yard",
			"The forge teaches one fire at a time.", 17},
		{"c2", "The Deep Forge",
			"Every lesson, turned against you.", 18},
		{"c3", "The White Heart",
			"Every lesson at once, at white heat.", 16},
	};
	return road;
}

Spot spot_of (size_t stage_index) {
	Spot spot{0, static_cast<int>(stage_index)};
	for (const Chapter& chapter : chapters()) {
		if (spot.stage < chapter.stages) {
			return spot;
		}
		spot.stage -= chapter.stages;
		++spot.chapter;
	}
	return spot;
}

const std::vector<Stage>& stages () {
	// The road. Chapter one teaches the forge one gimmick at a time;
	// chapter two turns each of them against the player. Every id is
	// frozen - it is the key the save file stores stars under.
	static const std::vector<Stage> road = [] {
		std::vector<Stage> list;
		Stage s{};

		// --- Chapter 1: The Outer Yard. --------------------------------
		s = Stage{};
		s.id = "c1s1"; s.name = "First Sparks";
		s.blurb = "A plain board and nothing out to get you. Learn the well.";
		s.mode = 0; s.quota = 10; s.par_seconds = 100;
		s.slag_first = 15; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s2"; s.name = "The Scrap Pile";
		s.blurb = "The board starts part-filled with old iron. Clear down through"
			" it.";
		s.mode = 0; s.quota = 15; s.par_seconds = 130;
		s.board = "..77777.77\n777.777777\n7777777.77";
		s.slag_first = 18; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s3"; s.name = "The First Cut";
		s.blurb = "Rubble rows, one hole each. Find the hole and dig.";
		s.mode = 3; s.quota = 10; s.par_seconds = 110;
		s.cheese_holes = 1; s.cheese_messiness = 30;
		s.slag_first = 18; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s4"; s.name = "Loose Mortar";
		s.blurb = "Nothing holds here: when a line goes, the blocks above fall"
			" loose and can clear again.";
		s.mode = 0; s.quota = 15; s.par_seconds = 140;
		s.cleartype = 1;
		s.slag_first = 20; s.slag_repeat = 5;
		list.push_back(s);

		// V2.1: a skirmish - a lesser foe met on an ordinary battle node,
		// so not every duel is a boss. It sits inside the battle window,
		// unlike the trailing miniboss/boss block.
		s = Stage{};
		s.id = "c1k1"; s.name = "Scrap Whelp";
		s.blurb = "A small foe, and a slow one. Your first real opponent.";
		// The road's duels climb one rung at a time, three to a chapter,
		// and each chapter starts a rung under the last one's boss:
		// C-B-A, then B-A-S, then A-S-SS. A rung is a real step in how
		// the bot thinks, so the ladder is the whole difficulty curve -
		// and the fire picked at the door moves the lot one more rung
		// either way (campaign::rank_for).
		s.mode = 5; s.rank = 3; s.first_to = 1;
		s.slag_first = 24; s.slag_repeat = 5;
		list.push_back(s);

		// V2.1: a stage won by points, not rows - the room where a spin or
		// a quad is the fast way through instead of a flourish.
		s = Stage{};
		s.id = "c1s10"; s.name = "Trial of Sparks";
		s.blurb = "Lines count for nothing here - only points. Spins and quads"
			" pay most.";
		s.mode = 0; s.quota = 0; s.score_quota = 7000;
		s.par_seconds = 150;
		s.slag_first = 22; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s5"; s.name = "Rusted Joints";
		s.blurb = "Rust has sealed the outer columns shut. You are playing a"
			" narrower well.";
		s.mode = 0; s.quota = 15; s.par_seconds = 150;
		// V2.1 re-dress: "no kicks" was a rulebook gimmick nobody could see.
		// Sealed Columns is the same room made visible - the two outer
		// columns walled off, the whole stage fought eight wide.
		s.sealed = (1 << 0) | (1 << 9);
		s.slag_first = 20; s.slag_repeat = 5;
		list.push_back(s);

		// V2.1: a watch - the floor rises and the only job is to outlast
		// it. The quota is not a finish line here but the star bar.
		s = Stage{};
		s.id = "c1s11"; s.name = "The Long Watch";
		s.blurb = "The floor climbs while you hold. Staying alive is the whole"
			" job.";
		s.mode = 4; s.quota = 8; s.survive_seconds = 75;
		s.cheese_period = 400;
		s.slag_first = 24; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s6"; s.name = "The Rising Floor";
		s.blurb = "The floor climbs from below. Clear your way out before it"
			" reaches the top.";
		s.mode = 4; s.quota = 12; s.par_seconds = 150;
		s.cheese_period = 350;
		s.slag_first = 22; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s9"; s.name = "The Dark Gallery";
		s.blurb = "The well is dark. You can see only what is near your own"
			" piece.";
		s.mode = 0; s.quota = 12; s.par_seconds = 140;
		s.dim = true;
		s.slag_first = 22; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s7"; s.name = "Backdraft";
		s.blurb = "This room burns: every piece carries a fuse, and it slams down"
			" when the fuse runs out.";
		s.mode = 0; s.quota = 20; s.par_seconds = 170;
		s.fuse = true; s.pressure = true; s.fuse_scale = 0.9;
		s.slag_first = 24; s.slag_repeat = 6;
		list.push_back(s);

		// V2.1: the chapter's miniboss - a duel on the risky branch, one
		// row under the boss. By convention every chapter's table ends
		// [battles..., miniboss, boss]: the map generator counts the
		// trailing mode-5 recipes off the battle window and seats the
		// second-to-last as the kind-4 node.
		s = Stage{};
		s.id = "c1m1"; s.name = "The Underwarden";
		s.blurb = "A warden. It telegraphs its skills - the plate over your well"
			" says what is coming.";
		s.mode = 5; s.rank = 4; s.first_to = 1;
		s.role = kMiniboss; s.pair = 0;
		s.slag_first = 30; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s8"; s.name = "The Gatekeeper";
		s.blurb = "The yard's keeper: three telegraphed skills, and a real blade"
			" behind them.";
		s.mode = 5; s.rank = 5; s.first_to = 1;
		s.role = kBoss; s.pair = 0;
		s.slag_first = 40; s.slag_repeat = 8;
		list.push_back(s);

		// The Hammers: the same rung as the Wardens, and no telegraphed
		// trick at all - what they carry instead is a heavier blade.
		// A chapter's pairs are rolled per run, so which watch stands
		// over a climb is the map's own business.
		s = Stage{};
		s.id = "c1m2"; s.name = "The Slag Fist";
		s.blurb = "No tricks at all. It hits, and it keeps hitting.";
		s.mode = 5; s.rank = 4; s.first_to = 1;
		s.role = kMiniboss; s.pair = 1;
		s.blade = "thick_wick,quench,bellows,spark";
		s.slag_first = 30; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c1b2"; s.name = "The Anvil-Breaker";
		s.blurb = "It will not out-think you. It carries the heaviest blade on"
			" the road so far.";
		s.mode = 5; s.rank = 5; s.first_to = 1;
		s.role = kBoss; s.pair = 1;
		s.blade = "thick_wick,quench,bellows,spark,white_heat";
		s.slag_first = 40; s.slag_repeat = 8;
		list.push_back(s);

		// The Tricksters: lighter steel than the Wardens carry, and one
		// more trick than their station allows.
		s = Stage{};
		s.id = "c1m3"; s.name = "The Lamplighter";
		s.blurb = "A trickster. It fights by taking things away from you - read"
			" the warnings.";
		s.mode = 5; s.rank = 4; s.first_to = 1;
		s.role = kMiniboss; s.pair = 2;
		s.blade = "thick_wick,quench";
		s.slag_first = 30; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c1b3"; s.name = "The Rust Weaver";
		s.blurb = "Lighter than the keeper, and it never once stops meddling.";
		s.mode = 5; s.rank = 5; s.first_to = 1;
		s.role = kBoss; s.pair = 2;
		s.blade = "thick_wick,quench,bellows";
		s.slag_first = 40; s.slag_repeat = 8;
		list.push_back(s);

		// --- Chapter 2: The Deep Forge. --------------------------------
		s = Stage{};
		s.id = "c2s1"; s.name = "Heavier Air";
		s.blurb = "The deep forge presses down: pieces fall much faster here.";
		s.mode = 0; s.quota = 20; s.par_seconds = 160;
		s.fall_delay = 20;
		s.slag_first = 26; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s2"; s.name = "Three Cuts";
		s.blurb = "Rubble with three holes a row, and the holes move between"
			" rows.";
		s.mode = 3; s.quota = 14; s.par_seconds = 150;
		s.cheese_holes = 3; s.cheese_messiness = 100;
		s.slag_first = 26; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		// Chapter two opens a rung under the Gatekeeper it just beat.
		s.id = "c2k1"; s.name = "Ash Hound";
		s.blurb = "Something fast in the lower halls - quicker than anything"
			" above.";
		s.mode = 5; s.rank = 4; s.first_to = 1;
		s.slag_first = 28; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s10"; s.name = "Weight in Gold";
		s.blurb = "Points, not lines, and the floor falls fast while you chase"
			" them.";
		s.mode = 0; s.quota = 0; s.score_quota = 16000;
		s.par_seconds = 190;
		s.fall_delay = 24;
		s.slag_first = 28; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s3"; s.name = "Chain Collapse";
		s.blurb = "Linked cascade: loose blocks fall and clear again, and every"
			" twist scores.";
		s.mode = 0; s.quota = 20; s.par_seconds = 170;
		s.cleartype = 2; s.spin_rule = 3;
		s.slag_first = 28; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s9"; s.name = "Smoke in the Rafters";
		s.blurb = "Smoke over the queue. You can see one piece ahead, no more.";
		s.mode = 0; s.quota = 18; s.par_seconds = 170;
		s.fog = true;
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s11"; s.name = "The Flood Watch";
		s.blurb = "The deep floor climbs fast. Hold on; clearing is optional.";
		s.mode = 4; s.quota = 12; s.survive_seconds = 90;
		s.cheese_period = 300;
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s4"; s.name = "The Overheated Wing";
		s.blurb = "This room burns, and the heat is already in your blood: the"
			" fuse runs short.";
		s.mode = 0; s.quota = 20; s.par_seconds = 160;
		s.fuse = true; s.pressure = true; s.fuse_scale = 0.8;
		s.tempers = "overheat";
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s5"; s.name = "The Buried Hall";
		s.blurb = "The hall starts buried under rubble. Dig it out, then keep"
			" clearing.";
		s.mode = 0; s.quota = 18; s.par_seconds = 180;
		// V2.1 re-dress: the rubble is the event and stays; the invisible
		// "no kicks" rider is gone, per the rulebook-gimmick purge.
		s.board = "7.77777777\n77777.7777\n777.777777\n7777777.77\n77.7777777";
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		// V2.1: a raid - three lesser foes back to back, one loss and the
		// gauntlet closes. first_to is the gauntlet's length.
		s = Stage{};
		s.id = "c2r1"; s.name = "The Kennel";
		s.blurb = "The hounds come one after another. Weak alone, and there are"
			" three.";
		// A gauntlet is priced by the whole run of it, not by one foe: three
		// fights with no second chance, so every hound stands a rung under
		// the chapter's own ladder.
		s.mode = 5; s.rank = 3; s.first_to = 3;
		s.raid = "1,2,2";
		s.slag_first = 36; s.slag_repeat = 8;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s6"; s.name = "The Flood";
		s.blurb = "The floor climbs faster here. Clear down through it.";
		s.mode = 4; s.quota = 15; s.par_seconds = 180;
		s.cheese_period = 250;
		s.slag_first = 32; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s7"; s.name = "Cold Iron";
		s.blurb = "The iron freezes as it lands: a finished line locks solid and"
			" must be broken a second time.";
		s.mode = 0; s.quota = 18; s.par_seconds = 210;
		// V2.1 rebuild: the old dress was bare parameter extremes (gravity
		// and wick cranked). Now the room is cold instead - completed rows
		// freeze solid and shatter one lock later - and the dials relax to
		// merely brisk so the gimmick is the fight.
		s.cold_iron = true;
		s.fall_delay = 22;
		s.slag_first = 34; s.slag_repeat = 8;
		list.push_back(s);

		s = Stage{};
		s.id = "c2m1"; s.name = "The Quenchguard";
		s.blurb = "The master's second, cold and quick. It freezes your iron and"
			" hides your queue.";
		s.mode = 5; s.rank = 5; s.first_to = 1;
		s.role = kMiniboss; s.pair = 0;
		s.slag_first = 45; s.slag_repeat = 9;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s8"; s.name = "The Forgemaster";
		s.blurb = "The master: cold, heat and closing walls, and it does not fall"
			" in one round.";
		s.mode = 5; s.rank = 6; s.first_to = 2;
		s.role = kBoss; s.pair = 0;
		s.blade = "bellows,white_heat,overheat,gamble";
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		// The Deep Forge's Hammers: weight, and nothing but.
		s = Stage{};
		s.id = "c2m2"; s.name = "The Drop Hammer";
		s.blurb = "Nothing clever down here. Everything heavy.";
		s.mode = 5; s.rank = 5; s.first_to = 1;
		s.role = kMiniboss; s.pair = 1;
		s.blade = "thick_wick,quench,bellows,spark,white_heat";
		s.slag_first = 45; s.slag_repeat = 9;
		list.push_back(s);

		s = Stage{};
		s.id = "c2b2"; s.name = "The Deep Kiln";
		s.blurb = "It seals nothing and freezes nothing. It simply out-burns you.";
		s.mode = 5; s.rank = 6; s.first_to = 2;
		s.role = kBoss; s.pair = 1;
		s.blade = "bellows,white_heat,overheat,gamble,heavy_hand";
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		// And its Tricksters: cold, loud, and thin in the arm.
		s = Stage{};
		s.id = "c2m3"; s.name = "The Frost Cantor";
		s.blurb = "It sings the cold down on you, and hits like nothing at all.";
		s.mode = 5; s.rank = 5; s.first_to = 1;
		s.role = kMiniboss; s.pair = 2;
		s.blade = "thick_wick,quench,bellows";
		s.slag_first = 45; s.slag_repeat = 9;
		list.push_back(s);

		s = Stage{};
		s.id = "c2b3"; s.name = "The Quench Choir";
		s.blurb = "Three voices, none of them heavy, all of them cold.";
		s.mode = 5; s.rank = 6; s.first_to = 2;
		s.role = kBoss; s.pair = 2;
		s.blade = "bellows,white_heat,overheat";
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		// --- Chapter 3: The White Heart. -------------------------------
		// Every lesson at once: the rooms here pair gimmicks the first two
		// chapters taught one at a time - never a rule the road has not
		// already shown alone.
		s = Stage{};
		s.id = "c3s1"; s.name = "White Heat Rising";
		s.blurb = "The heart's antechamber. Nothing strange in here - only speed.";
		s.mode = 0; s.quota = 20; s.par_seconds = 170;
		s.fall_delay = 20;
		s.slag_first = 40; s.slag_repeat = 9;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s2"; s.name = "The Narrow Dark";
		s.blurb = "Sealed galleries, and only a lantern to see them by.";
		s.mode = 0; s.quota = 15; s.par_seconds = 180;
		s.dim = true;
		s.sealed = (1 << 0) | (1 << 9);
		s.slag_first = 42; s.slag_repeat = 9;
		list.push_back(s);

		s = Stage{};
		s.id = "c3k1"; s.name = "Cinder Wolf";
		s.blurb = "Something old hunts the antechamber.";
		s.mode = 5; s.rank = 5; s.first_to = 1;
		s.slag_first = 44; s.slag_repeat = 9;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s3"; s.name = "Gold in the Dark";
		s.blurb = "Points by lantern light. Spins pay best where you can see"
			" least.";
		s.mode = 0; s.quota = 0; s.score_quota = 20000;
		s.par_seconds = 210;
		s.dim = true;
		s.slag_first = 46; s.slag_repeat = 10;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s4"; s.name = "The Frozen Gallery";
		s.blurb = "The iron freezes and the smoke hides your queue. Both at once.";
		s.mode = 0; s.quota = 16; s.par_seconds = 210;
		s.cold_iron = true;
		s.fog = true;
		s.slag_first = 48; s.slag_repeat = 10;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s5"; s.name = "The Last Flood";
		s.blurb = "The heart floods faster than anywhere above. Hold on.";
		s.mode = 4; s.quota = 14; s.survive_seconds = 105;
		s.cheese_period = 260;
		s.slag_first = 50; s.slag_repeat = 10;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s6"; s.name = "Rubble and Frost";
		s.blurb = "Buried in cold scrap, where every line you cut freezes before"
			" it breaks.";
		s.mode = 0; s.quota = 16; s.par_seconds = 220;
		s.cold_iron = true;
		s.board = "77.7777777\n7777.77777\n7.77777777\n777777.777\n77777.7777";
		s.slag_first = 52; s.slag_repeat = 11;
		list.push_back(s);

		s = Stage{};
		s.id = "c3r1"; s.name = "The Pack";
		s.blurb = "Three of the forge's own, loose and hungry.";
		s.mode = 5; s.rank = 4; s.first_to = 3;
		s.raid = "2,3,3";
		s.slag_first = 58; s.slag_repeat = 12;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s7"; s.name = "Backdraft Vault";
		s.blurb = "The hottest room on the road: a short fuse, and the heat"
			" squeezing it shorter.";
		s.mode = 0; s.quota = 22; s.par_seconds = 200;
		s.fuse = true; s.pressure = true; s.fuse_scale = 0.8;
		s.slag_first = 54; s.slag_repeat = 11;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s8"; s.name = "Chain of Embers";
		s.blurb = "Linked iron. What hangs, falls - and falls again.";
		s.mode = 0; s.quota = 18; s.par_seconds = 210;
		s.cleartype = 2;
		s.slag_first = 56; s.slag_repeat = 11;
		list.push_back(s);

		s = Stage{};
		s.id = "c3m1"; s.name = "The Vault Warden";
		s.blurb = "The heart's last door and the warden who seals it. It shuts"
			" your columns and doubles your gravity.";
		s.mode = 5; s.rank = 6; s.first_to = 1;
		s.role = kMiniboss; s.pair = 0;
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		s = Stage{};
		s.id = "c3s9"; s.name = "The Forge Heart";
		s.blurb = "The fire itself. Everything the road taught you, turned around"
			" to kill you.";
		s.mode = 5; s.rank = 7; s.first_to = 2;
		s.role = kBoss; s.pair = 0;
		s.blade = "white_heat,white_heat,overheat,gamble,heavy_hand,"
			"loaded_dice";
		s.slag_first = 70; s.slag_repeat = 14;
		list.push_back(s);

		// The White Heart's Hammers: the heaviest steel on the road.
		s = Stage{};
		s.id = "c3m2"; s.name = "The White Weight";
		s.blurb = "It has no tricks left. It gave them all up for mass.";
		s.mode = 5; s.rank = 6; s.first_to = 1;
		s.role = kMiniboss; s.pair = 1;
		s.blade = "quench,bellows,spark,white_heat,every_twist";
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		s = Stage{};
		s.id = "c3b2"; s.name = "The Bellows Titan";
		s.blurb = "No warnings, no windows, and the heaviest blade the forge ever"
			" hung on anything.";
		s.mode = 5; s.rank = 7; s.first_to = 2;
		s.role = kBoss; s.pair = 1;
		s.blade = "bellows,white_heat,white_heat,overheat,gamble,heavy_hand,loaded_dice";
		s.slag_first = 70; s.slag_repeat = 14;
		list.push_back(s);

		// And its Tricksters: every trick the heart knows, thinly armed.
		s = Stage{};
		s.id = "c3m3"; s.name = "The Ember Sophist";
		s.blurb = "It would rather argue than swing, and it is very good at"
			" arguing.";
		s.mode = 5; s.rank = 6; s.first_to = 1;
		s.role = kMiniboss; s.pair = 2;
		s.blade = "quench,bellows,spark";
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		s = Stage{};
		s.id = "c3b3"; s.name = "The Chorus of Coals";
		s.blurb = "Lighter steel than the Heart, and every trick it ever learned,"
			" all at once.";
		s.mode = 5; s.rank = 7; s.first_to = 2;
		s.role = kBoss; s.pair = 2;
		s.blade = "white_heat,overheat,gamble,heavy_hand,loaded_dice";
		s.slag_first = 70; s.slag_repeat = 14;
		list.push_back(s);

		return list;
	}();
	return road;
}

} // namespace campaign
} // namespace forcetris
