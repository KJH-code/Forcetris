// The Forcetris GUI: the graded sim with a face on it.
//
// The game inside the window is cpp/src/sim.cpp - the code the trace harness
// proves agrees with the Python engine move for move - fed from the keyboard
// instead of a script. This file only draws what the sim reports and routes
// what the player asks: menus and settings are Dear ImGui, the board is
// plain SDL rectangles, and every screen is mouse-first.
//
// FORCETRIS_SMOKE=<frames> runs the game headlessly for that many frames
// with scripted-random input and exits, which is how the build is tested on
// machines with no display.
#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <SDL.h>
#ifdef __ANDROID__
// On Android SDL owns the real entry point; main below becomes SDL_main.
#include <SDL_main.h>
#endif

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"

#include "audio.hpp"
#include "config.hpp"
#include "forcetris/career.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/munch.hpp"
#include "forcetris/profile.hpp"
#include "forcetris/rating.hpp"
#include "forcetris/replay.hpp"
#include "session.hpp"
#include "stats.hpp"
#include "versus.hpp"

namespace forcetris {
namespace gui {
namespace {

// The board's place on screen. The stat panels anchor to its right edge, so
// their saved positions survive a window resize. Everything is laid out in
// the same 96-dpi units it always was and multiplied once by the display's
// scale, so a laptop at 150% gets the same picture with more pixels in it
// instead of a stretched copy of the 100% one.
float kScale = 1.f;
int kCell = 26;
int kBoardX = 300;
int kBoardY = 48;
int kBoardW = kWidth * kCell;
int kBoardH = kHeight * kCell;

// A design-unit measure scaled to the display, for the layout literals.
float ui (float units) {
	return units * kScale;
}

int px (float units) {
	return static_cast<int>(std::lround(units * kScale));
}

// The phone build: true on Android (or under FORCETRIS_MOBILE=1, which is
// how the layouts are exercised on a desk). Off, nothing below changes a
// pixel of the desktop game.
bool kMobile = false;
bool kPortrait = false;
// Where the versus opponent's mini board sits - portrait has no right
// margin to put it in, so the spot moves with the layout.
int kMiniX = 940;
int kMiniY = 88;
int kMiniCell = 13;
int kPreviews = 5;   // Portrait trims the queue to make room.

void apply_ui_scale (float scale) {
	kScale = scale;
	kCell = px(26);
	kBoardX = px(300);
	kBoardY = px(48);
	kBoardW = kWidth * kCell;
	kBoardH = kHeight * kCell;
	kMiniX = px(940);
	kMiniY = kBoardY + px(40);
	kMiniCell = px(13);
	kPreviews = 5;
}

// The phone layouts. Landscape is the desktop design fitted to the screen
// and centred; portrait rebuilds the essential column - hold, board, queue
// - across the width, pushes it to the top, and leaves the bottom of the
// screen to the touch controls. The stat panels only draw in landscape.
void apply_mobile_layout (int w, int h) {
	kPortrait = h > w;
	if (!kPortrait) {
		const float scale = std::max(0.5f, std::min(w / 1180.f, h / 700.f));
		apply_ui_scale(scale);
		const int dx = std::max(0, (w - px(1180)) / 2);
		kBoardX += dx;
		kMiniX += dx;
	} else {
		// Essential units across: 122 of hold, 264 of board, 122 of queue,
		// margins - 540 in all, centred when the screen is wider.
		const float scale = std::max(0.5f, std::min(w / 540.f, h / 1150.f));
		apply_ui_scale(scale);
		kBoardX = px(134) + std::max(0, (w - px(540)) / 2);
		kBoardY = px(56);
		// Three previews instead of five, and the opponent's board shrinks
		// and tucks under them.
		kPreviews = 3;
		kMiniCell = px(8);
		kMiniX = kBoardX + kBoardW + px(18);
		kMiniY = kBoardY + px(348);
	}
}

// The typefaces, baked from a real vector font when one can be found: the
// stock ImGui font is a 13px bitmap, and every scaled-up use of it is what
// pixelated the old screens.
struct Fonts {
	ImFont* body = nullptr;
	ImFont* head = nullptr;    // Section headers, stat values, screen names.
	ImFont* title = nullptr;   // The FORCETRIS wordmark.
};

// The first face the machine actually has, tried in the order a player is
// likely to own them. FORCETRIS_FONT overrides the lot (a bold companion is
// guessed from the regular's name); missing everything falls back to the
// bitmap font, small enough to stay sharp.
std::string first_file (const std::vector<std::string>& candidates) {
	std::error_code ignored;
	for (const std::string& path : candidates) {
		if (!path.empty() && std::filesystem::is_regular_file(path, ignored)) {
			return path;
		}
	}
	return "";
}

std::pair<std::string, std::string> find_font_files () {
	if (const char* forced = std::getenv("FORCETRIS_FONT")) {
		// Guess the bold companion from the usual naming: X-Regular.ttf has
		// X-Bold.ttf beside it, plain X.ttf might have X-Bold.ttf.
		std::string bold = forced;
		const size_t tagged = bold.rfind("-Regular");
		const size_t dot = bold.rfind('.');
		if (tagged != std::string::npos) {
			bold.replace(tagged, 8, "-Bold");
		} else if (dot != std::string::npos) {
			bold.insert(dot, "-Bold");
		}
		return {forced, first_file({bold})};
	}
	const std::string regular = first_file({
		"C:\\Windows\\Fonts\\segoeui.ttf",
		"/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
		"/System/Library/Fonts/Supplemental/Arial.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
		"/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
		"/usr/share/fonts/TTF/DejaVuSans.ttf",
		"/system/fonts/Roboto-Regular.ttf",
	});
	const std::string bold = first_file({
		"C:\\Windows\\Fonts\\segoeuib.ttf",
		"/System/Library/Fonts/Supplemental/Arial Bold.ttf",
		"/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
		"/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
		"/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
		"/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf",
		"/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
		"/system/fonts/Roboto-Bold.ttf",
	});
	return {regular, bold};
}

Fonts load_fonts () {
	Fonts fonts;
	ImGuiIO& io = ImGui::GetIO();
	const auto [regular, bold] = find_font_files();
	if (!regular.empty()) {
		fonts.body = io.Fonts->AddFontFromFileTTF(regular.c_str(), ui(19.f));
		const std::string& heavy = bold.empty() ? regular : bold;
		fonts.head = io.Fonts->AddFontFromFileTTF(heavy.c_str(), ui(26.f));
		fonts.title = io.Fonts->AddFontFromFileTTF(heavy.c_str(), ui(44.f));
	}
	if (fonts.body == nullptr) {
		SDL_Log("no scalable font found - falling back to the bitmap font");
		fonts.body = io.Fonts->AddFontDefault();
	}
	if (fonts.head == nullptr) {
		fonts.head = fonts.body;
	}
	if (fonts.title == nullptr) {
		fonts.title = fonts.head;
	}
	return fonts;
}

// The theme: the board's own palette carried into the chrome. One accent -
// the I piece's cyan - against slate, rounded corners, and room to breathe;
// the stock StyleColorsDark is a debugger's colour scheme, not a game's.
void apply_theme () {
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 10.f;
	style.ChildRounding = 8.f;
	style.FrameRounding = 6.f;
	style.PopupRounding = 8.f;
	style.GrabRounding = 6.f;
	style.TabRounding = 6.f;
	style.ScrollbarRounding = 8.f;
	style.WindowPadding = ImVec2(20.f, 16.f);
	style.FramePadding = ImVec2(11.f, 6.f);
	style.ItemSpacing = ImVec2(10.f, 9.f);
	style.ItemInnerSpacing = ImVec2(8.f, 6.f);
	style.CellPadding = ImVec2(8.f, 5.f);
	style.ScrollbarSize = 12.f;
	style.GrabMinSize = 12.f;
	style.WindowBorderSize = 1.f;
	style.FrameBorderSize = 0.f;
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.SeparatorTextBorderSize = 2.f;

	const ImVec4 canvas(0.070f, 0.086f, 0.118f, 0.98f);   // #12161E
	const ImVec4 well(0.118f, 0.145f, 0.196f, 1.f);       // #1E2532
	const ImVec4 wellHover(0.153f, 0.188f, 0.251f, 1.f);
	const ImVec4 wellActive(0.180f, 0.227f, 0.306f, 1.f);
	const ImVec4 accent(0.255f, 0.776f, 0.878f, 1.f);     // The I piece.
	const ImVec4 accentDim(0.255f, 0.776f, 0.878f, 0.28f);
	const ImVec4 edge(0.165f, 0.196f, 0.259f, 0.65f);
	const ImVec4 text(0.910f, 0.929f, 0.957f, 1.f);
	const ImVec4 faded(0.486f, 0.529f, 0.596f, 1.f);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = text;
	colors[ImGuiCol_TextDisabled] = faded;
	colors[ImGuiCol_WindowBg] = canvas;
	colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.086f, 0.106f, 0.145f, 0.98f);
	colors[ImGuiCol_Border] = edge;
	colors[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_FrameBg] = well;
	colors[ImGuiCol_FrameBgHovered] = wellHover;
	colors[ImGuiCol_FrameBgActive] = wellActive;
	colors[ImGuiCol_TitleBg] = ImVec4(0.055f, 0.067f, 0.094f, 1.f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.078f, 0.098f, 0.137f, 1.f);
	colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_MenuBarBg] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_ScrollbarGrab] = well;
	colors[ImGuiCol_ScrollbarGrabHovered] = wellHover;
	colors[ImGuiCol_ScrollbarGrabActive] = wellActive;
	colors[ImGuiCol_CheckMark] = accent;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = ImVec4(0.42f, 0.86f, 0.94f, 1.f);
	colors[ImGuiCol_Button] = well;
	colors[ImGuiCol_ButtonHovered] = wellHover;
	colors[ImGuiCol_ButtonActive] = wellActive;
	colors[ImGuiCol_Header] = well;
	colors[ImGuiCol_HeaderHovered] = wellHover;
	colors[ImGuiCol_HeaderActive] = wellActive;
	colors[ImGuiCol_Separator] = edge;
	colors[ImGuiCol_SeparatorHovered] = accentDim;
	colors[ImGuiCol_SeparatorActive] = accent;
	colors[ImGuiCol_ResizeGrip] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_ResizeGripHovered] = accentDim;
	colors[ImGuiCol_ResizeGripActive] = accent;
	colors[ImGuiCol_Tab] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_TabHovered] = wellHover;
	colors[ImGuiCol_TabSelected] = well;
	colors[ImGuiCol_TabSelectedOverline] = accent;
	colors[ImGuiCol_TabDimmed] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_TabDimmedSelected] = well;
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.086f, 0.106f, 0.145f, 1.f);
	colors[ImGuiCol_TableBorderStrong] = edge;
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.165f, 0.196f, 0.259f, 0.35f);
	colors[ImGuiCol_TableRowBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.f, 1.f, 1.f, 0.02f);
	colors[ImGuiCol_TextSelectedBg] = accentDim;
	colors[ImGuiCol_NavHighlight] = accent;
	colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.02f, 0.03f, 0.05f, 0.6f);

	style.ScaleAllSizes(kScale);
}

// One colour per form, matching the Python game's palette closely enough
// that a player moving between the two is not surprised.
const SDL_Color kFormColors[8] = {
	{65, 198, 224, 255},   // I
	{242, 201, 76, 255},   // O
	{155, 81, 224, 255},   // T
	{111, 207, 87, 255},   // S
	{235, 87, 87, 255},    // Z
	{62, 123, 224, 255},   // J
	{242, 153, 74, 255},   // L
	{122, 122, 122, 255},  // garbage
};

enum class Screen {
	Menu, Modes, Game, Over, Replays, Viewer, Scores, Help, Analysis,
	Profile, Career };

// A replay being watched: which placement, which stop along its journey.
struct Viewing {
	replay::Replay game;
	int index = 0;
	int step = 0;
	bool playing = true;
	int speed = 1;
	bool fixed = false;      // Walk the finesse routes instead of the trails.
	double carry = 0.;
	Screen back = Screen::Menu;
};

struct App {
	SDL_Window* window = nullptr;
	SDL_Renderer* renderer = nullptr;
	Config config;
	std::string config_file;
	std::string root;
	Audio audio;
	std::optional<Session> session;
	std::optional<replay::Replay> last_replay;
	std::vector<replay::Replay> shelf;   // The browser's listing.
	std::optional<Viewing> viewing;
	// The replay the analysis screen is showing, and where it came from.
	std::optional<replay::Replay> studying;
	Screen study_back = Screen::Menu;
	Screen screen = Screen::Menu;
	Screen help_back = Screen::Menu;      // How to play returns where it came from.
	Fonts fonts;
	bool paused = false;
	bool editing = false;        // The stat layout editor is live.
	// The pre-game countdown: frames left before the sim starts stepping,
	// and how many a fresh game deals (zero under the smoke harness, whose
	// frame budget the countdown would silently eat).
	int countdown = 0;
	int start_delay = 150;
	// The editor opened from the menu, over a throwaway board: closing it
	// drops the preview game and lands back in the settings screen.
	bool layout_preview = false;
	bool show_settings = false;
	bool place_panels = false;   // Push saved positions into ImGui this frame.
	std::string rebinding;       // Action waiting for its next key, if any.
	int mode = 0;                // The gametype the current game was started as.
	// The mode picker's detail window: 0 none, 1 cheese, 2 versus. The
	// picker shows one entry per family; the dials themselves live in the
	// config, saved with the rest of it.
	int mode_popup = 0;
	// The phone build's on-screen controls: buttons feeding the same key
	// path the keyboard does. A hardware key hides them; a touch brings
	// them back.
	struct TouchButton {
		Key key;
		const char* label;
		SDL_Rect rect;
	};
	std::vector<TouchButton> touch;
	std::vector<profile::GameRecord> history;   // The profile screen's data.
	// The career: ladder stars and the daily latch, plus the live match's
	// place in it - which stage is being fought (-1 for a free duel),
	// whether the player ignited Overdrive this match, and whether the run
	// in play is today's one daily attempt.
	career::State career;
	int career_stage = -1;
	bool career_od = false;
	bool daily_run = false;
	std::map<SDL_FingerID, size_t> touch_held;
	bool touch_shown = true;
	bool relayout = false;       // The screen rotated; rebuild before drawing.
	// The match against the bot, when one is on.
	std::optional<VersusMatch> versus;
	int score_page = 0;          // The high score table being looked at.
	int hiscore_place = -1;      // Where the finished game would place, if it does.
	char name_entry[9] = "";
	bool score_saved = false;
	std::mt19937 seeds{std::random_device{}()};
	bool quit = false;
};

// The one owner of the mode-to-name mapping. The fused axis matters: a
// fuse-rules game and a trainer-rules game are different games, so their
// records, tables and history keep different keys - and the legacy strings
// are frozen forever, or old files would silently change meaning.
const char* gametype_name (int mode, bool fused) {
	if (fused) {
		switch (mode) {
			case 1: return "blaze";
			case 2: return "inferno";
			case 3: return "meltdown";
			case 4: return "bunker";
			case 5: return "duel";
			default: return "ignition";
		}
	}
	switch (mode) {
		case 1: return "timed";
		case 2: return "arcade";
		case 3: return "cheese_race";
		case 4: return "cheese_survival";
		case 5: return "versus";
		default: return "free";
	}
}

std::string place_string (int at) {
	if (at == 0) return "1st";
	if (at == 1) return "2nd";
	if (at == 2) return "3rd";
	return std::to_string(at + 1) + "th";
}

replay::Meta meta_for (const Config& config, int mode) {
	replay::Meta meta;
	char stamp[32] = "";
	const std::time_t now = std::time(nullptr);
	if (std::tm* local = std::localtime(&now)) {
		std::strftime(stamp, sizeof stamp, "%Y-%m-%dT%H:%M:%S", local);
	}
	meta.played = stamp;
	meta.forced_delay = config.forced_delay;
	meta.finesse = config.finesse_rule;
	meta.spinrule = config.spin_rule;
	meta.cleartype = config.cleartype;
	meta.das = config.das;
	meta.arr = config.arr;
	meta.dcd = config.dcd;
	meta.sdf = config.sdf;
	meta.are = config.are;
	// The fuse ruleset, tunables and all - a file must say which game its
	// score belongs to. Duel fights under it too, both sides alike.
	const SimConfig rules = config.sim();
	meta.fuse = rules.fuse;
	meta.gametype = gametype_name(mode, meta.fuse);
	if (meta.fuse) {
		meta.fuse_base = rules.fuse_base;
		meta.fuse_min = rules.fuse_min;
		meta.fuse_decay = rules.fuse_decay;
		meta.fuse_bank_cap = rules.fuse_bank_cap;
		meta.fuse_draw_cap = rules.fuse_draw_cap;
		meta.fuse_refuel_line = rules.fuse_refuel_line;
		meta.fuse_refuel_attack = rules.fuse_refuel_attack;
		meta.flash_frac = rules.flash_frac;
		meta.flash_floor = rules.flash_floor;
		meta.flow_lock_gain = rules.flow_lock_gain;
		meta.flow_flash_gain = rules.flow_flash_gain;
		meta.flow_burn_loss = rules.flow_burn_loss;
		meta.overdrive_secs = rules.overdrive_secs;
		meta.overdrive_mult = rules.overdrive_mult;
	}
	return meta;
}

void start_game (App& app, int mode,
		std::optional<unsigned> fixed_seed = std::nullopt) {
	app.versus.reset();
	app.career_stage = -1;
	app.daily_run = false;
	app.mode = mode;
	// The dials just used are worth keeping even if the app never gets a
	// clean exit - phones rarely grant one.
	save_config(app.config, app.config_file);
	SimConfig config = app.config.sim();
	config.gametype = mode;
	if (config.fuse && mode == 1) {
		// Blaze burns three minutes, not the trainer's five. Its own table
		// keeps its scores, so the shorter clock competes only with itself.
		config.timer_ms = 3 * 60 * 1000;
	}
	config.cheese_total = app.config.cheese_total;
	config.cheese_period = app.config.cheese_period;
	config.cheese_holes = app.config.cheese_holes;
	config.cheese_messiness = app.config.cheese_messiness;
	app.session.emplace(config,
		fixed_seed.has_value() ? *fixed_seed : app.seeds(),
		meta_for(app.config, mode));
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.countdown = app.start_delay;
	app.audio.start_music();
}

// A match against the bot: the player's session as ever, the opponent and
// the scoreboard beside it.
void start_versus (App& app, int career_stage = -1) {
	app.mode = 5;
	app.career_stage = career_stage;
	app.career_od = false;
	app.daily_run = false;
	save_config(app.config, app.config_file);
	SimConfig config = app.config.sim();
	config.gametype = 5;
	config.cheese_holes = 1;
	config.cheese_messiness = 30;
	int rank = app.config.bot_rank;
	int first_to = app.config.first_to;
	if (career_stage >= 0) {
		// A ladder stage names its own terms: the stage's rank, longer
		// matches up top, and a fuse that tightens one notch per rung.
		rank = career_stage;
		first_to = career_stage >= 4 ? 2 : 1;
		config.fuse_base = std::max(config.fuse_min,
			config.fuse_base - 0.1 * career_stage);
	}
	const replay::Meta meta = meta_for(app.config, 5);
	app.session.emplace(config, app.seeds(), meta);
	app.versus.emplace(rank, first_to);
	app.versus->begin_round(config, app.seeds(), meta);
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.countdown = app.start_delay;
	app.audio.start_music();
}

// The finished round, both boards: the player's replay with the bot's side
// embedded under it. Nothing when the player's half was too short to keep.
std::optional<replay::Replay> finish_round (App& app) {
	std::optional<replay::Replay> done = app.session->finish();
	if (done.has_value() && app.versus.has_value()
		&& app.versus->bot.has_value()) {
		if (auto other = app.versus->bot->finish(true)) {
			done->opponent.emplace(replay::Opponent{
				std::move(other->meta), std::move(other->placements)});
		}
	}
	return done;
}

// One line of history for a finished game: the summary's figures, the
// rating estimate, and the munch numbers, appended to the profile file.
// `won` is the versus verdict - 1, 0, or -1 for draws and other modes.
void record_game (App& app, const replay::Replay& game, int won) {
	profile::GameRecord record;
	record.played = game.meta.played;
	record.gametype = game.meta.gametype;
	const replay::Summary sum = game.summary();
	record.seconds = sum.seconds;
	record.pieces = sum.placements;
	record.lines = sum.lines;
	record.score = sum.score;
	record.attack = sum.attack;
	record.downstack = game.meta.downstack;
	record.pps = sum.pps;
	record.apm = sum.apm;
	record.vs = sum.vs;
	record.finesse = sum.judged > 0 ? sum.rate * 100. : 100.;
	const rating::Estimate estimate
		= rating::estimate(sum.apm, sum.pps, sum.vs);
	if (estimate.rank[0] != '\0') {
		record.tr = estimate.tr;
	}
	record.won = won;
	for (const auto& group : munch::crunch(game).groups) {
		for (const auto& stat : group.stats) {
			record.stats[stat.id] = stat.value;
		}
	}
	profile::append(profile::path(app.root), record);
}

// The versus verdict of the round just decided, for the history line.
int round_verdict (const App& app) {
	if (!app.versus.has_value()) {
		return -1;
	}
	return app.versus->round_draw ? -1 : app.versus->round_player_won ? 1 : 0;
}

void next_versus_round (App& app) {
	// The round just decided is a game in its own right: save it - both
	// boards - before the fresh sessions sweep it away.
	if (auto done = finish_round(app)) {
		replay::save(*done, replay::folder(app.root));
		record_game(app, *done, round_verdict(app));
	}
	SimConfig config = app.config.sim();
	config.gametype = 5;
	config.cheese_holes = 1;
	config.cheese_messiness = 30;
	const replay::Meta meta = meta_for(app.config, 5);
	app.session.emplace(config, app.seeds(), meta);
	app.versus->round += 1;
	app.versus->begin_round(config, app.seeds(), meta);
	app.countdown = app.start_delay;
}

void end_game (App& app) {
	// Saved rather than offered, the way the Python game does it: the moment
	// a run ends is the worst moment to ask someone whether they will want
	// to look at it.
	app.screen = Screen::Over;
	app.audio.fade_music(2.5);
	app.last_replay = finish_round(app);
	if (app.last_replay.has_value()) {
		replay::save(*app.last_replay, replay::folder(app.root));
		record_game(app, *app.last_replay, round_verdict(app));
	}
	// The career's verdicts, before the table probe. A ladder stage pays
	// stars - one for the win, two for a sweep, three for a sweep with
	// Overdrive ignited - and only ever upward; the daily writes its score
	// onto the latch burned when it started.
	if (app.career_stage >= 0 && app.versus.has_value()) {
		const bool won = app.versus->player_wins > app.versus->bot_wins;
		const bool sweep = won && app.versus->bot_wins == 0;
		const int stars = won ? (sweep ? (app.career_od ? 3 : 2) : 1) : 0;
		const std::string rank = bot::ranks()[app.career_stage].name;
		if (stars > app.career.stars[rank]) {
			app.career.stars[rank] = stars;
			career::save(career::path(app.root), app.career);
		}
	}
	if (app.daily_run) {
		app.career.daily_score = std::max<long long>(
			app.career.daily_score, app.session->sim().final_score());
		career::save(career::path(app.root), app.career);
		app.daily_run = false;
	}
	// Would this run make the table? The probe carries the raw clock value,
	// exactly as eval_loss probes it - the conversion to stored centiseconds
	// only happens if a name is entered and the score actually submitted.
	// The loss-time counters: eval_loss probes before a still-resolving
	// clear lands its points, so the snapshot does too.
	const bool fused = app.session->sim().config().fuse;
	if (fused) {
		// A fuse-rules game competes in the variant's own tables, every
		// mode with a table of its own.
		const Sim& sim = app.session->sim();
		hiscore::Entry probe;
		probe.score = static_cast<std::uint64_t>(
			std::max<long long>(0, sim.final_score()));
		probe.lines = static_cast<std::uint32_t>(std::max(0, sim.final_lines()));
		probe.timer = static_cast<std::uint32_t>(std::max(0L, sim.timer_ms()));
		const int at = hiscore::place_fuse(
			hiscore::load_fuse(hiscore::folder(app.root)),
			gametype_name(app.mode, true), probe);
		app.hiscore_place = at < hiscore::kPerTable ? at : -1;
	} else if (app.mode >= 3) {
		// The cheese modes are this side's own; the trainer score file is
		// the Python game's, three tables and no more, byte-compatible.
		app.hiscore_place = -1;
	} else {
		const Sim& sim = app.session->sim();
		hiscore::Entry probe;
		probe.score = static_cast<std::uint64_t>(
			std::max<long long>(0, sim.final_score()));
		probe.lines = static_cast<std::uint32_t>(std::max(0, sim.final_lines()));
		probe.timer = static_cast<std::uint32_t>(std::max(0L, sim.timer_ms()));
		const int at = hiscore::place(
			hiscore::load(hiscore::folder(app.root)),
			gametype_name(app.mode, false), probe);
		app.hiscore_place = at < hiscore::kPerTable ? at : -1;
	}
	app.name_entry[0] = '\0';
	app.score_saved = false;
}

void watch (App& app, replay::Replay game, Screen back) {
	app.viewing.emplace();
	app.viewing->game = std::move(game);
	app.viewing->back = back;
	app.screen = Screen::Viewer;
}

// --- Input. ----------------------------------------------------------------

std::optional<Key> key_for (const Config& config, SDL_Scancode code) {
	for (const ActionDef& action : all_actions()) {
		const auto found = config.keys.find(action.id);
		if (found == config.keys.end()) {
			continue;
		}
		for (const int bound : found->second) {
			if (bound == code) {
				return action.key;
			}
		}
	}
	return std::nullopt;
}

// The next key pressed becomes the binding being waited for. A key is bound
// to one action at a time, so it is taken away from wherever it was.
void take_binding (App& app, SDL_Scancode code) {
	for (auto& [action, codes] : app.config.keys) {
		codes.erase(std::remove(codes.begin(), codes.end(), static_cast<int>(code)),
			codes.end());
	}
	app.config.keys[app.rebinding].push_back(code);
	app.rebinding.clear();
}

void handle_event (App& app, const SDL_Event& event) {
	ImGui_ImplSDL2_ProcessEvent(&event);
	if (event.type == SDL_QUIT) {
		app.quit = true;
		return;
	}
	if (kMobile && event.type == SDL_WINDOWEVENT
		&& event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
		// The screen rotated (or the window changed shape): rebuild the
		// layout and the fonts before the next frame, not mid-draw.
		app.relayout = true;
		return;
	}
	if (event.type == SDL_FINGERDOWN || event.type == SDL_FINGERUP
		|| event.type == SDL_FINGERMOTION) {
		if (!kMobile || app.renderer == nullptr) {
			return;
		}
		int w = 0;
		int h = 0;
		SDL_GetRendererOutputSize(app.renderer, &w, &h);
		const int x = static_cast<int>(event.tfinger.x * w);
		const int y = static_cast<int>(event.tfinger.y * h);
		if (event.type == SDL_FINGERDOWN) {
			app.touch_shown = true;   // Any touch calls the buttons back.
			if (app.screen == Screen::Game && !app.paused && !app.editing
				&& app.countdown <= 0 && app.session.has_value()) {
				for (size_t i = 0; i < app.touch.size(); ++i) {
					const SDL_Rect& rect = app.touch[i].rect;
					if (x >= rect.x && x < rect.x + rect.w
						&& y >= rect.y && y < rect.y + rect.h) {
						app.touch_held[event.tfinger.fingerId] = i;
						app.session->key(app.touch[i].key, true);
						break;
					}
				}
			}
		} else if (event.type == SDL_FINGERUP) {
			// Always released, whatever screen we are on now - a finger
			// that outlives its round must not leave a key stuck down.
			const auto found = app.touch_held.find(event.tfinger.fingerId);
			if (found != app.touch_held.end()) {
				if (app.session.has_value()
					&& found->second < app.touch.size()) {
					app.session->key(app.touch[found->second].key, false);
				}
				app.touch_held.erase(found);
			}
		} else if (app.touch_held.find(event.tfinger.fingerId)
			== app.touch_held.end()) {
			// A finger dragging outside the game buttons scrolls whatever
			// window it is over, the way phones scroll. ImGui's vertical
			// wheel step is five font-heights, so dividing the drag by
			// that keeps the content tracking the finger.
			ImGui::GetIO().AddMouseWheelEvent(0.f,
				event.tfinger.dy * h / (5.f * ui(19.f)));
		}
		return;
	}
	if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) {
		return;
	}
	const bool down = event.type == SDL_KEYDOWN;
	if (down && event.key.repeat) {
		// The sim runs its own DAS; the OS keyboard repeat is not part of it.
		return;
	}
	if (down && !app.rebinding.empty()) {
		// Escape stays the pause key, so it backs out rather than binding.
		if (event.key.keysym.scancode != SDL_SCANCODE_ESCAPE) {
			take_binding(app, event.key.keysym.scancode);
		} else {
			app.rebinding.clear();
		}
		return;
	}
	if (down && (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE
		|| event.key.keysym.scancode == SDL_SCANCODE_AC_BACK)) {
		// One step back, whatever is in front: the editor first, then an
		// open settings window, then the screen itself.
		if (app.editing) {
			// The editor's Done from the keyboard: closed and saved. The
			// main loop returns a preview board to the settings screen.
			app.editing = false;
			save_config(app.config, app.config_file);
		} else if (app.show_settings) {
			app.show_settings = false;
		} else if (app.screen == Screen::Game) {
			app.paused = !app.paused;
		} else if (app.screen == Screen::Viewer && app.viewing.has_value()) {
			app.screen = app.viewing->back;
			app.viewing.reset();
		} else if (app.screen == Screen::Analysis) {
			app.screen = app.study_back;
			app.studying.reset();
		} else if (app.screen == Screen::Modes) {
			// A detail window closes back to the picker; the picker closes
			// back to the menu.
			if (app.mode_popup != 0) {
				app.mode_popup = 0;
			} else {
				app.screen = Screen::Menu;
			}
		} else if (app.screen == Screen::Help) {
			app.screen = app.help_back;
		} else if (app.screen == Screen::Over
			&& !ImGui::GetIO().WantCaptureKeyboard) {
			// Not while a high score name is being typed - Escape there is
			// ImGui's, to leave the text field.
			app.versus.reset();
			app.screen = Screen::Menu;
		} else if (app.screen == Screen::Replays
			|| app.screen == Screen::Scores
			|| app.screen == Screen::Profile
			|| app.screen == Screen::Career) {
			app.screen = Screen::Menu;
		}
		return;
	}
	// R restarts the run, hardcoded like Escape rather than rebindable - a
	// binding would put it in the smoke masher's pool, and the masher would
	// never finish a game again. Works paused and on the loss screen; stays
	// out of the layout editor's preview board and of ImGui's text fields
	// (the high score name entry types Rs of its own).
	if (down && event.key.keysym.scancode == SDL_SCANCODE_R
		&& !ImGui::GetIO().WantCaptureKeyboard
		&& ((app.screen == Screen::Game && !app.editing && !app.layout_preview)
			|| app.screen == Screen::Over)) {
		if (app.mode == 5) {
			start_versus(app, app.career_stage);
		} else {
			start_game(app, app.mode);
		}
		return;
	}
	if (app.screen != Screen::Game || app.paused || app.editing
		|| app.countdown > 0 || ImGui::GetIO().WantCaptureKeyboard) {
		return;
	}
	if (const auto key = key_for(app.config, event.key.keysym.scancode)) {
		if (kMobile) {
			// A hardware keyboard is talking; the buttons step aside until
			// the next touch.
			app.touch_shown = false;
		}
		app.session->key(*key, down);
	}
}

// --- The board and its trimmings, in plain rectangles. ---------------------

void fill (SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c) {
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	const SDL_Rect rect{x, y, w, h};
	SDL_RenderFillRect(renderer, &rect);
}

void draw_cell (SDL_Renderer* renderer, int px, int py, SDL_Color c, int size = kCell) {
	fill(renderer, px + 1, py + 1, size - 2, size - 2, c);
}

// A piece drawn on its own, for the hold box and the queue previews.
void draw_preview (SDL_Renderer* renderer, int form, int left, int top, int size) {
	if (form < 0 || form > 6) {
		return;
	}
	const Piece piece{form, 0, 0, 0};
	for (const Offset cell : cells_of(piece)) {
		draw_cell(renderer, left + (cell.x + 1) * size, top + (cell.y + 1) * size,
			kFormColors[form], size);
	}
}

void draw_board (App& app) {
	SDL_Renderer* renderer = app.renderer;
	const Session& session = *app.session;
	const Sim& sim = session.sim();

	fill(renderer, kBoardX - px(3), kBoardY - px(3), kBoardW + px(6), kBoardH + px(6), {32, 40, 53, 255});
	fill(renderer, kBoardX, kBoardY, kBoardW, kBoardH, {14, 18, 24, 255});
	SDL_SetRenderDrawColor(renderer, 26, 33, 44, 255);
	for (int x = 1; x < kWidth; ++x) {
		SDL_RenderDrawLine(renderer, kBoardX + x * kCell, kBoardY,
			kBoardX + x * kCell, kBoardY + kBoardH - 1);
	}
	for (int y = 1; y < kHeight; ++y) {
		SDL_RenderDrawLine(renderer, kBoardX, kBoardY + y * kCell,
			kBoardX + kBoardW - 1, kBoardY + y * kCell);
	}

	const Board& board = sim.board();
	for (int y = 0; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			const int form = board.at(x, y);
			if (form >= 0) {
				draw_cell(renderer, kBoardX + x * kCell, kBoardY + y * kCell,
					kFormColors[std::min(form, 7)]);
			}
		}
	}

	if (sim.entry() && sim.piece().form <= 6) {
		const Piece& piece = sim.piece();
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		const Piece ghost = board.dropped(piece);
		if (ghost.y != piece.y) {
			SDL_Color faint = kFormColors[piece.form];
			faint.a = 70;
			for (const Offset cell : cells_of(ghost)) {
				if (cell.y >= 0) {
					draw_cell(renderer, kBoardX + cell.x * kCell,
						kBoardY + cell.y * kCell, faint);
				}
			}
		}
		for (const Offset cell : cells_of(piece)) {
			if (cell.y >= 0) {
				draw_cell(renderer, kBoardX + cell.x * kCell,
					kBoardY + cell.y * kCell, kFormColors[piece.form]);
			}
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	}

	// The hold box and the coming pieces.
	fill(renderer, kBoardX - px(122), kBoardY, px(104), px(86), {20, 26, 34, 255});
	draw_preview(renderer, sim.stored(), kBoardX - px(122) + px(16), kBoardY + px(12), px(18));
	const auto& queue = sim.queue();
	for (int slot = 0; slot < kPreviews
		&& slot < static_cast<int>(queue.size()); ++slot) {
		fill(renderer, kBoardX + kBoardW + px(18), kBoardY + slot * px(92), px(104), px(86),
			{20, 26, 34, 255});
		draw_preview(renderer, queue[slot],
			kBoardX + kBoardW + px(18) + px(16), kBoardY + slot * px(92) + px(12), px(18));
	}

	// The fuse wick (or, on trainer rules, the flat forced-drop meter):
	// how much of the piece's stay is left. Frozen solid cyan under
	// Overdrive - the one moment the wick stops burning.
	const bool fused = sim.config().fuse;
	const double limit = fused ? sim.fuse_total() : app.config.forced_delay;
	if (limit > 0.) {
		fill(renderer, kBoardX, kBoardY + kBoardH + px(10), kBoardW, px(8), {26, 33, 44, 255});
		const auto elapsed = sim.piece_elapsed();
		if (elapsed.has_value()) {
			const double part = std::min(1.0, *elapsed / limit);
			SDL_Color wick{static_cast<Uint8>(90 + 165 * part),
				static_cast<Uint8>(200 - 140 * part), 80, 255};
			if (fused && sim.overdrive()) {
				wick = {90, 220, 235, 255};
			}
			fill(renderer, kBoardX, kBoardY + kBoardH + px(10),
				static_cast<int>(kBoardW * (1.0 - part)), px(8), wick);
		}
	}

	// The Flow gauge, climbing the board's left flank; Overdrive lights the
	// whole rail and rims the board while it burns.
	if (fused) {
		const int rail_x = kBoardX - px(20);
		const int rail_y = kBoardY + px(120);
		const int rail_h = kBoardH - px(130);
		fill(renderer, rail_x, rail_y, px(10), rail_h, {26, 33, 44, 255});
		const bool burning = sim.overdrive();
		const int charge = burning ? rail_h
			: static_cast<int>(rail_h * (sim.flow() / 100.));
		if (charge > 0) {
			const SDL_Color glow = burning
				? SDL_Color{255, 214, 96, 255} : SDL_Color{65, 198, 224, 255};
			fill(renderer, rail_x, rail_y + rail_h - charge, px(10), charge,
				glow);
		}
		if (burning) {
			const SDL_Color rim{255, 214, 96, 255};
			fill(renderer, kBoardX - px(4), kBoardY - px(4),
				kBoardW + px(8), px(4), rim);
			fill(renderer, kBoardX - px(4), kBoardY + kBoardH,
				kBoardW + px(8), px(4), rim);
			fill(renderer, kBoardX - px(4), kBoardY,
				px(4), kBoardH, rim);
			fill(renderer, kBoardX + kBoardW, kBoardY,
				px(4), kBoardH, rim);
		}
	}
}

// --- The ImGui layers: labels, stat panels, menus. -------------------------

void draw_label (const char* text, float x, float y, ImU32 color = IM_COL32(150, 165, 185, 255)) {
	ImGui::GetBackgroundDrawList()->AddText(ImVec2(x, y), color, text);
}

void draw_banner (App& app) {
	const Banner& banner = app.session->banner();
	if (banner.frame < 0) {
		return;
	}
	const long age = app.session->sim().frame() - banner.frame;
	if (age > 70) {
		return;
	}
	const float alpha = age < 50 ? 1.f : 1.f - (age - 50) / 20.f;
	ImFont* font = app.fonts.head;
	const float size = font->FontSize;
	const ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, banner.text.c_str());
	ImGui::GetForegroundDrawList()->AddText(font, size,
		ImVec2(kBoardX + (kBoardW - extent.x) / 2, kBoardY - ui(38)),
		IM_COL32(255, 210, 74, static_cast<int>(alpha * 255)), banner.text.c_str());
}

// The other board, small, and the wire's state around both: the bot's
// stack with its live piece, the scoreboard, and each side's incoming
// garbage as a red column beside its board.
void draw_versus_panel (App& app) {
	if (!app.versus.has_value() || !app.versus->bot.has_value()) {
		return;
	}
	VersusMatch& match = *app.versus;
	const Sim& theirs = match.bot->sim();
	SDL_Renderer* renderer = app.renderer;
	const int cell = kMiniCell;
	const int left = kMiniX;
	const int top = kMiniY;
	fill(renderer, left - px(2), top - px(2),
		kWidth * cell + px(4), kHeight * cell + px(4), {32, 40, 53, 255});
	fill(renderer, left, top, kWidth * cell, kHeight * cell, {14, 18, 24, 255});
	for (int y = 0; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			const int form = theirs.board().at(x, y);
			if (form >= 0) {
				fill(renderer, left + x * cell + 1, top + y * cell + 1,
					cell - 2, cell - 2, kFormColors[std::min(form, 7)]);
			}
		}
	}
	if (theirs.entry() && theirs.piece().form <= 6) {
		for (const Offset at : cells_of(theirs.piece())) {
			if (at.y >= 0) {
				fill(renderer, left + at.x * cell + 1, top + at.y * cell + 1,
					cell - 2, cell - 2, kFormColors[theirs.piece().form]);
			}
		}
	}
	draw_label("BOT", static_cast<float>(left), top - ui(22));
	// Incoming garbage, as red columns: theirs beside their board, the
	// player's beside the player's.
	const auto meter = [renderer] (int x, int bottom, int rows, int step) {
		const int shown = std::min(rows, kHeight);
		if (shown > 0) {
			fill(renderer, x, bottom - shown * step, px(6), shown * step,
				{224, 82, 82, 255});
		}
	};
	meter(kBoardX - px(10), kBoardY + kBoardH,
		app.session->sim().pending_garbage(), kCell);
	meter(left - px(10), top + kHeight * cell,
		theirs.pending_garbage(), cell);
	// The scoreboard: under the bot's board, except in portrait, where the
	// right margin is too narrow for it - there it sits under the player's.
	if (kPortrait) {
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kBoardX),
			static_cast<float>(kBoardY + kBoardH) + ui(24)));
	} else {
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(left) - ui(4),
			static_cast<float>(top + kHeight * cell) + ui(10)));
	}
	ImGui::Begin("versus score", nullptr, ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("You %d - %d Bot (%s)", match.player_wins, match.bot_wins,
		bot::ranks()[match.rank_index].name);
	ImGui::TextDisabled("first to %d  round %d", match.first_to, match.round);
	const int surge = app.session->sim().surge_charge();
	if (surge > 0) {
		ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f),
			"SURGE +%d", surge);
	}
	ImGui::End();
	// The round's verdict, front and centre while it lingers.
	if (match.phase != VersusMatch::Phase::Playing) {
		const char* verdict = match.round_draw ? "DRAW - AGAIN"
			: match.round_player_won ? "ROUND WON" : "ROUND LOST";
		ImFont* font = app.fonts.title;
		const ImVec2 extent = font->CalcTextSizeA(
			font->FontSize, FLT_MAX, 0.f, verdict);
		ImGui::GetForegroundDrawList()->AddText(font, font->FontSize,
			ImVec2(kBoardX + (kBoardW - extent.x) / 2,
				kBoardY + kBoardH / 2.f - font->FontSize),
			IM_COL32(255, 210, 74, 255), verdict);
	}
}

// The pre-game count, front and centre over the frozen board: 3, 2, 1.
void draw_countdown (App& app) {
	const int number = app.countdown / 50 + 1;
	char text[16];
	std::snprintf(text, sizeof text, "%d", number);
	ImFont* font = app.fonts.title;
	const ImVec2 extent = font->CalcTextSizeA(
		font->FontSize, FLT_MAX, 0.f, text);
	ImGui::GetForegroundDrawList()->AddText(font, font->FontSize,
		ImVec2(kBoardX + (kBoardW - extent.x) / 2,
			kBoardY + kBoardH / 2.f - font->FontSize),
		IM_COL32(255, 210, 74, 255), text);
}

// Where the phone's buttons sit. Portrait gets a two-row grid under the
// board; landscape gets a thumb cluster in each bottom corner.
void layout_touch (App& app, int w, int h) {
	app.touch.clear();
	app.touch_held.clear();
	if (!kMobile) {
		return;
	}
	const auto add = [&] (Key key, const char* label, int x, int y,
	                      int bw, int bh) {
		app.touch.push_back(App::TouchButton{key, label,
			SDL_Rect{x, y, bw, bh}});
	};
	if (kPortrait) {
		// Below the scoreboard's line, which owns the strip under the board.
		const int top = kBoardY + kBoardH + px(96);
		const int gap = px(8);
		const int bw = (w - gap * 5) / 4;
		const int bh = std::max(px(56), std::min(px(104),
			(h - top - gap * 3) / 2));
		const char* labels[8]
			= {"<", ">", "CCW", "CW", "SOFT", "DROP", "180", "HOLD"};
		const Key keys[8] = {Key::Left, Key::Right, Key::Ccw, Key::Cw,
			Key::Soft, Key::Hard, Key::Flip, Key::Hold};
		for (int i = 0; i < 8; ++i) {
			add(keys[i], labels[i], gap + (i % 4) * (bw + gap),
				top + (i / 4) * (bh + gap), bw, bh);
		}
	} else {
		const int bw = px(84);
		const int bh = px(84);
		const int gap = px(10);
		const int y1 = h - bh * 2 - gap * 2;
		const int y2 = h - bh - gap;
		add(Key::Soft, "SOFT", gap, y1, bw, bh);
		add(Key::Hold, "HOLD", gap * 2 + bw, y1, bw, bh);
		add(Key::Left, "<", gap, y2, bw, bh);
		add(Key::Right, ">", gap * 2 + bw, y2, bw, bh);
		add(Key::Ccw, "CCW", w - gap * 2 - bw * 2, y1, bw, bh);
		add(Key::Cw, "CW", w - gap - bw, y1, bw, bh);
		add(Key::Flip, "180", w - gap * 2 - bw * 2, y2, bw, bh);
		add(Key::Hard, "DROP", w - gap - bw, y2, bw, bh);
	}
}

void draw_touch (App& app) {
	if (!kMobile || !app.touch_shown || app.touch.empty()) {
		return;
	}
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	for (size_t i = 0; i < app.touch.size(); ++i) {
		const App::TouchButton& button = app.touch[i];
		bool held = false;
		for (const auto& [finger, at] : app.touch_held) {
			(void) finger;
			held = held || at == i;
		}
		const ImVec2 a(static_cast<float>(button.rect.x),
			static_cast<float>(button.rect.y));
		const ImVec2 b(a.x + button.rect.w, a.y + button.rect.h);
		draw->AddRectFilled(a, b, held ? IM_COL32(64, 198, 224, 80)
			: IM_COL32(255, 255, 255, 22), ui(12));
		draw->AddRect(a, b, IM_COL32(150, 165, 185, 90), ui(12));
		ImFont* font = app.fonts.head;
		const ImVec2 extent = font->CalcTextSizeA(
			font->FontSize, FLT_MAX, 0.f, button.label);
		draw->AddText(font, font->FontSize,
			ImVec2(a.x + (button.rect.w - extent.x) / 2,
				a.y + (button.rect.h - extent.y) / 2),
			IM_COL32(210, 220, 235, 210), button.label);
	}
}

void draw_stat_panels (App& app) {
	const ImVec2 origin(kBoardX + kBoardW + ui(140), static_cast<float>(kBoardY));
	for (const StatDef& stat : all_stats()) {
		const auto found = app.config.stats.find(stat.id);
		if (found == app.config.stats.end() || !found->second.shown) {
			continue;
		}
		StatSpot& spot = found->second;
		if (app.place_panels || !app.editing) {
			// The saved layout is in design units, the window in pixels.
			ImGui::SetNextWindowPos(
				ImVec2(origin.x + ui(spot.x), origin.y + ui(spot.y)));
		}
		ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing
			| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings;
		if (!app.editing) {
			flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoMouseInputs;
		}
		ImGui::PushStyleColor(ImGuiCol_WindowBg,
			app.editing ? ImVec4(0.16f, 0.22f, 0.32f, 0.9f) : ImVec4(0.07f, 0.09f, 0.12f, 0.65f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(ui(14), ui(8)));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(2)));
		ImGui::SetNextWindowSizeConstraints(
			ImVec2(ui(96), 0.f), ImVec2(FLT_MAX, FLT_MAX));
		ImGui::Begin((std::string("stat##") + stat.id).c_str(), nullptr, flags);
		ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", stat.label);
		ImGui::PushFont(app.fonts.head);
		ImGui::Text("%s", stat.value(*app.session).c_str());
		ImGui::PopFont();
		if (app.editing && !app.place_panels) {
			// Dragging writes straight back into the layout being saved,
			// in design units so the file means the same at every scale.
			const ImVec2 where = ImGui::GetWindowPos();
			spot.x = (where.x - origin.x) / kScale;
			spot.y = (where.y - origin.y) / kScale;
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
		ImGui::PopStyleColor();
	}
	app.place_panels = false;
}

void draw_layout_editor (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ui(20), ui(48)), ImGuiCond_Appearing);
	ImGui::Begin("Stat layout", &app.editing,
		ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextWrapped("Drag any panel where you want it. Tick a stat to add it.");
	ImGui::Separator();
	if (ImGui::BeginTable("editorstats", 2)) {
		for (const StatDef& stat : all_stats()) {
			StatSpot& spot = app.config.stats[stat.id];
			ImGui::TableNextColumn();
			if (ImGui::Checkbox(stat.label, &spot.shown) && spot.shown) {
				app.place_panels = true;
			}
		}
		ImGui::EndTable();
	}
	ImGui::Separator();
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted("Presets");
	for (const std::string& name : preset_names()) {
		ImGui::SameLine();
		if (ImGui::Button(name.c_str())) {
			apply_preset(app.config, name);
			app.place_panels = true;
		}
	}
	ImGui::Spacing();
	if (ImGui::Button(app.layout_preview ? "Back to settings" : "Done",
		ImVec2(ui(180), 0))) {
		app.editing = false;
	}
	ImGui::End();
	if (!app.editing) {
		save_config(app.config, app.config_file);
	}
}

// The stat layout editor, reachable from the pause menu and - through a
// throwaway preview board - from the settings screen and the main menu.
void open_layout_editor (App& app) {
	if (!app.session.has_value() || app.screen != Screen::Game) {
		app.session.emplace(app.config.sim(), app.seeds(),
			meta_for(app.config, 0));
		app.layout_preview = true;
		app.screen = Screen::Game;
	}
	app.paused = false;
	app.editing = true;
	app.place_panels = true;
	app.show_settings = false;
}

void draw_settings (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(70)),
		ImGuiCond_Appearing, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(560), 0));
	ImGui::Begin("Settings", &app.show_settings,
		ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::BeginTabBar("settings", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Handling")) {
			ImGui::Spacing();
			// AlwaysClamp: ctrl-click turns a slider into a raw input box,
			// and an unclamped sdf of 0 would divide the gravity by zero.
			ImGui::SliderInt("DAS (ms)", &app.config.das, 0, 330,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("ARR (ms)", &app.config.arr, 0, 83,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("DCD (ms)", &app.config.dcd, 0, 330,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("SDF (x, 40 = instant)", &app.config.sdf, 5, 40,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("ARE (ms)", &app.config.are, 0, 500,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::Spacing();
			float forced = static_cast<float>(app.config.forced_delay);
			if (ImGui::SliderFloat("Forced drop (s, 0 = off)", &forced,
				0.f, 5.f, "%.2f", ImGuiSliderFlags_AlwaysClamp)) {
				// Stored to the hundredth, exactly - the sim compares this
				// against a clock built from exact 0.02 steps, and a value
				// laundered through float lands a frame late.
				app.config.forced_delay
					= std::round(forced * 100.f) / 100.;
			}
			ImGui::Spacing();
			ImGui::TextDisabled("Handling applies from the next game.");
			ImGui::TextDisabled(
				"The engine runs 20ms frames; values land on that grid.");
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Rules")) {
			ImGui::Spacing();
			ImGui::Checkbox("Fuse rules", &app.config.fuse);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", app.config.fuse
				? "every piece burns; clears refuel; Flow ignites Overdrive"
				: "plain rules with the flat forced-drop delay");
			ImGui::Spacing();
			const char* spin_rules[] = {
				"Off", "T-spins", "All spins", "All spins + minis"};
			ImGui::Combo("Spins", &app.config.spin_rule, spin_rules, 4);
			const char* clear_styles[] = {
				"Naive", "Sticky cascade", "Linked cascade"};
			ImGui::Combo("Line clears", &app.config.cleartype, clear_styles, 3);
			ImGui::Checkbox("Clear delay", &app.config.clear_delay);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", app.config.clear_delay
				? "clears animate" : "clears resolve instantly");
			const char* finesse_rules[] = {
				"Off", "Count faults", "Retry on fault"};
			ImGui::Combo("Finesse", &app.config.finesse_rule, finesse_rules, 3);
			ImGui::Checkbox("Wall kicks", &app.config.kicks);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Sound")) {
			ImGui::Spacing();
			int sfx = static_cast<int>(std::lround(app.config.sfx_volume * 100.f));
			if (ImGui::SliderInt("Effects (%)", &sfx, 0, 100)) {
				app.config.sfx_volume = sfx / 100.f;
				app.audio.set_sfx_volume(app.config.sfx_volume);
			}
			int music = static_cast<int>(std::lround(app.config.music_volume * 100.f));
			if (ImGui::SliderInt("Music (%)", &music, 0, 100)) {
				app.config.music_volume = music / 100.f;
				app.audio.set_music_volume(app.config.music_volume);
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Keys")) {
			ImGui::Spacing();
			for (const ActionDef& action : all_actions()) {
				ImGui::PushID(action.id);
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(action.label);
				ImGui::SameLine(ui(150));
				std::vector<int>& codes = app.config.keys[action.id];
				int remove_at = -1;
				for (size_t i = 0; i < codes.size(); ++i) {
					ImGui::PushID(static_cast<int>(i));
					const char* name = SDL_GetScancodeName(
						static_cast<SDL_Scancode>(codes[i]));
					if (ImGui::SmallButton(
						name != nullptr && *name != '\0' ? name : "?")) {
						remove_at = static_cast<int>(i);
					}
					if (ImGui::IsItemHovered()) {
						ImGui::SetTooltip("Click to unbind");
					}
					ImGui::PopID();
					ImGui::SameLine();
				}
				if (remove_at >= 0) {
					codes.erase(codes.begin() + remove_at);
				}
				if (app.rebinding == action.id) {
					ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f),
						"press a key...");
				} else if (ImGui::SmallButton("+")) {
					// The next key pressed lands here; escape backs out. A key
					// taken from another action leaves it, so nothing fires
					// twice.
					app.rebinding = action.id;
				}
				ImGui::PopID();
			}
			ImGui::Spacing();
			if (ImGui::Button("Reset keys")) {
				app.config.keys = default_keys();
				app.rebinding.clear();
			}
			ImGui::TextDisabled(
			"Escape pauses and R restarts; neither can be bound.");
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Layout")) {
			ImGui::Spacing();
			ImGui::TextDisabled("The panels beside the board while you play.");
			ImGui::Spacing();
			if (ImGui::BeginTable("statgrid", 2)) {
				for (const StatDef& stat : all_stats()) {
					StatSpot& spot = app.config.stats[stat.id];
					ImGui::TableNextColumn();
					if (ImGui::Checkbox(stat.label, &spot.shown) && spot.shown) {
						app.place_panels = true;
					}
				}
				ImGui::EndTable();
			}
			ImGui::Spacing();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Presets");
			for (const std::string& name : preset_names()) {
				ImGui::SameLine();
				if (ImGui::Button(name.c_str())) {
					apply_preset(app.config, name);
					app.place_panels = true;
				}
			}
			ImGui::Spacing();
			if (ImGui::Button("Edit stat layout", ImVec2(ui(240), 0))) {
				// Over the running game if one is on, over a preview board
				// if not - either way the panels become draggable.
				open_layout_editor(app);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("drag the panels where you want them");
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::Separator();
	if (ImGui::Button("Save", ImVec2(ui(140), 0))) {
		save_config(app.config, app.config_file);
		app.show_settings = false;
	}
	ImGui::End();
	if (!app.show_settings) {
		app.rebinding.clear();
	}
}

void draw_summary (const Session& session) {
	// The figures a finished game is judged by, whatever panels were up.
	const struct { const char* label; const char* id; } rows[] = {
		{"Time", "time"}, {"Pieces", "pieces"}, {"PPS", "pps"},
		{"Lines", "lines"}, {"Attack", "attack"}, {"APM", "apm"},
		{"VS", "vs"}, {"Finesse", "finesse"}, {"Best B2B / combo", "b2b"},
	};
	if (ImGui::BeginTable("summary", 2)) {
		for (const auto& row : rows) {
			for (const StatDef& stat : all_stats()) {
				if (std::string(stat.id) == row.id) {
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", row.label);
					ImGui::TableSetColumnIndex(1);
					ImGui::TextUnformatted(stat.value(session).c_str());
				}
			}
		}
		ImGui::EndTable();
	}
}

// What the run was made of, as the Python analysis screen lists it. The rows
// come out of the core, where the cross test grades them against that
// screen's own text, so the two games cannot drift apart.
void draw_analysis_rows (const replay::Replay& game) {
	if (ImGui::BeginTable("analysis", 2)) {
		for (const auto& [label, value] : replay::analysis_rows(game)) {
			ImGui::TableNextRow();
			if (label.empty()) {
				// The spacers the Python screen leaves between the groups.
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(" ");
				continue;
			}
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", label.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(value.c_str());
		}
		ImGui::EndTable();
	}
}

// The analysis of a saved replay, opened from the browser. The loss screen
// shows the same rows inline, since a finished game is already looking at
// them.
// One label-value line in an analysis table.
void detail_row (const char* label, const std::string& value) {
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "%s", label);
	ImGui::TableSetColumnIndex(1);
	ImGui::TextUnformatted(value.c_str());
}

std::string detail_text (const char* spec, ...) {
	char buffer[96];
	va_list args;
	va_start(args, spec);
	std::vsnprintf(buffer, sizeof buffer, spec, args);
	va_end(args);
	return buffer;
}

// The attack tab: what the run sent and dug, per minute, per piece, per
// line - the figures the TetraStats crowd reads a league game by.
void draw_analysis_attack (const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const double pieces = std::max(1, s.placements);
	const double seconds = std::max(1e-9, s.seconds);
	const int ds = game.meta.downstack;
	if (ImGui::BeginTable("attack", 2)) {
		detail_row("Attack", detail_text("%d", s.attack));
		detail_row("APM", detail_text("%.1f", s.apm));
		detail_row("APP (attack/piece)", detail_text("%.2f", s.attack / pieces));
		detail_row("APL (attack/line)", detail_text("%.2f",
			s.lines > 0 ? s.attack / static_cast<double>(s.lines) : 0.));
		detail_row("Downstack", detail_text("%d", ds));
		detail_row("DS/piece", detail_text("%.2f", ds / pieces));
		detail_row("DS/second", detail_text("%.2f", ds / seconds));
		detail_row("VS", detail_text("%.1f", s.vs));
		detail_row("VS/APM", detail_text("%.2f",
			s.apm > 0. ? s.vs / s.apm : 0.));
		ImGui::EndTable();
	}
}

// The speed tab: pace, its peak and its halves, and what the hands did.
void draw_analysis_speed (const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const auto& places = game.placements;
	const int count = static_cast<int>(places.size());
	double peak = 0.;
	for (int i = 0; i + 9 < count; ++i) {
		const double start = i > 0 ? places[i - 1].elapsed : 0.;
		const double span = places[i + 9].elapsed - start;
		if (span > 0.) {
			peak = std::max(peak, 10. / span);
		}
	}
	const int half = count / 2;
	double first = 0.;
	double second = 0.;
	if (half > 0 && places[half - 1].elapsed > 0.) {
		first = half / places[half - 1].elapsed;
	}
	if (count > half && places[count - 1].elapsed > places[half - 1].elapsed) {
		second = (count - half)
			/ (places[count - 1].elapsed - places[half - 1].elapsed);
	}
	int holds = 0;
	int forced = 0;
	for (const replay::Placement& place : places) {
		holds += place.held ? 1 : 0;
		forced += place.forced ? 1 : 0;
	}
	if (ImGui::BeginTable("speed", 2)) {
		detail_row("PPS", detail_text("%.2f", s.pps));
		detail_row("Peak PPS (10 pieces)", detail_text("%.2f", peak));
		detail_row("First half", detail_text("%.2f PPS", first));
		detail_row("Second half", detail_text("%.2f PPS", second));
		detail_row("Key presses", detail_text("%d", s.presses));
		detail_row("KPP (keys/piece)", detail_text("%.2f", s.ppp));
		detail_row("KPS (keys/second)", detail_text("%.2f",
			s.seconds > 0. ? s.presses / s.seconds : 0.));
		detail_row("Holds", detail_text("%d", holds));
		detail_row("Forced drops", detail_text("%d", forced));
		ImGui::EndTable();
	}
}

// The pieces tab: what was dealt, what it cleared, and how it spun.
void draw_analysis_pieces (const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const auto& places = game.placements;
	const double count = std::max<size_t>(1, places.size());
	int forms[7] = {};
	int spins = 0;
	int minis = 0;
	int max_combo = 0;
	int max_b2b = 0;
	for (const replay::Placement& place : places) {
		if (place.form >= 0 && place.form <= 6) {
			++forms[place.form];
		}
		if (!place.spin.empty()) {
			++spins;
			if (place.spin.rfind("MINI", 0) == 0) {
				++minis;
			}
		}
		max_combo = std::max(max_combo, place.combo);
		max_b2b = std::max(max_b2b, place.b2b);
	}
	if (ImGui::BeginTable("pieces", 2)) {
		static const char* names[] = {"I", "O", "T", "S", "Z", "J", "L"};
		for (int form = 0; form < 7; ++form) {
			detail_row(names[form], detail_text("%d  (%.0f%%)",
				forms[form], forms[form] * 100. / count));
		}
		static const char* sizes[] = {"Singles", "Doubles", "Triples", "Quads"};
		for (int size = 1; size <= 4; ++size) {
			const auto found = s.clears.find(size);
			detail_row(sizes[size - 1], detail_text("%d",
				found == s.clears.end() ? 0 : found->second));
		}
		detail_row("Spins", detail_text("%d  (%d mini)", spins, minis));
		detail_row("Perfect clears", detail_text("%d", s.perfects));
		detail_row("Best combo", detail_text("%d", max_combo));
		detail_row("Best B2B", detail_text("%d", max_b2b));
		ImGui::EndTable();
	}
}

// The rating tab: where the run's figures sit against the public shape of
// Tetra League. An estimate over community averages, and it says so.
void draw_analysis_rating (App& app, const replay::Replay& game) {
	const replay::Summary s = game.summary(false);
	const rating::Estimate guess = rating::estimate(s.apm, s.pps, s.vs);
	if (std::string(guess.rank).empty()) {
		ImGui::TextDisabled("Not enough of a game to place.");
		return;
	}
	ImGui::Spacing();
	ImGui::PushFont(app.fonts.title);
	ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f), "%s", guess.rank);
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::PushFont(app.fonts.head);
	ImGui::Text("  %.0f TR", guess.tr);
	ImGui::PopFont();
	ImGui::Spacing();
	if (ImGui::BeginTable("rating", 2)) {
		detail_row("Estimated Glicko", detail_text("%.0f", guess.glicko));
		detail_row("Estimated TR", detail_text("%.0f / 25000", guess.tr));
		detail_row("From APM", detail_text("%.1f", s.apm));
		detail_row("From PPS", detail_text("%.2f", s.pps));
		detail_row("From VS", detail_text("%.1f", s.vs));
		ImGui::EndTable();
	}
	ImGui::Spacing();
	ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui(430));
	ImGui::TextDisabled(
		"An estimate, not a rating: your figures placed against public "
		"Tetra League averages per rank. Only real league games rate.");
	ImGui::PopTextWrapPos();
}

// The analysis of a saved replay, opened from the browser or the loss
// screen: the graded overview first, the deeper cuts on their own tabs.
// The munch tab: minomuncher's statistics for this one game, grouped the
// way that tool groups them, with its two style verdicts spelled out.
void draw_analysis_munch (const replay::Replay& game) {
	const munch::Stats stats = munch::crunch(game);
	if (stats.groups.empty()) {
		ImGui::TextDisabled("Nothing to chew on.");
		return;
	}
	// The style verdicts, by the muncher's own twenty-point zones.
	const auto zone = [] (double value, const char* const names[5]) {
		return names[std::clamp(static_cast<int>(value / 20.), 0, 4)];
	};
	static const char* kStackStyles[5]
		= {"upstacker", "aggressive", "medium", "defensive", "downstacker"};
	static const char* kCheeseStyles[5]
		= {"lean", "clean", "medium", "cheesy", "greasy"};
	const double ratio = stats.get("ds_ratio");
	const double cheese = stats.get("cheesiness");
	ImGui::Text("Stacking style: %s (%.0f)", zone(ratio, kStackStyles),
		ratio);
	ImGui::Text("Attack style: %s (%.0f)", zone(cheese, kCheeseStyles),
		cheese);
	ImGui::TextDisabled(
		"Cheesiness is scored over raw attack - a trainer has no wire.");
	for (const munch::Group& group : stats.groups) {
		ImGui::Separator();
		ImGui::TextDisabled("%s", group.name);
		if (ImGui::BeginTable(group.name, 2,
			ImGuiTableFlags_SizingStretchProp)) {
			for (const munch::Stat& stat : group.stats) {
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(stat.label);
				ImGui::TableSetColumnIndex(1);
				if (stat.value == std::floor(stat.value)
					&& std::fabs(stat.value) < 1e6) {
					ImGui::Text("%.0f", stat.value);
				} else {
					ImGui::Text("%.2f", stat.value);
				}
			}
			ImGui::EndTable();
		}
	}
}

void draw_analysis (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(6)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(490), 0));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(6)));
	ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui(8), ui(3)));
	ImGui::Begin("Analysis", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (app.studying.has_value()) {
		ImGui::TextDisabled("%s", app.studying->title().c_str());
		if (ImGui::BeginTabBar("analysis_tabs")) {
			if (ImGui::BeginTabItem("Overview")) {
				draw_analysis_rows(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Attack")) {
				draw_analysis_attack(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Speed")) {
				draw_analysis_speed(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Pieces")) {
				draw_analysis_pieces(*app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Rating")) {
				draw_analysis_rating(app, *app.studying);
				ImGui::EndTabItem();
			}
			if (ImGui::BeginTabItem("Munch")) {
				draw_analysis_munch(*app.studying);
				ImGui::EndTabItem();
			}
			ImGui::EndTabBar();
		}
		ImGui::Separator();
		if (ImGui::Button("Watch replay", ImVec2(ui(240), 0))) {
			watch(app, *app.studying, Screen::Analysis);
		}
	} else {
		ImGui::TextDisabled("That run was too short to record.");
	}
	if (ImGui::Button("Back", ImVec2(ui(240), 0))) {
		app.screen = app.study_back;
		app.studying.reset();
	}
	ImGui::End();
	ImGui::PopStyleVar(2);
}

// How to play: the keys as they are bound right now, and the one rule no
// other Tetris has. The Python help screen's text, read off the live
// bindings the same way.
void draw_help (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(50)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(620), 0));
	ImGui::Begin("How to Play", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::BeginTable("keys", 2)) {
		for (const ActionDef& action : all_actions()) {
			// Every key bound to the action, or the word for none - the same
			// line controls.describe() builds.
			std::string bound;
			const auto found = app.config.keys.find(action.id);
			if (found != app.config.keys.end()) {
				for (const int code : found->second) {
					const char* name =
						SDL_GetScancodeName(static_cast<SDL_Scancode>(code));
					bound += (bound.empty() ? "" : ", ")
						+ std::string(name != nullptr && *name != '\0' ? name : "?");
				}
			}
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImVec4(1.f, 0.88f, 0.5f, 1.f), "%s",
				bound.empty() ? "Unbound" : bound.c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(action.label);
		}
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextColored(ImVec4(1.f, 0.88f, 0.5f, 1.f), "Escape");
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("Pause");
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextColored(ImVec4(1.f, 0.88f, 0.5f, 1.f), "R");
		ImGui::TableSetColumnIndex(1);
		ImGui::TextUnformatted("Restart the run");
		ImGui::EndTable();
	}
	ImGui::Separator();
	if (app.config.fuse) {
		ImGui::TextUnformatted("The Fuse");
		ImGui::TextUnformatted(
			"Every piece burns. When its fuse runs out it is slammed down\n"
			"where it stands - and the fuse gets shorter as the levels climb.");
		ImGui::TextUnformatted(
			"Clears refuel the pieces to come; spins, quads and perfect\n"
			"clears refuel hardest. Holding does not stop the burn.");
		ImGui::TextUnformatted("");
		ImGui::TextUnformatted("Flow and Overdrive");
		ImGui::TextUnformatted(
			"Lock with fuse to spare and the Flow rail climbs - lock in the\n"
			"first moments, the Flash window, and it climbs hardest. Burn a\n"
			"fuse to the end and it drains.");
		ImGui::TextUnformatted(
			"A full rail ignites Overdrive: the fuse freezes and everything\n"
			"you send is multiplied, until it gutters out.");
		ImGui::TextDisabled(
			"The plain trainer rules live under Settings, Rules, Fuse.");
	} else {
		ImGui::TextUnformatted("Forced Drop");
		if (app.config.forced_delay > 0.) {
			ImGui::Text("Every piece is hard dropped for you %.2fs after it spawns.",
				app.config.forced_delay);
			ImGui::Text("Holding gives the incoming piece a fresh %.2fs, once per piece.",
				app.config.forced_delay);
			ImGui::TextUnformatted("Soft dropping and wall kicks do not stop the clock.");
			ImGui::TextUnformatted("Change the time under Settings.");
		} else {
			ImGui::TextUnformatted("Currently off, so this is plain Tetris.");
			ImGui::TextUnformatted("Turn it on under Settings to train placement speed.");
		}
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(240), 0))) {
		app.screen = app.help_back;
	}
	ImGui::End();
}

// --- The replay viewer: a re-enactment, never a re-simulation. ------------

void advance_viewer (App& app) {
	// One 20ms tick of watching: a stop every 0.16 seconds at single speed.
	Viewing& show = *app.viewing;
	if (!show.playing || show.game.placements.empty()) {
		return;
	}
	show.carry += 0.02 * show.speed;
	while (show.carry >= 0.16) {
		show.carry -= 0.16;
		const auto stops = show.game.placements[show.index].steps(show.fixed);
		if (show.step + 1 < static_cast<int>(stops.size())) {
			++show.step;
		} else if (show.index + 1 < static_cast<int>(show.game.placements.size())) {
			++show.index;
			show.step = 0;
		} else {
			show.playing = false;
		}
	}
}

void draw_row_strings (App& app, const std::vector<std::string>& rows,
                       int left = kBoardX, int top = kBoardY, int size = kCell) {
	SDL_Renderer* renderer = app.renderer;
	const int w = kWidth * size;
	const int h = kHeight * size;
	fill(renderer, left - px(3), top - px(3), w + px(6), h + px(6), {32, 40, 53, 255});
	fill(renderer, left, top, w, h, {14, 18, 24, 255});
	for (size_t y = 0; y < rows.size() && y < kHeight; ++y) {
		for (size_t x = 0; x < rows[y].size() && x < kWidth; ++x) {
			const char cell = rows[y][x];
			if (cell >= '0' && cell <= '7') {
				draw_cell(renderer, left + static_cast<int>(x) * size,
					top + static_cast<int>(y) * size, kFormColors[cell - '0'],
					size);
			}
		}
	}
}

// The player's clock at the current frame of the re-enactment: a placement
// spans from the previous lock time to its own, walked linearly across its
// stops. At the last stop the clock is exactly the lock time, so the fixed
// (finesse) view - fewer stops, same endpoints - stays in sync.
double viewer_clock (const Viewing& show, size_t stop_count) {
	const auto& places = show.game.placements;
	const double at = places[show.index].elapsed;
	const double from = show.index > 0
		? std::min(places[show.index - 1].elapsed, at) : 0.;
	const double frac = stop_count <= 1 ? 1.
		: static_cast<double>(show.step)
			/ static_cast<double>(stop_count - 1);
	return from + (at - from) * frac;
}

void draw_viewer (App& app) {
	Viewing& show = *app.viewing;
	if (show.game.placements.empty()) {
		return;
	}
	show.index = std::clamp(
		show.index, 0, static_cast<int>(show.game.placements.size()) - 1);
	const replay::Placement& place = show.game.placements[show.index];
	const auto stops = place.steps(show.fixed);
	show.step = std::clamp(show.step, 0, static_cast<int>(stops.size()) - 1);

	// The board the placement was made onto, with the piece walking over it.
	draw_row_strings(app, replay::padded(show.game.before(show.index)));
	const auto& stop = stops[show.step];
	const Piece piece{place.form, stop[0], stop[1], stop[2]};
	if (place.form >= 0 && place.form <= 6) {
		for (const Offset cell : cells_of(piece)) {
			if (cell.y >= 0 && cell.y < kHeight) {
				draw_cell(app.renderer, kBoardX + cell.x * kCell,
					kBoardY + cell.y * kCell, kFormColors[place.form]);
			}
		}
	}

	// The other board, when the file carries one: the bot's own snapshots,
	// shown as they stood at this moment of the player's walk. Stateless -
	// a scan per frame - so scrubbing backwards is just as correct.
	if (show.game.opponent.has_value()) {
		const double clock = viewer_clock(show, stops.size());
		const replay::Placement* seen = nullptr;
		for (const replay::Placement& theirs : show.game.opponent->placements) {
			if (theirs.elapsed <= clock) {
				seen = &theirs;
			}
		}
		// Below the info window, which owns the top of this margin (except
		// on a phone, where the layout says where the mini board fits).
		const int mini_left = kMiniX;
		const int mini_top = kMobile ? kMiniY : kBoardY + px(340);
		draw_row_strings(app,
			replay::padded(seen != nullptr ? seen->rows
				: std::vector<std::string>{}),
			mini_left, mini_top, kMiniCell);
		draw_label("BOT", static_cast<float>(mini_left),
			static_cast<float>(mini_top - ui(22)));
	}

	// What the player could see at the time: the hold box and the previews.
	fill(app.renderer, kBoardX - px(122), kBoardY, px(104), px(86), {20, 26, 34, 255});
	draw_preview(app.renderer, place.stored,
		kBoardX - px(122) + px(16), kBoardY + px(12), px(18));
	for (size_t slot = 0; slot < place.queue.size() && slot < 3; ++slot) {
		fill(app.renderer, kBoardX + kBoardW + px(18),
			kBoardY + static_cast<int>(slot) * px(92), px(104), px(86),
			{20, 26, 34, 255});
		draw_preview(app.renderer, place.queue[slot],
			kBoardX + kBoardW + px(18) + px(16),
			kBoardY + static_cast<int>(slot) * px(92) + px(12), px(18));
	}

	// The placement, in words.
	ImGui::SetNextWindowPos(ImVec2(kBoardX + kBoardW + ui(140), kBoardY));
	ImGui::Begin("viewer info", nullptr, ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("Placement %d of %d", show.index + 1,
		static_cast<int>(show.game.placements.size()));
	std::string presses;
	for (const std::string& press : place.presses_shown(show.fixed)) {
		presses += (presses.empty() ? "" : " ") + press;
	}
	ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f), "Presses: %s",
		presses.empty() ? "none" : presses.c_str());
	if (place.judged && place.best.has_value()) {
		ImGui::TextColored(ImVec4(0.59f, 0.65f, 0.73f, 1.f),
			"Best: %d press%s%s", *place.best, *place.best == 1 ? "" : "es",
			place.wasted() > 0 && !show.fixed ? "  (wasted)" : "");
	}
	if (!place.spin.empty()) {
		ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f), "%s", place.spin.c_str());
	}
	if (place.lines > 0) {
		ImGui::Text("Cleared %d", place.lines);
	}
	if (place.perfect) {
		ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f), "PERFECT CLEAR");
	}
	if (place.attack > 0) {
		ImGui::Text("Attack +%d", place.attack);
	}
	ImGui::Text("Score %lld", place.score);
	ImGui::Text("At %.2fs", place.elapsed);
	ImGui::End();

	// The controls, mouse-first.
	ImGui::SetNextWindowPos(ImVec2(kBoardX - ui(3), kBoardY + kBoardH + ui(14)));
	ImGui::Begin("viewer controls", nullptr, ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings);
	if (ImGui::Button(show.playing ? "Pause" : "Play")) {
		show.playing = !show.playing;
		if (show.playing && show.index + 1 == static_cast<int>(show.game.placements.size())
			&& show.step + 1 == static_cast<int>(stops.size())) {
			show.index = 0;
			show.step = 0;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("|<")) {
		show.index = std::max(0, show.index - 1);
		show.step = 0;
	}
	ImGui::SameLine();
	if (ImGui::Button(">|")) {
		show.index = std::min(
			static_cast<int>(show.game.placements.size()) - 1, show.index + 1);
		show.step = 0;
	}
	ImGui::SameLine();
	const char* speeds[] = {"1x", "2x", "4x"};
	int gear = show.speed == 4 ? 2 : show.speed - 1;
	ImGui::SetNextItemWidth(ui(80));
	if (ImGui::Combo("##speed", &gear, speeds, 3)) {
		show.speed = gear == 2 ? 4 : gear + 1;
	}
	ImGui::SameLine();
	if (ImGui::Checkbox("Perfect finesse", &show.fixed)) {
		// The stops change under the piece, so start this placement over.
		show.step = 0;
	}
	ImGui::SameLine();
	if (ImGui::Button("Back")) {
		app.screen = show.back;
		app.viewing.reset();
	}
	ImGui::End();
}

void draw_replays (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(60)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(580), 0));
	ImGui::Begin("Replays", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	if (app.shelf.empty()) {
		ImGui::TextDisabled("No replays yet. Finish a game first.");
	}
	for (size_t i = 0; i < app.shelf.size(); ++i) {
		ImGui::PushID(static_cast<int>(i));
		ImGui::TextUnformatted(app.shelf[i].title().c_str());
		ImGui::SameLine(ui(380));
		if (ImGui::SmallButton("Analysis")) {
			app.studying = app.shelf[i];
			app.study_back = Screen::Replays;
			app.screen = Screen::Analysis;
		}
		ImGui::SameLine(ui(470));
		if (ImGui::SmallButton("Watch")) {
			watch(app, app.shelf[i], Screen::Replays);
		}
		ImGui::PopID();
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(140), 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
}

void draw_scores (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(50)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	ImGui::SetNextWindowSize(ImVec2(ui(660), 0));
	ImGui::Begin("High scores", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	// The variant's six tables first, the trainer's three behind them -
	// different files, different games, one screen.
	static const char* kPages[] = {"Ignition", "Blaze", "Inferno", "Meltdown",
		"Bunker", "Duel", "Arcade", "Timed", "Free"};
	for (int page = 0; page < 9; ++page) {
		if (page > 0 && page != 6) {
			ImGui::SameLine();
		}
		if (ImGui::RadioButton(kPages[page], app.score_page == page)) {
			app.score_page = page;
		}
	}
	ImGui::Separator();
	const hiscore::Tables plain = hiscore::load(hiscore::folder(app.root));
	const hiscore::FuseTables fuse
		= hiscore::load_fuse(hiscore::folder(app.root));
	const hiscore::Table& table = app.score_page < 6
		? fuse[app.score_page] : plain[app.score_page - 6];
	if (ImGui::BeginTable("scores", 4)) {
		ImGui::TableSetupColumn("Name");
		ImGui::TableSetupColumn("Score");
		ImGui::TableSetupColumn("Lines");
		ImGui::TableSetupColumn("Time taken");
		ImGui::TableHeadersRow();
		for (const hiscore::Entry& entry : table) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextUnformatted(hiscore::shown_name(entry).c_str());
			ImGui::TableSetColumnIndex(1);
			ImGui::Text("%llu", static_cast<unsigned long long>(entry.score));
			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%u", entry.lines);
			ImGui::TableSetColumnIndex(3);
			ImGui::TextUnformatted(hiscore::shown_timer(entry.timer).c_str());
		}
		ImGui::EndTable();
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(140), 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
}

// A line chart on the window's own draw list: the raw series faint, a
// ten-game moving average bright over it, the range labelled at the ends.
void draw_chart (const char* label, const std::vector<double>& values,
                 float height) {
	ImGui::PushFont(ImGui::GetFont());
	ImGui::TextUnformatted(label);
	ImGui::PopFont();
	if (values.size() < 2) {
		ImGui::TextDisabled("Not enough games yet.");
		ImGui::Dummy(ImVec2(0.f, ui(8)));
		return;
	}
	const float width = ImGui::GetContentRegionAvail().x - ui(4);
	const ImVec2 origin = ImGui::GetCursorScreenPos();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	double lo = values[0];
	double hi = values[0];
	for (const double value : values) {
		lo = std::min(lo, value);
		hi = std::max(hi, value);
	}
	if (hi - lo < 1e-9) {
		hi = lo + 1.;
	}
	const double pad = (hi - lo) * 0.08;
	lo -= pad;
	hi += pad;
	draw->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
		IM_COL32(20, 26, 34, 255), ui(6));
	for (int line = 1; line < 4; ++line) {
		const float y = origin.y + height * line / 4.f;
		draw->AddLine(ImVec2(origin.x, y), ImVec2(origin.x + width, y),
			IM_COL32(40, 50, 64, 120));
	}
	const auto at = [&] (size_t i, double value) {
		return ImVec2(
			origin.x + width * (values.size() > 1
				? static_cast<float>(i) / (values.size() - 1) : 0.f),
			origin.y + height
				- height * static_cast<float>((value - lo) / (hi - lo)));
	};
	for (size_t i = 1; i < values.size(); ++i) {
		draw->AddLine(at(i - 1, values[i - 1]), at(i, values[i]),
			IM_COL32(65, 198, 224, 90), std::max(1.f, ui(1)));
	}
	// The moving average, the line the eye should follow.
	double rolling = 0.;
	std::vector<double> window;
	ImVec2 last{};
	bool started = false;
	for (size_t i = 0; i < values.size(); ++i) {
		window.push_back(values[i]);
		rolling += values[i];
		if (window.size() > 10) {
			rolling -= window.front();
			window.erase(window.begin());
		}
		const ImVec2 point = at(i, rolling / window.size());
		if (started) {
			draw->AddLine(last, point, IM_COL32(65, 198, 224, 255),
				std::max(1.f, ui(2)));
		}
		last = point;
		started = true;
	}
	char text[48];
	std::snprintf(text, sizeof text, "%.1f", hi - pad);
	draw->AddText(ImVec2(origin.x + ui(6), origin.y + ui(2)),
		IM_COL32(150, 165, 185, 200), text);
	std::snprintf(text, sizeof text, "%.1f", lo + pad);
	draw->AddText(ImVec2(origin.x + ui(6), origin.y + height - ui(18)),
		IM_COL32(150, 165, 185, 200), text);
	std::snprintf(text, sizeof text, "%.1f",
		rolling / std::max<size_t>(1, window.size()));
	draw->AddText(ImVec2(origin.x + width - ui(56), origin.y + ui(2)),
		IM_COL32(65, 198, 224, 255), text);
	ImGui::Dummy(ImVec2(width, height + ui(10)));
}

void draw_profile (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(24)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	const float wide
		= std::min(ui(680), ImGui::GetIO().DisplaySize.x - ui(16));
	// Tall content - the growth charts - scrolls instead of running off
	// the screen's bottom.
	ImGui::SetNextWindowSizeConstraints(ImVec2(wide, 0),
		ImVec2(wide, ImGui::GetIO().DisplaySize.y - ui(48)));
	ImGui::Begin("Profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);

	// The mode filter every tab reads.
	// Each filter matches its mode family under either ruleset's key - the
	// variant name and the trainer name are the same family of game.
	static const char* kFilters[] = {"All", "Ignition", "Blaze", "Inferno",
		"Meltdown", "Bunker", "Duel"};
	static const char* kOld[] = {"", "free", "timed", "arcade", "cheese_race",
		"cheese_survival", "versus"};
	static const char* kNew[] = {"", "ignition", "blaze", "inferno",
		"meltdown", "bunker", "duel"};
	static int filter = 0;
	ImGui::SetNextItemWidth(ui(160));
	ImGui::Combo("Mode", &filter, kFilters, 7);
	std::vector<const profile::GameRecord*> games;
	for (const profile::GameRecord& record : app.history) {
		if (filter == 0 || record.gametype == kOld[filter]
			|| record.gametype == kNew[filter]) {
			games.push_back(&record);
		}
	}
	ImGui::SameLine();
	ImGui::TextDisabled("%d game%s", static_cast<int>(games.size()),
		games.size() == 1 ? "" : "s");
	ImGui::Separator();

	if (games.empty()) {
		ImGui::TextUnformatted(
			"No games on record yet. Finish one and come back.");
	} else if (ImGui::BeginTabBar("profile", ImGuiTabBarFlags_None)) {
		// FORCETRIS_PROFILE_TAB picks the tab once - how the gallery
		// screenshots the charts without a mouse.
		static int force = [] {
			const char* forced = std::getenv("FORCETRIS_PROFILE_TAB");
			return forced != nullptr ? std::atoi(forced) : -1;
		}();
		const auto tab_flags = [&] (int index) {
			return force == index
				? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
		};
		const auto series = [&] (auto pick) {
			std::vector<double> values;
			for (const profile::GameRecord* game : games) {
				const double value = pick(*game);
				if (value >= 0.) {
					values.push_back(value);
				}
			}
			return values;
		};
		if (ImGui::BeginTabItem("Overview", nullptr, tab_flags(0))) {
			double seconds = 0.;
			long long pieces = 0;
			long long lines = 0;
			long long attack = 0;
			int wins = 0;
			int losses = 0;
			profile::GameRecord best;
			for (const profile::GameRecord* game : games) {
				seconds += game->seconds;
				pieces += game->pieces;
				lines += game->lines;
				attack += game->attack;
				wins += game->won == 1 ? 1 : 0;
				losses += game->won == 0 ? 1 : 0;
				best.pps = std::max(best.pps, game->pps);
				best.apm = std::max(best.apm, game->apm);
				best.vs = std::max(best.vs, game->vs);
				best.score = std::max(best.score, game->score);
				best.tr = std::max(best.tr, game->tr);
			}
			ImGui::PushFont(app.fonts.head);
			ImGui::Text("%d games,  %.1f hours,  %lld pieces",
				static_cast<int>(games.size()), seconds / 3600.,
				pieces);
			ImGui::PopFont();
			ImGui::Text("%lld lines cleared,  %lld attack sent",
				lines, attack);
			if (wins + losses > 0) {
				ImGui::Text("Versus record %d - %d  (%.0f%%)", wins, losses,
					100. * wins / (wins + losses));
			}
			ImGui::Separator();
			ImGui::TextDisabled("Bests");
			ImGui::Text("PPS %.2f    APM %.1f    VS %.1f", best.pps,
				best.apm, best.vs);
			ImGui::Text("Score %lld", best.score);
			if (best.tr >= 0.) {
				ImGui::Text("Estimated TR %.0f  (%s)", best.tr,
					rating::rank_for(best.tr));
			}
			// The last ten against everything before them: the short answer
			// to "am I getting better".
			if (games.size() >= 20) {
				double old_pps = 0.;
				double new_pps = 0.;
				double old_apm = 0.;
				double new_apm = 0.;
				const size_t cut = games.size() - 10;
				for (size_t i = 0; i < games.size(); ++i) {
					(i < cut ? old_pps : new_pps) += games[i]->pps;
					(i < cut ? old_apm : new_apm) += games[i]->apm;
				}
				old_pps /= cut;
				old_apm /= cut;
				new_pps /= 10.;
				new_apm /= 10.;
				ImGui::Separator();
				ImGui::TextDisabled("Last ten games, against the rest");
				ImGui::Text("PPS %+.2f    APM %+.1f",
					new_pps - old_pps, new_apm - old_apm);
			}
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Growth", nullptr, tab_flags(1))) {
			draw_chart("PPS", series([] (const profile::GameRecord& g) {
				return g.pps; }), ui(96));
			draw_chart("APM", series([] (const profile::GameRecord& g) {
				return g.apm; }), ui(96));
			draw_chart("VS", series([] (const profile::GameRecord& g) {
				return g.vs; }), ui(96));
			draw_chart("Estimated TR", series(
				[] (const profile::GameRecord& g) { return g.tr; }), ui(96));
			draw_chart("Finesse %", series(
				[] (const profile::GameRecord& g) { return g.finesse; }),
				ui(96));
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Style", nullptr, tab_flags(2))) {
			// The munch numbers, averaged over the filtered games - the
			// per-game versions live on the analysis screen's Munch tab.
			std::map<std::string, std::pair<double, int>> sums;
			for (const profile::GameRecord* game : games) {
				for (const auto& [key, value] : game->stats) {
					sums[key].first += value;
					sums[key].second += 1;
				}
			}
			if (sums.empty()) {
				ImGui::TextUnformatted(
					"No munch numbers yet - they are written from the next "
					"finished game on.");
			} else if (ImGui::BeginTable("style", 2)) {
				for (const char* id : munch::order()) {
					const auto found = sums.find(id);
					if (found == sums.end()) {
						continue;
					}
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(munch::label(id));
					ImGui::TableSetColumnIndex(1);
					ImGui::Text("%.2f",
						found->second.first / found->second.second);
				}
				ImGui::EndTable();
			}
			ImGui::EndTabItem();
		}
		force = -1;
		ImGui::EndTabBar();
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(140), 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
}

// The date the daily runs under, local time, the way the profile stamps.
std::string today () {
	char stamp[16] = "";
	const std::time_t now = std::time(nullptr);
	if (std::tm* local = std::localtime(&now)) {
		std::strftime(stamp, sizeof stamp, "%Y-%m-%d", local);
	}
	return stamp;
}

// Start today's one run: the latch is burned before the first piece falls,
// so walking out costs the attempt - that is what makes it a daily.
void start_daily (App& app) {
	app.career.daily_date = today();
	app.career.daily_score = -1;
	career::save(career::path(app.root), app.career);
	unsigned seed = 2166136261u;
	for (const char letter : app.career.daily_date) {
		seed = (seed ^ static_cast<unsigned char>(letter)) * 16777619u;
	}
	start_game(app, 0, seed);
	app.daily_run = true;
}

void draw_career (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(24)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	const float wide
		= std::min(ui(420), ImGui::GetIO().DisplaySize.x - ui(16));
	ImGui::SetNextWindowSizeConstraints(ImVec2(wide, 0),
		ImVec2(wide, ImGui::GetIO().DisplaySize.y - ui(48)));
	ImGui::Begin("Career", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::PushFont(app.fonts.head);
	ImGui::TextUnformatted("The Ladder");
	ImGui::PopFont();
	ImGui::TextDisabled("Fight the ranks in order. A win opens the next;");
	ImGui::TextDisabled("a sweep pays two stars, a sweep with Overdrive three.");
	ImGui::Dummy(ImVec2(0.f, ui(4)));
	const auto& ladder = bot::ranks();
	std::vector<std::string> names;
	for (const auto& rank : ladder) {
		names.push_back(rank.name);
	}
	if (ImGui::BeginTable("ladder", 3)) {
		for (size_t stage = 0; stage < ladder.size(); ++stage) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("Stage %d  -  %s", static_cast<int>(stage) + 1,
				ladder[stage].name);
			ImGui::TableSetColumnIndex(1);
			const auto found = app.career.stars.find(names[stage]);
			const int stars
				= found != app.career.stars.end() ? found->second : 0;
			char marks[8] = "- - -";
			for (int i = 0; i < stars && i < 3; ++i) {
				marks[i * 2] = '*';
			}
			ImGui::TextColored(stars > 0
				? ImVec4(1.f, 0.84f, 0.38f, 1.f)
				: ImVec4(0.45f, 0.5f, 0.58f, 1.f), "%s", marks);
			ImGui::TableSetColumnIndex(2);
			if (career::open(app.career, names, stage)) {
				ImGui::PushID(static_cast<int>(stage));
				if (ImGui::Button(stars > 0 ? "Again" : "Fight",
					ImVec2(ui(70), 0))) {
					start_versus(app, static_cast<int>(stage));
				}
				ImGui::PopID();
			} else {
				ImGui::TextDisabled("Locked");
			}
		}
		ImGui::EndTable();
	}
	ImGui::Separator();
	ImGui::PushFont(app.fonts.head);
	ImGui::TextUnformatted("The Daily");
	ImGui::PopFont();
	ImGui::TextDisabled("One Ignition run a day, same fuse for everyone");
	ImGui::TextDisabled("who shares the date. Leaving still spends it.");
	ImGui::Dummy(ImVec2(0.f, ui(2)));
	if (app.career.daily_date == today()) {
		if (app.career.daily_score >= 0) {
			ImGui::Text("Today's run: %lld", app.career.daily_score);
		} else {
			ImGui::TextUnformatted("Today's run is spent.");
		}
	} else if (ImGui::Button("Run today's fuse", ImVec2(ui(200), 0))) {
		start_daily(app);
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(140), 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();
}

// One option in a picker row: drawn selected in the accent, and returns
// true when clicked.
bool option_button (const char* label, bool selected, float width) {
	if (selected) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.42f, 0.49f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.50f, 0.58f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.24f, 0.58f, 0.67f, 1.f));
	}
	const bool clicked = ImGui::Button(label, ImVec2(width, 0));
	if (selected) {
		ImGui::PopStyleColor(3);
	}
	return clicked;
}

void draw_menus (App& app) {
	const ImVec2 middle(ImGui::GetIO().DisplaySize.x / 2,
		ImGui::GetIO().DisplaySize.y / 2);
	const ImGuiWindowFlags box = ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings;

	if (app.screen == Screen::Menu) {
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("main menu", nullptr, box);
		ImGui::PushFont(app.fonts.title);
		ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f), "FORCETRIS");
		ImGui::PopFont();
		ImGui::TextDisabled("the forced hard drop trainer");
		ImGui::Dummy(ImVec2(0.f, ui(10)));
		if (ImGui::Button("Play", ImVec2(ui(260), ui(44)))) {
			app.screen = Screen::Modes;
			app.mode_popup = 0;
		}
		ImGui::Dummy(ImVec2(0.f, ui(2)));
		if (ImGui::Button("Career", ImVec2(ui(260), ui(44)))) {
			app.career = career::load(career::path(app.root));
			app.screen = Screen::Career;
		}
		ImGui::Dummy(ImVec2(0.f, ui(6)));
		if (ImGui::Button("How to play", ImVec2(ui(260), 0))) {
			app.help_back = Screen::Menu;
			app.screen = Screen::Help;
		}
		if (ImGui::Button("High scores", ImVec2(ui(260), 0))) {
			app.score_page = 0;
			app.screen = Screen::Scores;
		}
		if (ImGui::Button("Replays", ImVec2(ui(260), 0))) {
			app.shelf = replay::listing(replay::folder(app.root));
			app.screen = Screen::Replays;
		}
		if (ImGui::Button("Profile", ImVec2(ui(260), 0))) {
			app.history = profile::load(profile::path(app.root));
			app.screen = Screen::Profile;
		}
		if (ImGui::Button("Settings", ImVec2(ui(260), 0))) {
			app.show_settings = true;
		}
		ImGui::Dummy(ImVec2(0.f, ui(6)));
		if (ImGui::Button("Quit", ImVec2(ui(260), 0))) {
			app.quit = true;
		}
		ImGui::End();
	} else if (app.screen == Screen::Modes) {
		ImGui::SetNextWindowPos(ImVec2(middle.x, ui(8)),
			ImGuiCond_Always, ImVec2(0.5f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(7)));
		if (app.mode_popup == 1) {
			// The cheese family's own window: both modes and how the cheese
			// is cut, out of the picker's way.
			ImGui::Begin("cheese setup", nullptr, box);
			ImGui::PushFont(app.fonts.head);
			ImGui::TextUnformatted("Meltdown / Bunker");
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Meltdown");
			static const struct { const char* label; int rows; } kRace[] = {
				{"10", 10}, {"18", 18}, {"100", 100},
			};
			for (const auto& race : kRace) {
				ImGui::SameLine();
				if (ImGui::Button(race.label, ImVec2(ui(52), 0))) {
					app.config.cheese_total = race.rows;
					start_game(app, 3);
				}
			}
			ImGui::TextDisabled("Dig that many rows; the clock stops at the last.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Bunker");
			static const struct { const char* label; int period; } kRise[] = {
				{"8s", 400}, {"5s", 250}, {"3s", 150},
			};
			for (const auto& rise : kRise) {
				ImGui::SameLine();
				if (ImGui::Button(rise.label, ImVec2(ui(52), 0))) {
					app.config.cheese_period = rise.period;
					start_game(app, 4);
				}
			}
			ImGui::TextDisabled("The floor rises on that clock. Outlast it.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			ImGui::Separator();
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Holes per row");
			for (int holes = 1; holes <= 3; ++holes) {
				ImGui::SameLine();
				char label[4];
				std::snprintf(label, sizeof label, "%d", holes);
				if (option_button(label, app.config.cheese_holes == holes, ui(44))) {
					app.config.cheese_holes = holes;
				}
			}
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Messiness");
			static const struct { const char* label; int percent; } kMess[] = {
				{"Clean", 0}, {"Low", 33}, {"High", 66}, {"Full", 100},
			};
			for (const auto& mess : kMess) {
				ImGui::SameLine();
				if (option_button(mess.label,
					app.config.cheese_messiness == mess.percent, ui(62))) {
					app.config.cheese_messiness = mess.percent;
				}
			}
			ImGui::TextDisabled("How the cheese is cut: holes per row, and how");
			ImGui::TextDisabled("often they move between rows.");
			ImGui::Dummy(ImVec2(0.f, ui(6)));
			if (ImGui::Button("Back", ImVec2(ui(280), 0))) {
				app.mode_popup = 0;
			}
			ImGui::End();
		} else if (app.mode_popup == 2) {
			// The bot fight's window: rank, match length, go.
			ImGui::Begin("versus setup", nullptr, box);
			ImGui::PushFont(app.fonts.head);
			ImGui::TextUnformatted("Duel");
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("Rank");
			const auto& ladder = bot::ranks();
			for (size_t i = 0; i < ladder.size(); ++i) {
				ImGui::SameLine();
				if (option_button(ladder[i].name,
					app.config.bot_rank == static_cast<int>(i), ui(34))) {
					app.config.bot_rank = static_cast<int>(i);
				}
			}
			ImGui::TextDisabled("A bot paced at that rank's league-average PPS,");
			ImGui::TextDisabled("garbage, cancelling and surge included.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			ImGui::AlignTextToFramePadding();
			ImGui::TextUnformatted("First to");
			for (int ft = 1; ft <= 3; ++ft) {
				ImGui::SameLine();
				char label[8];
				std::snprintf(label, sizeof label, "FT%d", ft);
				if (option_button(label, app.config.first_to == ft, ui(52))) {
					app.config.first_to = ft;
				}
			}
			ImGui::Dummy(ImVec2(0.f, ui(6)));
			if (ImGui::Button("Fight", ImVec2(ui(280), ui(44)))) {
				start_versus(app);
			}
			ImGui::Dummy(ImVec2(0.f, ui(2)));
			if (ImGui::Button("Back", ImVec2(ui(280), 0))) {
				app.mode_popup = 0;
			}
			ImGui::End();
		} else {
			ImGui::Begin("mode select", nullptr, box);
			ImGui::PushFont(app.fonts.head);
			ImGui::TextUnformatted("Choose a mode");
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (ImGui::Button("Ignition", ImVec2(ui(280), ui(44)))) {
				start_game(app, 0);
			}
			ImGui::TextDisabled("Endless. The fuse shortens as the levels");
			ImGui::TextDisabled("climb; burn bright for as long as you can.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (ImGui::Button("Blaze", ImVec2(ui(280), ui(44)))) {
				start_game(app, 1);
			}
			ImGui::TextDisabled("Three minutes on the clock; the multiplier");
			ImGui::TextDisabled("climbs as it drains. Make them count.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (ImGui::Button("Inferno", ImVec2(ui(280), ui(44)))) {
				start_game(app, 2);
			}
			ImGui::TextDisabled("The floor rises, the levels ramp, and the");
			ImGui::TextDisabled("fuse keeps shrinking.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (ImGui::Button("Meltdown / Bunker", ImVec2(ui(280), ui(44)))) {
				app.mode_popup = 1;
			}
			ImGui::TextDisabled("Race a stack of holey garbage down, or");
			ImGui::TextDisabled("outlast the rising floor. Cut to taste.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (ImGui::Button("Duel", ImVec2(ui(280), ui(44)))) {
				app.mode_popup = 2;
			}
			ImGui::TextDisabled("Fight the bot, rank D through X.");
			ImGui::Dummy(ImVec2(0.f, ui(6)));
			if (ImGui::Button("Back", ImVec2(ui(280), 0))) {
				app.screen = Screen::Menu;
			}
			ImGui::End();
		}
		ImGui::PopStyleVar();
	} else if (app.screen == Screen::Game && app.paused) {
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("paused", nullptr, box);
		ImGui::TextUnformatted("Paused");
		ImGui::Spacing();
		if (ImGui::Button("Resume", ImVec2(ui(240), 0))) {
			app.paused = false;
		}
		if (ImGui::Button("Restart", ImVec2(ui(240), 0))) {
			if (app.mode == 5) {
				start_versus(app, app.career_stage);
			} else {
				start_game(app, app.mode);
			}
		}
		if (ImGui::Button("Edit stat layout", ImVec2(ui(240), 0))) {
			open_layout_editor(app);
		}
		if (ImGui::Button("Settings", ImVec2(ui(240), 0))) {
			app.show_settings = true;
		}
		if (ImGui::Button("Back to menu", ImVec2(ui(240), 0))) {
			app.versus.reset();
			app.screen = Screen::Menu;
		}
		ImGui::End();
	} else if (app.screen == Screen::Over) {
		ImGui::SetNextWindowPos(ImVec2(middle.x, ui(6)),
			ImGuiCond_Always, ImVec2(0.5f, 0.f));
		ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ui(10), ui(6)));
		ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(ui(8), ui(3)));
		ImGui::Begin("game over", nullptr, box);
		const bool won = app.session.has_value() && app.session->sim().won();
		ImGui::PushFont(app.fonts.head);
		if (app.versus.has_value()) {
			ImGui::TextUnformatted(
				app.versus->player_wins > app.versus->bot_wins
					? "You win the match!" : "You lose the match");
		} else {
			ImGui::TextUnformatted(won ? "Finished!" : "Game over");
		}
		ImGui::PopFont();
		if (app.versus.has_value()) {
			ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f),
				"You %d - %d Bot (%s)  first to %d",
				app.versus->player_wins, app.versus->bot_wins,
				bot::ranks()[app.versus->rank_index].name,
				app.versus->first_to);
		}
		if (won) {
			const double seconds = app.session->sim().frame() * 0.02;
			ImGui::TextColored(ImVec4(0.255f, 0.776f, 0.878f, 1.f),
				"All the cheese in %d:%05.2f",
				static_cast<int>(seconds) / 60, std::fmod(seconds, 60.));
		}
		ImGui::Spacing();
		// What the run was made of, as the Python analysis screen lists it -
		// which needs the recording. A game too short for a file says so and
		// falls back to the totals the panels were already showing.
		if (app.last_replay.has_value()) {
			draw_analysis_rows(*app.last_replay);
		} else if (app.session.has_value()) {
			ImGui::TextDisabled("That run was too short to record.");
			draw_summary(*app.session);
		}
		ImGui::Spacing();
		if (app.hiscore_place >= 0 && !app.score_saved) {
			ImGui::TextColored(ImVec4(1.f, 0.82f, 0.29f, 1.f),
				"You got the %s place high score!",
				place_string(app.hiscore_place).c_str());
			ImGui::SetNextItemWidth(ui(150));
			ImGui::InputText("##name", app.name_entry, sizeof app.name_entry,
				ImGuiInputTextFlags_CallbackCharFilter,
				[] (ImGuiInputTextCallbackData* data) {
					const ImWchar c = data->EventChar;
					const bool fine = (c >= 'a' && c <= 'z')
						|| (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
						|| c == ' ';
					return fine ? 0 : 1;
				});
			ImGui::SameLine();
			if (ImGui::Button("Save score")) {
				// As the save menu stores it: the name padded to eight, the
				// clock converted to elapsed centiseconds.
				const Sim& sim = app.session->sim();
				hiscore::Entry entry;
				std::string name = app.name_entry;
				name.resize(8, ' ');
				std::copy(name.begin(), name.end(), entry.name.begin());
				entry.score = static_cast<std::uint64_t>(
					std::max<long long>(0, sim.final_score()));
				entry.lines = static_cast<std::uint32_t>(
					std::max(0, sim.final_lines()));
				entry.timer = static_cast<std::uint32_t>(app.mode == 1
					? std::max(0L, (static_cast<long>(sim.config().timer_ms)
						- sim.timer_ms()) / 10)
					: std::max(0L, sim.timer_ms() / 10));
				if (sim.config().fuse) {
					hiscore::submit_fuse(hiscore::folder(app.root),
						gametype_name(app.mode, true), entry);
				} else {
					hiscore::submit(hiscore::folder(app.root),
						gametype_name(app.mode, false), entry);
				}
				app.score_saved = true;
			}
		} else if (app.score_saved) {
			ImGui::TextDisabled("Score saved.");
		}
		if (app.last_replay.has_value()) {
			if (ImGui::Button("Full analysis", ImVec2(ui(240), 0))) {
				app.studying = app.last_replay;
				app.study_back = Screen::Over;
				app.screen = Screen::Analysis;
			}
			if (ImGui::Button("Watch replay", ImVec2(ui(240), 0))) {
				watch(app, *app.last_replay, Screen::Over);
			}
		} else {
			ImGui::TextDisabled("Too short to record.");
		}
		if (ImGui::Button("Play again", ImVec2(ui(240), 0))) {
			if (app.mode == 5) {
				start_versus(app, app.career_stage);
			} else {
				start_game(app, app.mode);
			}
		}
		if (ImGui::Button("Back to menu", ImVec2(ui(240), 0))) {
			app.versus.reset();
			app.screen = Screen::Menu;
		}
		ImGui::End();
		ImGui::PopStyleVar(2);
	}

	if (app.show_settings) {
		draw_settings(app);
	}
}

// --- The loop. -------------------------------------------------------------

// The screens the smoke run visits between games, in order. Each is opened
// the way a player opens it - with the data the screen reads actually
// loaded - so the headless run proves they draw, not merely that they build.
// A picture of the frame as drawn, before the present wipes it.
void capture_frame (SDL_Renderer* renderer, const std::string& path) {
	int w = 0;
	int h = 0;
	SDL_GetRendererOutputSize(renderer, &w, &h);
	SDL_Surface* grab = SDL_CreateRGBSurfaceWithFormat(
		0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (grab != nullptr && SDL_RenderReadPixels(renderer, nullptr,
		grab->format->format, grab->pixels, grab->pitch) == 0) {
		SDL_SaveBMP(grab, path.c_str());
	}
	SDL_FreeSurface(grab);
}

// Which screen the frame shows, for the one-shot-per-screen gallery
// FORCETRIS_SHOTS collects - the way a design change is looked at without a
// display, one BMP per screen the run visits.
std::string screen_shot_key (const App& app) {
	if (app.editing) {
		return "editor";
	}
	if (app.show_settings) {
		return "settings";
	}
	switch (app.screen) {
		case Screen::Menu: return "menu";
		case Screen::Modes:
			return app.mode_popup == 1 ? "modes_cheese"
				: app.mode_popup == 2 ? "modes_versus" : "modes";
		case Screen::Game:
			return app.paused ? "pause"
				: app.versus.has_value() ? "versus" : "game";
		case Screen::Over: return "over";
		case Screen::Replays: return "replays";
		case Screen::Viewer: return "viewer";
		case Screen::Scores: return "scores";
		case Screen::Help: return "help";
		case Screen::Analysis:
			return app.studying.has_value() ? "analysis" : "analysis_empty";
		case Screen::Profile: return "profile";
		case Screen::Career: return "career";
	}
	return "screen";
}

constexpr int kTour = 16;

void tour_screen (App& app, int stop) {
	switch (stop) {
		case 0:
			app.help_back = Screen::Over;
			app.screen = Screen::Help;
			break;
		case 1:
			app.score_page = 0;
			app.screen = Screen::Scores;
			break;
		case 2:
			// The trainer file's side of the screen, so both loaders draw.
			app.score_page = 8;
			app.screen = Screen::Scores;
			break;
		case 3:
			app.shelf = replay::listing(replay::folder(app.root));
			app.screen = Screen::Replays;
			break;
		case 4:
			// The analysis of the run just finished, or of the newest file
			// on the shelf if this game was too short to keep.
			app.studying = app.last_replay.has_value()
				? app.last_replay
				: (app.shelf.empty() ? std::nullopt
					: std::optional<replay::Replay>(app.shelf.front()));
			app.study_back = Screen::Over;
			app.screen = Screen::Analysis;
			break;
		case 5:
			// And the same screen with nothing to show, which is what a run
			// too short to record leaves it holding.
			app.studying.reset();
			app.study_back = Screen::Over;
			app.screen = Screen::Analysis;
			break;
		case 6:
			app.screen = Screen::Menu;
			app.show_settings = true;
			break;
		case 7:
			// The layout editor over the finished game's board.
			app.screen = Screen::Game;
			app.editing = true;
			app.place_panels = true;
			break;
		case 8:
			app.screen = Screen::Game;
			app.paused = true;
			break;
		case 9:
			// The editor as the settings screen opens it: over a throwaway
			// preview board, no game running.
			app.session.reset();
			app.screen = Screen::Menu;
			open_layout_editor(app);
			break;
		case 10:
			app.screen = Screen::Modes;
			app.mode_popup = 0;
			break;
		case 11:
			// The picker's two detail windows, cheese and versus.
			app.screen = Screen::Modes;
			app.mode_popup = 1;
			break;
		case 12:
			app.screen = Screen::Modes;
			app.mode_popup = 2;
			break;
		case 13:
			app.mode_popup = 0;
			app.history = profile::load(profile::path(app.root));
			app.screen = Screen::Profile;
			break;
		case 14:
			app.career = career::load(career::path(app.root));
			app.screen = Screen::Career;
			break;
		default:
			app.screen = Screen::Menu;
			break;
	}
}

#ifdef __ANDROID__
// The assets ride inside the APK, where fopen cannot see them. On first
// launch they are unpacked into the app's own storage - the build wrote
// assets.txt listing every file - and that storage becomes the game root,
// so every existing path in the game just works.
std::string android_root () {
	const char* base = SDL_AndroidGetInternalStoragePath();
	const std::string root = base != nullptr ? base : ".";
	SDL_RWops* list = SDL_RWFromFile("assets.txt", "rb");
	if (list == nullptr) {
		return root;
	}
	const Sint64 size = SDL_RWsize(list);
	std::string names(size > 0 ? static_cast<size_t>(size) : 0, '\0');
	SDL_RWread(list, names.data(), 1, names.size());
	SDL_RWclose(list);
	std::error_code ignored;
	size_t at = 0;
	while (at < names.size()) {
		size_t end = names.find('\n', at);
		if (end == std::string::npos) {
			end = names.size();
		}
		const std::string name = names.substr(at, end - at);
		at = end + 1;
		if (name.empty()) {
			continue;
		}
		const std::filesystem::path dest
			= std::filesystem::path(root) / name;
		if (std::filesystem::exists(dest, ignored)) {
			continue;
		}
		SDL_RWops* in = SDL_RWFromFile(name.c_str(), "rb");
		if (in == nullptr) {
			continue;
		}
		const Sint64 length = SDL_RWsize(in);
		std::vector<char> bytes(length > 0 ? static_cast<size_t>(length) : 0);
		SDL_RWread(in, bytes.data(), 1, bytes.size());
		SDL_RWclose(in);
		std::filesystem::create_directories(dest.parent_path(), ignored);
		std::ofstream out(dest, std::ios::binary);
		out.write(bytes.data(),
			static_cast<std::streamsize>(bytes.size()));
	}
	return root;
}
#endif

int run (bool smoke, long smoke_frames) {
	App app;
	app.config_file = config_path();
	app.config = load_config(app.config_file);
#ifdef __ANDROID__
	kMobile = true;
#else
	kMobile = std::getenv("FORCETRIS_MOBILE") != nullptr;
#endif

#ifdef SDL_HINT_WINDOWS_DPI_AWARENESS
	// Without this Windows draws the window at 96dpi and stretches the
	// bitmap to the display's scale, which blurs every pixel in it. With it
	// the window is laid out in real pixels and the scale below re-derives
	// the same design at the display's density.
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
#endif
	if (SDL_Init(SDL_INIT_VIDEO) != 0) {
		SDL_Log("SDL_Init: %s", SDL_GetError());
		return 1;
	}
#ifdef __ANDROID__
	app.root = android_root();
#else
	app.root = game_root();
#endif
	// No audio device is not a reason not to play; the mixer just stays shut.
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0) {
		app.audio.open(app.root);
		app.audio.set_sfx_volume(app.config.sfx_volume);
		app.audio.set_music_volume(app.config.music_volume);
	}
	float scale = 1.f;
	float dpi = 0.f;
	if (SDL_GetDisplayDPI(0, &dpi, nullptr, nullptr) == 0 && dpi > 0.f) {
		scale = std::clamp(dpi / 96.f, 1.f, 3.f);
	}
	// The escape hatch for displays that misreport their density - WSLg says
	// 96dpi while Windows stretches the result - and for anyone who simply
	// wants the whole game bigger.
	if (const char* forced = std::getenv("FORCETRIS_SCALE")) {
		const float wanted = static_cast<float>(std::atof(forced));
		if (wanted > 0.f) {
			scale = std::clamp(wanted, 1.f, 3.f);
		}
	}
	apply_ui_scale(scale);
	int want_w = px(1180);
	int want_h = px(700);
	if (kMobile) {
		// On a device SDL ignores the size and fills the screen. On a desk
		// FORCETRIS_MOBILE=WxH stands a phone-shaped window up, which is
		// how the phone layouts are looked at without a phone.
		want_w = 2280;
		want_h = 1080;
		if (const char* shape = std::getenv("FORCETRIS_MOBILE")) {
			int w = 0;
			int h = 0;
			if (std::sscanf(shape, "%dx%d", &w, &h) == 2
				&& w > 200 && h > 200) {
				want_w = w;
				want_h = h;
			}
		}
	}
	app.window = SDL_CreateWindow("Forcetris",
		SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, want_w, want_h,
		SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (app.window == nullptr) {
		SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
		return 1;
	}
	app.renderer = SDL_CreateRenderer(app.window, -1,
		smoke ? SDL_RENDERER_SOFTWARE
		      : SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (app.renderer == nullptr) {
		app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (app.renderer == nullptr) {
		SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
		return 1;
	}
	if (kMobile) {
		int w = 0;
		int h = 0;
		SDL_GetRendererOutputSize(app.renderer, &w, &h);
		apply_mobile_layout(w, h);
		layout_touch(app, w, h);
	}

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO().IniFilename = nullptr;
	apply_theme();
	app.fonts = load_fonts();
	ImGui_ImplSDL2_InitForSDLRenderer(app.window, app.renderer);
	ImGui_ImplSDLRenderer2_Init(app.renderer);

	std::mt19937 mash(20260818);
	if (smoke) {
		// The countdown would silently eat the smoke run's frame budget on
		// every start, so scripted runs skip it - unless FORCETRIS_COUNTDOWN
		// asks for it, which is how its overlay gets screenshotted.
		if (std::getenv("FORCETRIS_COUNTDOWN") == nullptr) {
			app.start_delay = 0;
		}
		// FORCETRIS_SMOKE_MODE picks which mode the scripted run plays, so
		// every mode's whole loop - dealer, session, end screen - can be
		// proven headlessly, not only free's.
		int mode = 0;
		if (const char* forced = std::getenv("FORCETRIS_SMOKE_MODE")) {
			mode = std::clamp(std::atoi(forced), 0, 5);
		}
		if (mode == 4) {
			app.config.cheese_period = 150;
		}
		if (mode == 5) {
			start_versus(app);
		} else {
			start_game(app, mode);
		}
	}

	Uint64 previous = SDL_GetPerformanceCounter();
	double behind = 0.;
	long frames = 0;
	// Where the per-screen gallery goes, if anywhere.
	const char* shots_dir = std::getenv("FORCETRIS_SHOTS");
	std::map<std::string, int> shot_frames;
	if (shots_dir != nullptr) {
		std::error_code ignored;
		std::filesystem::create_directories(shots_dir, ignored);
	}

	// The smoke run's tour of the screens a game never opens. It only starts
	// once a game has ended, and the viewer mode skips it, so the run is only
	// held to finishing a tour it was in a position to start.
	const bool touring = smoke && std::getenv("FORCETRIS_SMOKE_VIEW") == nullptr;
	int toured = 0;
	int tour_frames = 0;
	bool game_ended = false;
	while (!app.quit) {
		SDL_Event event;
		while (SDL_PollEvent(&event)) {
			handle_event(app, event);
		}

		if (smoke) {
			// Button mashing at a fixed seed, pushed as real SDL key events so
			// the run goes through the binding lookup and not around it. The
			// point is that the whole loop runs, draws and shuts down without
			// a display or a player.
			if (frames % 3 == 0) {
				const auto& actions = all_actions();
				const auto& codes = app.config.keys[actions[mash() % actions.size()].id];
				if (!codes.empty()) {
					SDL_Event pressed{};
					const bool down = mash() % 4 != 0;
					pressed.type = down ? SDL_KEYDOWN : SDL_KEYUP;
					pressed.key.state = down ? SDL_PRESSED : SDL_RELEASED;
					pressed.key.keysym.scancode =
						static_cast<SDL_Scancode>(codes[mash() % codes.size()]);
					pressed.key.keysym.sym =
						SDL_GetKeyFromScancode(pressed.key.keysym.scancode);
					SDL_PushEvent(&pressed);
				}
			}
			behind = 0.02;
		} else {
			const Uint64 now = SDL_GetPerformanceCounter();
			behind += static_cast<double>(now - previous)
				/ SDL_GetPerformanceFrequency();
			previous = now;
			behind = std::min(behind, 0.25);
		}

		while (behind >= 0.02) {
			behind -= 0.02;
			if (app.screen == Screen::Game && !app.paused && !app.editing) {
				if (app.countdown > 0) {
					// The pre-game breath: both boards stand frozen - the
					// versus step is skipped too, so the bot waits with you.
					--app.countdown;
					++frames;
					continue;
				}
				const bool live = app.session->step();
				if (app.versus.has_value()) {
					if (app.career_stage >= 0
						&& app.session->sim().overdrive()) {
						app.career_od = true;
					}
					app.versus->step(*app.session);
					// The round and match flow: linger on the verdict, then
					// the next round or the loss screen.
					if (app.versus->phase == VersusMatch::Phase::RoundOver) {
						if (++app.versus->phase_frames >= 150) {
							next_versus_round(app);
						}
					} else if (app.versus->phase
						== VersusMatch::Phase::MatchOver) {
						if (++app.versus->phase_frames >= 100) {
							end_game(app);
						}
					}
				} else if (!live) {
					end_game(app);
				}
				++frames;
			} else if (app.screen == Screen::Viewer && app.viewing.has_value()) {
				advance_viewer(app);
				if (smoke) {
					++frames;
				}
			} else if (smoke) {
				// A menu screen on the tour still counts, so the run always
				// reaches its frame budget rather than sitting on one.
				++frames;
			}
		}
		if (app.session.has_value()) {
			for (const std::string& cue : app.session->take_cues()) {
				app.audio.play(cue);
			}
		}

		if (app.relayout) {
			// The screen rotated: re-derive the layout, the touch buttons
			// and the fonts before this frame draws anything.
			app.relayout = false;
			int w = 0;
			int h = 0;
			SDL_GetRendererOutputSize(app.renderer, &w, &h);
			apply_mobile_layout(w, h);
			layout_touch(app, w, h);
			ImGui::GetIO().Fonts->Clear();
			app.fonts = load_fonts();
			ImGui_ImplSDLRenderer2_DestroyDeviceObjects();
			ImGui::GetStyle() = ImGuiStyle();
			apply_theme();
			app.place_panels = true;
		}
		ImGui_ImplSDLRenderer2_NewFrame();
		ImGui_ImplSDL2_NewFrame();
		ImGui::NewFrame();

		SDL_SetRenderDrawColor(app.renderer, 12, 15, 20, 255);
		SDL_RenderClear(app.renderer);
		if (app.session.has_value()
			&& (app.screen == Screen::Game || app.screen == Screen::Over)) {
			draw_board(app);
			draw_label("HOLD", kBoardX - ui(122), kBoardY - ui(24));
			draw_label("NEXT", kBoardX + kBoardW + ui(18), kBoardY - ui(24));
			if (app.session->sim().config().fuse) {
				draw_label("FLOW", kBoardX - px(56), kBoardY + px(98));
				if (app.session->sim().overdrive()) {
					draw_label("OVERDRIVE", kBoardX - px(56),
						kBoardY + kBoardH - px(4),
						IM_COL32(255, 214, 96, 255));
				}
			}
			if (!kPortrait) {
				// A phone held upright has no margin for the stat panels;
				// the board and the fight are the screen.
				draw_stat_panels(app);
			}
			draw_banner(app);
			if (app.versus.has_value()) {
				draw_versus_panel(app);
			}
			if (app.screen == Screen::Game && app.countdown > 0) {
				draw_countdown(app);
			}
			if (app.screen == Screen::Game && !app.paused && !app.editing) {
				draw_touch(app);
			}
		}
		if (app.screen == Screen::Viewer && app.viewing.has_value()) {
			draw_label("HOLD", kBoardX - ui(122), kBoardY - ui(24));
			draw_label("NEXT", kBoardX + kBoardW + ui(18), kBoardY - ui(24));
			draw_viewer(app);
		}
		if (app.screen == Screen::Replays) {
			draw_replays(app);
		}
		if (app.screen == Screen::Scores) {
			draw_scores(app);
		}
		if (app.screen == Screen::Profile) {
			draw_profile(app);
		}
		if (app.screen == Screen::Career) {
			draw_career(app);
		}
		if (app.screen == Screen::Analysis) {
			draw_analysis(app);
		}
		if (app.screen == Screen::Help) {
			draw_help(app);
		}
		if (app.editing) {
			draw_layout_editor(app);
		}
		if (app.layout_preview && !app.editing) {
			// The throwaway preview board goes with the editor, back to the
			// settings screen it was opened from.
			app.layout_preview = false;
			app.session.reset();
			app.screen = Screen::Menu;
			app.show_settings = true;
		}
		draw_menus(app);

		ImGui::Render();
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), app.renderer);

		if (smoke && frames >= smoke_frames) {
			// A picture of the last frame, before the present wipes it, so a
			// headless run can be looked at rather than taken on faith.
			if (const char* shot = std::getenv("FORCETRIS_SHOT")) {
				capture_frame(app.renderer, shot);
			}
		}
		if (shots_dir != nullptr) {
			// One picture per screen, taken a few frames in so the layout
			// has settled.
			const std::string key = screen_shot_key(app);
			if (++shot_frames[key] == 4) {
				capture_frame(app.renderer,
					(std::filesystem::path(shots_dir) / (key + ".bmp")).string());
			}
		}
		SDL_RenderPresent(app.renderer);

		if (smoke) {
			if (app.screen == Screen::Over) {
				game_ended = true;
				// With FORCETRIS_SMOKE_VIEW set the run ends in the replay
				// viewer instead of another game, so the screenshot shows a
				// recording being re-enacted.
				if (!touring && app.last_replay.has_value()) {
					watch(app, *app.last_replay, Screen::Menu);
				} else if (toured < kTour) {
					// Between games, walk the screens a game never opens, a
					// few frames each: every one of them has to build and
					// draw with real data behind it, not merely compile.
					tour_screen(app, toured++);
				} else if (app.mode == 5) {
					start_versus(app);
				} else {
					start_game(app, app.mode);
				}
			} else if (toured > 0 && toured <= kTour
				&& app.screen != Screen::Viewer
				&& (app.screen != Screen::Game || app.paused || app.editing)) {
				if (++tour_frames >= 6) {
					tour_frames = 0;
					app.screen = Screen::Over;
					app.studying.reset();
					app.show_settings = false;
					app.editing = false;
					app.paused = false;
				}
			}
			if (frames >= smoke_frames) {
				app.quit = true;
			}
		}
	}

	save_config(app.config, app.config_file);
	ImGui_ImplSDLRenderer2_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	SDL_DestroyRenderer(app.renderer);
	SDL_DestroyWindow(app.window);
	SDL_Quit();
	if (smoke) {
		SDL_Log("smoke: ran %ld frames, toured %d of %d screens",
			frames, toured, kTour);
		if (touring && game_ended && toured < kTour) {
			// The tour is the only thing that opens the menu screens, so a
			// run that had the chance to walk them and did not finish proves
			// nothing about them. A budget too short to end a single game is
			// not held to it - there was never a screen to open.
			SDL_Log("smoke: the screen tour did not finish");
			return 1;
		}
	}
	return 0;
}

} // namespace
} // namespace gui
} // namespace forcetris

int main (int, char* []) {
	long smoke_frames = 0;
	if (const char* smoke = std::getenv("FORCETRIS_SMOKE")) {
		smoke_frames = std::strtol(smoke, nullptr, 10);
	}
	return forcetris::gui::run(smoke_frames > 0, smoke_frames);
}
