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
			"The forge teaches one fire at a time.", 9},
		{"c2", "The Deep Forge",
			"Every lesson, turned against you.", 9},
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
		s.blurb = "Clear ten lines. The forge is patient, once.";
		s.mode = 0; s.quota = 10; s.par_seconds = 100;
		s.slag_first = 15; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s2"; s.name = "The Scrap Pile";
		s.blurb = "Old iron on the floor. Fifteen lines through it.";
		s.mode = 0; s.quota = 15; s.par_seconds = 130;
		s.board = "..77777.77\n777.777777\n7777777.77";
		s.slag_first = 18; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s3"; s.name = "The First Cut";
		s.blurb = "Ten rows of cheese, cut clean. Dig.";
		s.mode = 3; s.quota = 10; s.par_seconds = 110;
		s.cheese_holes = 1; s.cheese_messiness = 30;
		s.slag_first = 18; s.slag_repeat = 4;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s4"; s.name = "Loose Mortar";
		s.blurb = "Nothing holds: clears cascade. Fifteen lines.";
		s.mode = 0; s.quota = 15; s.par_seconds = 140;
		s.cleartype = 1;
		s.slag_first = 20; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s5"; s.name = "Rusted Joints";
		s.blurb = "The walls give nothing back: no kicks. Fifteen lines.";
		s.mode = 0; s.quota = 15; s.par_seconds = 150;
		s.no_kicks = true;
		s.slag_first = 20; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s6"; s.name = "The Rising Floor";
		s.blurb = "The floor climbs. Twelve lines before it takes you.";
		s.mode = 4; s.quota = 12; s.par_seconds = 150;
		s.cheese_period = 350;
		s.slag_first = 22; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s9"; s.name = "The Dark Gallery";
		s.blurb = "Only your lantern lights the well. Twelve lines.";
		s.mode = 0; s.quota = 12; s.par_seconds = 140;
		s.dim = true;
		s.slag_first = 22; s.slag_repeat = 5;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s7"; s.name = "Backdraft";
		s.blurb = "The fuse burns hot the whole way. Twenty lines.";
		s.mode = 0; s.quota = 20; s.par_seconds = 170;
		s.pressure = true; s.fuse_scale = 0.9;
		s.slag_first = 24; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c1s8"; s.name = "The Gatekeeper";
		s.blurb = "The yard's keeper, and its blade. Beat it out.";
		s.mode = 5; s.rank = 1; s.first_to = 1;
		s.slag_first = 40; s.slag_repeat = 8;
		list.push_back(s);

		// --- Chapter 2: The Deep Forge. --------------------------------
		s = Stage{};
		s.id = "c2s1"; s.name = "Heavier Air";
		s.blurb = "Faster gravity, a shorter wick. Twenty lines.";
		s.mode = 0; s.quota = 20; s.par_seconds = 160;
		s.fuse_scale = 0.85; s.fall_delay = 20;
		s.slag_first = 26; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s2"; s.name = "Three Cuts";
		s.blurb = "Fourteen rows, three holes each, cut wild.";
		s.mode = 3; s.quota = 14; s.par_seconds = 150;
		s.cheese_holes = 3; s.cheese_messiness = 100;
		s.slag_first = 26; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s3"; s.name = "Chain Collapse";
		s.blurb = "Linked cascade, every twist scored. Twenty lines.";
		s.mode = 0; s.quota = 20; s.par_seconds = 170;
		s.cleartype = 2; s.spin_rule = 3;
		s.slag_first = 28; s.slag_repeat = 6;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s9"; s.name = "Smoke in the Rafters";
		s.blurb = "The queue is smoked over: one piece ahead. Eighteen.";
		s.mode = 0; s.quota = 18; s.par_seconds = 170;
		s.fog = true;
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s4"; s.name = "The Overheated Wing";
		s.blurb = "Overheat is already in your blood. Twenty lines, hot.";
		s.mode = 0; s.quota = 20; s.par_seconds = 160;
		s.pressure = true; s.fuse_scale = 0.8;
		s.tempers = "overheat";
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s5"; s.name = "The Buried Hall";
		s.blurb = "Deep rubble and rigid walls. Eighteen lines out.";
		s.mode = 0; s.quota = 18; s.par_seconds = 180;
		s.no_kicks = true;
		s.board = "7.77777777\n77777.7777\n777.777777\n7777777.77\n77.7777777";
		s.slag_first = 30; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s6"; s.name = "The Flood";
		s.blurb = "The floor climbs faster here. Fifteen lines.";
		s.mode = 4; s.quota = 15; s.par_seconds = 180;
		s.cheese_period = 250;
		s.slag_first = 32; s.slag_repeat = 7;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s7"; s.name = "Cold Iron";
		s.blurb = "The deepest room: fast, short, unforgiving. 25 lines.";
		s.mode = 0; s.quota = 25; s.par_seconds = 200;
		s.fuse_scale = 0.75; s.fall_delay = 15;
		s.slag_first = 34; s.slag_repeat = 8;
		list.push_back(s);

		s = Stage{};
		s.id = "c2s8"; s.name = "The Forgemaster";
		s.blurb = "Two falls against the master and the master's blade.";
		s.mode = 5; s.rank = 4; s.first_to = 2;
		s.blade = "bellows,white_heat,overheat,gamble";
		s.slag_first = 60; s.slag_repeat = 12;
		list.push_back(s);

		return list;
	}();
	return road;
}

} // namespace campaign
} // namespace forcetris
