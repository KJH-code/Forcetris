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
#include <cstring>
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
#include "forcetris/campaign.hpp"
#include "forcetris/career.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/munch.hpp"
#include "forcetris/profile.hpp"
#include "forcetris/rating.hpp"
#include "forcetris/replay.hpp"
#include "forcetris/temper.hpp"
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
	style.FrameRounding = 8.f;
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
	style.FrameBorderSize = 1.f;
	style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
	style.SeparatorTextBorderSize = 2.f;

	// The ember palette: soot grounds, ember accents, gold for Overdrive.
	// The game is about things burning; the chrome smoulders to match.
	const ImVec4 canvas(0.086f, 0.063f, 0.047f, 0.98f);   // #161008
	const ImVec4 well(0.165f, 0.122f, 0.086f, 1.f);       // #2A1F16
	const ImVec4 wellHover(0.216f, 0.157f, 0.110f, 1.f);
	const ImVec4 wellActive(0.267f, 0.192f, 0.133f, 1.f);
	const ImVec4 accent(1.f, 0.541f, 0.227f, 1.f);        // Ember.
	const ImVec4 accentDim(1.f, 0.541f, 0.227f, 0.28f);
	const ImVec4 edge(0.376f, 0.267f, 0.176f, 0.60f);
	const ImVec4 text(0.957f, 0.929f, 0.894f, 1.f);
	const ImVec4 faded(0.616f, 0.549f, 0.471f, 1.f);

	ImVec4* colors = style.Colors;
	colors[ImGuiCol_Text] = text;
	colors[ImGuiCol_TextDisabled] = faded;
	colors[ImGuiCol_WindowBg] = canvas;
	colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_PopupBg] = ImVec4(0.106f, 0.078f, 0.055f, 0.98f);
	colors[ImGuiCol_Border] = edge;
	colors[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_FrameBg] = well;
	colors[ImGuiCol_FrameBgHovered] = wellHover;
	colors[ImGuiCol_FrameBgActive] = wellActive;
	colors[ImGuiCol_TitleBg] = ImVec4(0.067f, 0.049f, 0.035f, 1.f);
	colors[ImGuiCol_TitleBgActive] = ImVec4(0.098f, 0.071f, 0.051f, 1.f);
	colors[ImGuiCol_TitleBgCollapsed] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_MenuBarBg] = colors[ImGuiCol_TitleBg];
	colors[ImGuiCol_ScrollbarBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	colors[ImGuiCol_ScrollbarGrab] = well;
	colors[ImGuiCol_ScrollbarGrabHovered] = wellHover;
	colors[ImGuiCol_ScrollbarGrabActive] = wellActive;
	colors[ImGuiCol_CheckMark] = accent;
	colors[ImGuiCol_SliderGrab] = accent;
	colors[ImGuiCol_SliderGrabActive] = ImVec4(1.f, 0.671f, 0.373f, 1.f);
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
	colors[ImGuiCol_TableHeaderBg] = ImVec4(0.106f, 0.078f, 0.055f, 1.f);
	colors[ImGuiCol_TableBorderStrong] = edge;
	colors[ImGuiCol_TableBorderLight] = ImVec4(0.302f, 0.220f, 0.149f, 0.35f);
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
	// The Forge Road: what has been earned and bought, which stage the run
	// in play belongs to (-1 for every ordinary game), whether this boss
	// fight saw the player ignite, and what the last settlement paid - the
	// over screen shows the receipt.
	campaign::State campaign;
	int campaign_stage = -1;
	bool campaign_od = false;
	int last_stage_stars = 0;
	int last_slag_gain = 0;
	// Tempering: the rules the run started from, the tempers drafted since,
	// how many heats have been forged, and the three cards on the table
	// right now - which is also the flag for "the board is waiting".
	SimConfig temper_start;
	std::vector<std::string> tempers;
	unsigned temper_seed = 0;
	// The duel's other side, fixed for the whole match: the rules the bot
	// builds from (a campaign boss's terms carry none of the player's
	// permanent metal) and the blade forged into them each round.
	SimConfig versus_bot_base;
	std::vector<std::string> versus_blade;
	int heat = 0;
	std::vector<std::string> offers;
	int offer_at = 0;
	int offer_shown = 0;         // Frames the cards have been on the table.
	// The run's coin. The balance is derived - what the sim's totals have
	// earned, minus what the draft screen has spent - so there is no
	// second counter to fall out of step with the board.
	int ember_spent = 0;
	int ember_bonus = 0;         // Percent, from the Anvil; campaign only.
	unsigned offer_salt = 0;     // Rerolls of the current hand.
	int extra_picks = 0;         // Second cards bought on this hand.
	bool offer_taken = false;    // A card has left this hand already.
	// Preheat's free draft, owed but not yet dealt: the cards wait behind
	// the starting countdown instead of being drawn over it.
	bool preheat_owed = false;
	// Smooth motion: the piece as it stood before the last sim tick, so the
	// renderer can draw the space between two 20ms steps instead of only
	// the steps. lerp_have is false whenever the last tick carried input -
	// a pressed key's effect must land on the very next drawn frame, not
	// slide in over the following twenty milliseconds.
	Piece lerp_prev;
	bool lerp_have = false;
	float lerp_alpha = 1.f;      // 0 at the tick, 1 just before the next.
	// The F3 frame diagnostics: recent render times and how many sim ticks
	// each render carried, so a stutter report can arrive with numbers.
	bool show_frames = false;
	float frame_ms[120] = {};
	int frame_at = 0;
	int tick_hist[3] = {};       // Renders that carried 0, 1, 2+ ticks.
	// The juice: a fixed pool of clear sparks (a hard cap, not a queue -
	// an overflowing celebration recycles its oldest embers), and the
	// frame the board stops shuddering.
	struct Spark {
		float x = 0.f, y = 0.f, vx = 0.f, vy = 0.f;
		int life = 0;
		SDL_Color color{255, 255, 255, 255};
	};
	std::array<Spark, 256> sparks{};
	size_t spark_at = 0;
	long shake_until = -1;
	// The menu screens' ambient embers, drifting up behind the windows.
	std::array<Spark, 48> embers{};
	size_t ember_at = 0;
	// A game key arrived this loop: the sim may borrow its next tick early
	// (one tick at most - the accumulator repays), so the press lands now
	// instead of waiting out the 20ms boundary.
	bool input_nudge = false;
	// The intensity layer: attack streaks in flight between the boards,
	// the ignition flash and banner countdowns, and the last pending-
	// garbage count so a fresh landing can hit back.
	struct Streak {
		float sx = 0.f, sy = 0.f, tx = 0.f, ty = 0.f;
		float at = 0.f;         // 0 to 1 along the arc; <0 = free slot.
		int rows = 0;
	};
	std::array<Streak, 16> streaks{};
	size_t streak_at = 0;
	int od_flash = 0;           // Frames of the ignition flash left.
	int od_banner = 0;          // Frames of the OVERDRIVE banner left.
	bool was_overdrive = false;
	bool was_pressured = false;
	int hit_flash = 0;          // Frames of the garbage-landing flash.
	// Rows going white-hot as they clear: the row, and the frames left of
	// its burn. Kept here so an instant clear - where the row is spliced
	// out on the lock frame - still gets its moment.
	struct BurnRow {
		int row = 0;
		int life = 0;
	};
	std::array<BurnRow, 8> burn_rows{};
	size_t burn_at = 0;
	long burn_seen_lock = -1;   // The lock whose rows were already lit.
	long backdrop_tick = 0;     // A clock for the backdrop, alive everywhere.
	// One soft radial sprite, built once: every glow in the game is this
	// texture stretched and tinted. Stacked translucent rectangles band
	// into visible bricks; a falloff sprite simply does not.
	SDL_Texture* glow = nullptr;
	// The other built-by-hand sprite: a strip of flame frames, cycled so
	// the fire actually moves. Overdrive stands them outside the well and
	// every menu plate has them licking up from behind it.
	SDL_Texture* flames = nullptr;
	int last_pending = 0;
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
			case 6: return "temper";
			default: return "ignition";
		}
	}
	switch (mode) {
		case 1: return "timed";
		case 2: return "arcade";
		case 3: return "cheese_race";
		case 4: return "cheese_survival";
		case 5: return "versus";
		// Tempering is a fuse mode and nothing else: its drafts are the
		// fuse's own numbers, so there is no trainer-rules version of it to
		// name. A run started here is forced onto the variant's rules.
		case 6: return "temper";
		default: return "free";
	}
}

// A number with thousands separators, for the one place a score is shown
// big enough that the digits need grouping to be read at a glance.
std::string grouped (long long value) {
	std::string digits = std::to_string(value < 0 ? -value : value);
	for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3) {
		digits.insert(static_cast<size_t>(at), ",");
	}
	return value < 0 ? "-" + digits : digits;
}

std::string place_string (int at) {
	if (at == 0) return "1st";
	if (at == 1) return "2nd";
	if (at == 2) return "3rd";
	return std::to_string(at + 1) + "th";
}

// The fuse block on a recording: which ruleset, and every number it was
// played under. One writer, because a file that says fuse-rules without
// saying under which numbers cannot be read back honestly - and Tempering
// starts from rules the settings screen never saw.
void stamp_fuse (replay::Meta& meta, const SimConfig& rules) {
	meta.fuse = rules.fuse;
	if (!rules.fuse) {
		return;
	}
	meta.fuse_base = rules.fuse_base;
	meta.fuse_min = rules.fuse_min;
	meta.fuse_decay = rules.fuse_decay;
	meta.fuse_bank_cap = rules.fuse_bank_cap;
	meta.fuse_draw_cap = rules.fuse_draw_cap;
	meta.fuse_refuel_line = rules.fuse_refuel_line;
	meta.fuse_refuel_attack = rules.fuse_refuel_attack;
	meta.flash_frac = rules.flash_frac;
	meta.flash_floor = rules.flash_floor;
	meta.flow_gain_line = rules.flow_gain_line;
	meta.flow_gain_attack = rules.flow_gain_attack;
	meta.flow_lock_gain = rules.flow_lock_gain;
	meta.flow_flash_gain = rules.flow_flash_gain;
	meta.flow_burn_loss = rules.flow_burn_loss;
	meta.overdrive_secs = rules.overdrive_secs;
	meta.overdrive_mult = rules.overdrive_mult;
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
	meta.gametype = gametype_name(mode, rules.fuse);
	stamp_fuse(meta, rules);
	return meta;
}

// Everything the last game left burning. The shake deadline is a *sim
// frame number*, and a new game starts counting from zero again - so a
// deadline inherited from a long round would shake the opening of the
// next one for as many frames as the last one lasted. The rest of the
// intensity state carries the same way: countdowns mid-tick, the
// pending-garbage watermark, the ignition and pressure edges, live
// sparks and streaks, and the music left running hot.
void reset_effects (App& app) {
	app.shake_until = -1;
	app.last_pending = 0;
	app.od_flash = 0;
	app.od_banner = 0;
	app.hit_flash = 0;
	for (App::BurnRow& row : app.burn_rows) {
		row.life = 0;
	}
	app.burn_seen_lock = -1;
	app.was_overdrive = false;
	app.was_pressured = false;
	for (App::Spark& spark : app.sparks) {
		spark.life = 0;
	}
	for (App::Streak& streak : app.streaks) {
		streak.at = -1.f;
	}
	app.audio.set_music_rate(1.f);
}

void start_game (App& app, int mode,
		std::optional<unsigned> fixed_seed = std::nullopt) {
	app.versus.reset();
	app.career_stage = -1;
	app.campaign_stage = -1;
	app.daily_run = false;
	app.mode = mode;
	app.tempers.clear();
	app.offers.clear();
	app.offer_at = 0;
	app.offer_shown = 0;
	app.heat = 0;
	app.ember_spent = 0;
	app.ember_bonus = 0;
	app.offer_salt = 0;
	app.extra_picks = 0;
	app.offer_taken = false;
	// The dials just used are worth keeping even if the app never gets a
	// clean exit - phones rarely grant one.
	save_config(app.config, app.config_file);
	SimConfig config = app.config.sim();
	config.gametype = mode;
	if (mode == 6) {
		// Tempering is an endless game with a finish line and a draft. The
		// fuse is not optional here: every temper in the pool is one of the
		// fuse's own numbers, so a trainer-rules run would have nothing to
		// draft. The sim is told it is game zero; the mode lives in the
		// quota, the draft, and the table the score goes to.
		config.gametype = 0;
		config.fuse = true;
		config.line_quota = temper::kQuota;
	}
	if (config.fuse && mode == 1) {
		// Blaze burns three minutes, not the trainer's five. Its own table
		// keeps its scores, so the shorter clock competes only with itself.
		config.timer_ms = 3 * 60 * 1000;
	}
	config.cheese_total = app.config.cheese_total;
	config.cheese_period = app.config.cheese_period;
	config.cheese_holes = app.config.cheese_holes;
	config.cheese_messiness = app.config.cheese_messiness;
	const unsigned seed = fixed_seed.has_value() ? *fixed_seed : app.seeds();
	app.temper_seed = seed;
	app.temper_start = config;
	replay::Meta meta = meta_for(app.config, mode);
	if (mode == 6) {
		// meta_for reads the Rules tab; Tempering overrides it, so the file
		// records the rules the run is actually starting from.
		stamp_fuse(meta, config);
		meta.gametype = gametype_name(mode, true);
	}
	app.session.emplace(config, seed, meta);
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.countdown = app.start_delay;
	app.preheat_owed = false;
	reset_effects(app);
	app.audio.start_music();
}

// A match against the bot: the player's session as ever, the opponent and
// the scoreboard beside it.
// One round of a duel put on the table: the player's session, the bot's,
// and the draft state both sides start the round with. Shared by the match's
// first round and every one after it, so the rules a round is played under -
// the career stage's tightened fuse included - can never quietly differ
// between round one and round two, which is exactly the bug the old
// duplicated construction had.
void deal_versus_round (App& app) {
	// A duel never drafts, so the temper state is scrubbed rather than
	// re-dealt: a leftover solo build would print on the pause screen, and
	// leftover offers would freeze the match under a card nobody can see.
	app.tempers.clear();
	app.offers.clear();
	const unsigned seed = app.seeds();
	// The rules this round is played under were fixed by start_versus -
	// meta_for reads the Rules tab, which the career stage overrides, so
	// the recording is stamped from the config actually in force.
	replay::Meta meta = meta_for(app.config, 5);
	stamp_fuse(meta, app.temper_start);
	app.session.emplace(app.temper_start, seed, meta);
	// The bot arrives armed rather than drafting: its blade is the round's
	// identity, the same fight every time this rank is fought. A campaign
	// boss overrides both its base rules and its blade.
	app.versus->begin_round(app.seeds(), meta, app.versus_bot_base,
		app.versus_blade);
	app.countdown = app.start_delay;
	app.preheat_owed = false;
	reset_effects(app);
}

void start_versus (App& app, int career_stage = -1) {
	app.mode = 5;
	app.career_stage = career_stage;
	app.campaign_stage = -1;
	app.career_od = false;
	app.campaign_od = false;
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
	// The round rules, tightened and all, kept for every round of the
	// match. In a plain duel the bot builds from the same rules; its blade
	// is its rank's standard issue.
	app.temper_start = config;
	app.versus_bot_base = config;
	app.versus_blade = temper::blade_for(rank);
	app.versus.emplace(rank, first_to);
	deal_versus_round(app);
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
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
	app.versus->round += 1;
	deal_versus_round(app);
}

// What each family of temper is painted in: fuel the iron's own orange,
// Flow the gold Overdrive already uses, risk the danger red the board's
// edges close in with, and the two rule cards a pale hot white so they read
// as the rare ones they are.
ImU32 family_ink (temper::Family family) {
	switch (family) {
		case temper::Family::Flow: return IM_COL32(255, 206, 96, 255);
		case temper::Family::Risk: return IM_COL32(226, 92, 62, 255);
		case temper::Family::Rule: return IM_COL32(240, 234, 220, 255);
		case temper::Family::Fuel:
		default: return IM_COL32(214, 128, 62, 255);
	}
}

// The family's one-word tag, as the card wears it.
const char* family_tag (temper::Family family) {
	switch (family) {
		case temper::Family::Flow: return "FLOW";
		case temper::Family::Risk: return "RISK";
		case temper::Family::Rule: return "RULE";
		case temper::Family::Fuel:
		default: return "FUEL";
	}
}

// The family's glyph, drawn in primitives the way the mode buttons draw
// theirs: a flame for fuel, a bolt for Flow, a blade for risk, a scroll
// bar for the rule cards. Small on purpose - it is a stamp, not an icon
// set, and it reads at a glance next to the tag word.
void family_glyph (ImDrawList* draw, temper::Family family, ImVec2 at,
		float size, ImU32 ink) {
	const float r = size / 2.f;
	const ImVec2 mid(at.x + r, at.y + r);
	switch (family) {
		case temper::Family::Flow:
			// A bolt: two strokes with a kink.
			draw->AddTriangleFilled(ImVec2(mid.x + r * 0.5f, at.y),
				ImVec2(mid.x - r * 0.7f, mid.y + r * 0.25f),
				ImVec2(mid.x + r * 0.1f, mid.y + r * 0.05f), ink);
			draw->AddTriangleFilled(ImVec2(mid.x - r * 0.5f, at.y + size),
				ImVec2(mid.x + r * 0.7f, mid.y - r * 0.25f),
				ImVec2(mid.x - r * 0.1f, mid.y - r * 0.05f), ink);
			break;
		case temper::Family::Risk:
			// A blade, point down.
			draw->AddTriangleFilled(ImVec2(mid.x, at.y + size),
				ImVec2(mid.x - r * 0.45f, at.y + r * 0.5f),
				ImVec2(mid.x + r * 0.45f, at.y + r * 0.5f), ink);
			draw->AddRectFilled(ImVec2(mid.x - r * 0.8f, at.y + r * 0.28f),
				ImVec2(mid.x + r * 0.8f, at.y + r * 0.52f), ink);
			break;
		case temper::Family::Rule:
			// A scroll: three lines of law.
			for (int row = 0; row < 3; ++row) {
				draw->AddRectFilled(
					ImVec2(at.x + r * 0.2f, at.y + r * 0.35f + row * r * 0.55f),
					ImVec2(at.x + size - r * 0.2f,
						at.y + r * 0.6f + row * r * 0.55f), ink);
			}
			break;
		case temper::Family::Fuel:
		default:
			// The flame the mode buttons already speak.
			draw->AddTriangleFilled(ImVec2(mid.x, at.y),
				ImVec2(mid.x - r * 0.75f, mid.y + r * 0.7f),
				ImVec2(mid.x + r * 0.75f, mid.y + r * 0.7f), ink);
			draw->AddCircleFilled(ImVec2(mid.x, mid.y + r * 0.45f),
				r * 0.62f, ink);
			break;
	}
}

// A run's build on one line: "Quench x2, Bellows, Overheat".
std::string temper_line (const std::vector<std::string>& taken) {
	std::vector<std::pair<std::string, int>> counted;
	for (const std::string& id : taken) {
		const temper::Temper* card = temper::find(id);
		// An id from a newer build has no name here; it is still part of
		// the run, so it is counted under its own key rather than dropped.
		const std::string name = card != nullptr ? card->name : id;
		bool seen = false;
		for (auto& entry : counted) {
			if (entry.first == name) {
				++entry.second;
				seen = true;
				break;
			}
		}
		if (!seen) {
			counted.emplace_back(name, 1);
		}
	}
	std::string line;
	for (const auto& [name, times] : counted) {
		if (!line.empty()) {
			line += ", ";
		}
		line += name;
		if (times > 1) {
			line += " x" + std::to_string(times);
		}
	}
	return line;
}

// One stage of the Forge Road put on the table. The recipe decides
// everything - the mode, the finish line, the overrides, the boss's terms -
// and the launcher's whole job is to hand those decisions to the same
// machinery every ordinary game uses, plus the three things a recipe can
// ask for that no ordinary game does: a preset board, a fuse that burns
// hot from the first frame, and the Anvil's permanent metal on the
// player's side only.
void start_stage (App& app, int index) {
	if (index < 0 || index >= static_cast<int>(campaign::stages().size())) {
		return;
	}
	const campaign::Stage& stage
		= campaign::stages()[static_cast<size_t>(index)];
	app.versus.reset();
	app.career_stage = -1;
	app.career_od = false;
	app.campaign_od = false;
	app.daily_run = false;
	app.campaign_stage = index;
	app.mode = stage.mode;
	app.tempers.clear();
	app.offers.clear();
	app.offer_at = 0;
	app.offer_shown = 0;
	app.heat = 0;
	app.ember_spent = 0;
	app.offer_salt = 0;
	app.extra_picks = 0;
	app.offer_taken = false;
	app.ember_bonus = campaign::ember_bonus_percent(app.campaign.forge);
	save_config(app.config, app.config_file);

	SimConfig base = app.config.sim();
	base.gametype = stage.mode == 5 ? 5 : stage.mode;
	if (stage.mode == 5) {
		base.cheese_holes = 1;
		base.cheese_messiness = 30;
	}
	const SimConfig mine = campaign::stage_config(stage, base,
		app.campaign.forge);
	const unsigned seed = app.seeds();
	app.temper_seed = seed;
	app.temper_start = mine;
	replay::Meta meta = meta_for(app.config, stage.mode);
	stamp_fuse(meta, mine);
	// Its own name in every record: fuse_table_for has no fall-through, so
	// an Anvil-boosted score can never reach an ordinary table even if the
	// probe gate below were lost.
	meta.gametype = "campaign";

	if (stage.mode == 5) {
		app.versus_bot_base = campaign::bot_config(stage, base);
		app.versus_blade = campaign::blade_of(stage);
		app.session.emplace(mine, seed, meta);
		app.versus.emplace(stage.rank, stage.first_to);
		app.versus->begin_round(app.seeds(), meta, app.versus_bot_base,
			app.versus_blade);
		app.countdown = app.start_delay;
		reset_effects(app);
	} else {
		app.session.emplace(mine, seed, meta);
		const std::vector<std::string> rows = campaign::board_rows(stage);
		if (!rows.empty()) {
			// Legal before the first step, and the countdown guarantees
			// there has been none.
			app.session->sim_mutable().seed(Board::from_rows(rows));
		}
		if (stage.pressure) {
			// Sticks for the whole game: nothing solo ever rewrites it.
			app.session->sim_mutable().set_pressure(true);
		}
		app.countdown = app.start_delay;
		reset_effects(app);
		// Preheat: the first draft comes free at the door - but the door
		// has a countdown on it, so the hand is owed now and dealt the
		// frame the countdown expires. Dealing here drew the cards under
		// the 3-2-1 and both fought for the same screen.
		app.preheat_owed = campaign::free_drafts(app.campaign.forge) > 0;
	}
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.audio.start_music();
}

// What the run's coin purse holds right now: earned by the sim's own
// monotone totals - lines, and the attack they carried - swollen by the
// Anvil's ember sense in a campaign stage, minus what the draft screen has
// spent. Derived rather than accumulated, so it cannot drift.
int ember_balance (const App& app) {
	if (!app.session.has_value()) {
		return 0;
	}
	const Sim& sim = app.session->sim();
	const int earned = temper::embers_of(sim.lines_cleared(),
		sim.attack_sent());
	return earned + earned * app.ember_bonus / 100 - app.ember_spent;
}

// A heat has been forged if the run's counter has crossed another rung -
// lines everywhere, dug rows in Meltdown. Put three cards on the table when
// it has; the loop stops stepping the sim while they are there, so the fuse
// waits with the player. This fires for every fuse-rules game: the draft is
// part of the fuse ruleset, not one mode's gimmick. Only Tempering has a
// ceiling, because only Tempering has a finish line; everywhere else the
// forge keeps dealing until the pool runs dry.
void offer_tempers (App& app) {
	if (!app.session.has_value() || !app.session->sim().config().fuse) {
		return;
	}
	if (app.mode == 6 && app.heat >= temper::kHeats) {
		return;
	}
	const Sim& sim = app.session->sim();
	const int forged = temper::heats_done(
		sim.lines_cleared(), sim.downstack(), app.mode == 3);
	if (forged <= app.heat) {
		return;
	}
	app.offers = temper::offer(app.temper_seed, app.heat, app.tempers);
	app.offer_at = 0;
	app.offer_shown = 0;
	app.offer_salt = 0;
	app.extra_picks = 0;
	app.offer_taken = false;
	if (app.offers.empty()) {
		// The pool ran dry - nineteen stacks is all there is - and a draft
		// with nothing to draft must not stop the game.
		app.heat = forged;
		return;
	}
	app.audio.play("overdrive");
}

// One card taken: the run's rules are rebuilt from the start plus every
// temper in order, which is what makes a stack of the same card add up
// rather than each one overwrite the last.
void take_temper (App& app, int at) {
	if (!app.session.has_value() || at < 0
		|| at >= static_cast<int>(app.offers.size())) {
		return;
	}
	app.tempers.push_back(app.offers[at]);
	app.session->draft(temper::tempered(app.temper_start, app.tempers),
		app.tempers.back());
	app.audio.play("b2b");
	// A bought second pick keeps the hand open, minus the card just taken;
	// the heat advances once per hand, not once per card.
	if (app.extra_picks > 0 && app.offers.size() > 1) {
		--app.extra_picks;
		app.offer_taken = true;
		app.offers.erase(app.offers.begin() + at);
		app.offer_at = std::min(app.offer_at,
			static_cast<int>(app.offers.size()) - 1);
		return;
	}
	app.offers.clear();
	app.offer_shown = 0;
	app.offer_taken = false;
	app.extra_picks = 0;
	++app.heat;
}

// The two things the coin buys, both on the draft screen: the same heat
// dealt again, and a second card off the same table.
void reroll_offer (App& app) {
	if (app.offer_taken || ember_balance(app) < temper::kRerollCost) {
		return;
	}
	app.ember_spent += temper::kRerollCost;
	app.offers = temper::offer(app.temper_seed, app.heat, app.tempers,
		++app.offer_salt);
	app.offer_at = 0;
	app.audio.play("rotate");
}

void buy_extra_pick (App& app) {
	if (app.extra_picks > 0 || app.offers.size() < 2
		|| ember_balance(app) < temper::kExtraPickCost) {
		return;
	}
	app.ember_spent += temper::kExtraPickCost;
	app.extra_picks = 1;
	app.audio.play("hold");
}

void end_game (App& app) {
	// Saved rather than offered, the way the Python game does it: the moment
	// a run ends is the worst moment to ask someone whether they will want
	// to look at it.
	app.screen = Screen::Over;
	app.audio.fade_music(2.5);
	// A game that ended mid-Overdrive leaves the sim's flag stuck on; the
	// track would stay fast through the loss screen and the menu behind it.
	app.audio.set_music_rate(1.f);
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
	// The Forge Road's settlement: stars only ever upward, slag always -
	// a win pays the stage's bounty, a death renders the unspent embers
	// down. Written before the table probe, which a stage never enters.
	if (app.campaign_stage >= 0) {
		const campaign::Stage& stage
			= campaign::stages()[static_cast<size_t>(app.campaign_stage)];
		bool won = false;
		int stars = 0;
		if (stage.mode == 5 && app.versus.has_value()) {
			won = app.versus->player_wins > app.versus->bot_wins;
			const bool sweep = won && app.versus->bot_wins == 0;
			stars = campaign::boss_stars(won, sweep, app.campaign_od);
		} else if (app.session.has_value()) {
			const Sim& sim = app.session->sim();
			won = sim.won();
			stars = campaign::solo_stars(won, sim.frame() * 0.02,
				stage.par_seconds, app.session->forced());
		}
		const auto held = app.campaign.stars.find(stage.id);
		const bool first_clear
			= held == app.campaign.stars.end() || held->second == 0;
		const int slag = campaign::slag_award(stage, first_clear, won, stars,
			ember_balance(app));
		app.campaign.slag += slag;
		if (stars > app.campaign.stars[stage.id]) {
			app.campaign.stars[stage.id] = stars;
		}
		campaign::save(campaign::path(app.root), app.campaign);
		app.last_stage_stars = stars;
		app.last_slag_gain = slag;
	}
	// Would this run make the table? The probe carries the raw clock value,
	// exactly as eval_loss probes it - the conversion to stored centiseconds
	// only happens if a name is entered and the score actually submitted.
	// The loss-time counters: eval_loss probes before a still-resolving
	// clear lands its points, so the snapshot does too.
	const bool fused = app.session->sim().config().fuse;
	if (app.campaign_stage >= 0) {
		// A stage never enters a table: its rules carry the Anvil's metal,
		// and its record already says "campaign" - a name no table owns.
		app.hiscore_place = -1;
	} else if (fused) {
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
				&& app.countdown <= 0 && app.offers.empty()
				&& app.session.has_value()) {
				for (size_t i = 0; i < app.touch.size(); ++i) {
					const SDL_Rect& rect = app.touch[i].rect;
					if (x >= rect.x && x < rect.x + rect.w
						&& y >= rect.y && y < rect.y + rect.h) {
						app.touch_held[event.tfinger.fingerId] = i;
						app.session->key(app.touch[i].key, true);
						app.input_nudge = true;
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
			// Not while a draft is up: the cards outrank the pause menu in
			// the draw order, so a pause taken here would be invisible - a
			// game frozen twice with nothing on screen saying so.
			if (app.offers.empty()) {
				app.paused = !app.paused;
			}
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
	// F11 toggles fullscreen - on Windows the compositor adds a frame of
	// latency to a mere window, and a fullscreen one gets the direct path.
	if (down && event.key.keysym.scancode == SDL_SCANCODE_F11 && !kMobile) {
		const bool full = (SDL_GetWindowFlags(app.window)
			& SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
		SDL_SetWindowFullscreen(app.window,
			full ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
		return;
	}
	// F3 toggles the frame diagnostics, so "it stutters" can arrive as
	// numbers. The tick histogram starts fresh each time it comes up.
	if (down && event.key.keysym.scancode == SDL_SCANCODE_F3) {
		app.show_frames = !app.show_frames;
		if (app.show_frames) {
			app.tick_hist[0] = app.tick_hist[1] = app.tick_hist[2] = 0;
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
		if (app.campaign_stage >= 0) {
			// A stage restarts as the stage, or the retry would silently
			// shed the recipe and play a plain game in its clothes.
			start_stage(app, app.campaign_stage);
		} else if (app.mode == 5) {
			start_versus(app, app.career_stage);
		} else {
			start_game(app, app.mode);
		}
		return;
	}
	// The draft has the keyboard while it is up: 1/2/3 takes a card
	// outright, the arrows walk the row and Enter or space takes the one
	// under the cursor. No confirm step - a pick every ten lines that costs
	// two presses would be two presses too many.
	if (down && app.screen == Screen::Game && !app.offers.empty()
		&& !app.editing && !ImGui::GetIO().WantCaptureKeyboard) {
		const int cards = static_cast<int>(app.offers.size());
		const SDL_Scancode code = event.key.keysym.scancode;
		if (code >= SDL_SCANCODE_1 && code < SDL_SCANCODE_1 + cards) {
			take_temper(app, code - SDL_SCANCODE_1);
		} else if (code == SDL_SCANCODE_LEFT) {
			app.offer_at = (app.offer_at + cards - 1) % cards;
		} else if (code == SDL_SCANCODE_RIGHT) {
			app.offer_at = (app.offer_at + 1) % cards;
		} else if (code == SDL_SCANCODE_RETURN
			|| code == SDL_SCANCODE_KP_ENTER || code == SDL_SCANCODE_SPACE) {
			take_temper(app, app.offer_at);
		}
		// Whatever else was pressed dies here: a game key that fell through
		// would queue in the session and land as a burst on the first
		// unfrozen frame, moving a piece the player never saw move.
		return;
	}
	if (app.screen == Screen::Game && !app.offers.empty() && down) {
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
		app.input_nudge = true;
	}
}

// --- The board and its trimmings, in plain rectangles. ---------------------

void spawn_sparks_at (App& app, float x, float y, SDL_Color color, int count,
	float kick);

// The soft sprite: white, fading to nothing at the rim, built by hand so
// the repository carries no image files.
SDL_Texture* make_glow (SDL_Renderer* renderer) {
	const int size = 128;
	SDL_Surface* face = SDL_CreateRGBSurfaceWithFormat(0, size, size, 32,
		SDL_PIXELFORMAT_RGBA32);
	if (face == nullptr) {
		return nullptr;
	}
	Uint32* pixels = static_cast<Uint32*>(face->pixels);
	const double middle = (size - 1) / 2.;
	for (int y = 0; y < size; ++y) {
		for (int x = 0; x < size; ++x) {
			const double dx = (x - middle) / middle;
			const double dy = (y - middle) / middle;
			const double r = std::sqrt(dx * dx + dy * dy);
			const double fade = r >= 1. ? 0. : (1. - r) * (1. - r);
			pixels[y * (face->pitch / 4) + x] = SDL_MapRGBA(face->format,
				255, 255, 255, static_cast<Uint8>(fade * 255.));
		}
	}
	SDL_Texture* made = SDL_CreateTextureFromSurface(renderer, face);
	SDL_FreeSurface(face);
	if (made != nullptr) {
		// Added rather than blended: light piles up the way light does.
		SDL_SetTextureBlendMode(made, SDL_BLENDMODE_ADD);
	}
	return made;
}

// A tongue of fire, as a strip of frames it can be animated through.
//
// Same idea as the glow above with one dimension of time on the end: value
// noise, warped by an envelope that is wide at the foot and narrow at the
// tip. The noise lattice wraps in y over exactly the distance the frames
// scroll, so the last frame runs back into the first and the flame never
// jumps. Nothing here is loaded from disk.
//
// Where it differs from the glow is that it is drawn as pixel art and not
// as a photograph of a fire: a small grid, a five-colour ramp with nothing
// in between, and hard edges. A soft flame looks like a smear the moment
// the game stops moving, and a paused screen is exactly when someone looks
// at it properly. So the sprite is tiny, the palette is banded on purpose,
// and it is scaled up with nearest-neighbour to whole pixels.
constexpr int kFlameFrames = 16;
constexpr int kFlameW = 38;
constexpr int kFlameH = 88;

// The fire ramp, coldest first. Index 0 is the outside air.
constexpr SDL_Color kFireRamp[6] = {
	{0, 0, 0, 0},
	{124, 26, 12, 255},
	{198, 58, 16, 255},
	{240, 118, 26, 255},
	{252, 188, 66, 255},
	{255, 244, 196, 255},
};

namespace {

// A hash on the lattice, wrapped in y so the strip loops.
double flame_lattice (int x, int y, int period, int seed) {
	y = ((y % period) + period) % period;
	unsigned h = static_cast<unsigned>(x) * 374761393u
		+ static_cast<unsigned>(y) * 668265263u
		+ static_cast<unsigned>(seed) * 2246822519u;
	h = (h ^ (h >> 13)) * 1274126177u;
	return ((h ^ (h >> 16)) & 0xffffu) / 65535.;
}

// Bilinear value noise off that lattice.
double flame_noise (double x, double y, int period, int seed) {
	const int x0 = static_cast<int>(std::floor(x));
	const int y0 = static_cast<int>(std::floor(y));
	const double fx = x - x0;
	const double fy = y - y0;
	// Smoothstep the interpolation or the lattice shows as diamonds.
	const double sx = fx * fx * (3. - 2. * fx);
	const double sy = fy * fy * (3. - 2. * fy);
	const double top = flame_lattice(x0, y0, period, seed) * (1. - sx)
		+ flame_lattice(x0 + 1, y0, period, seed) * sx;
	const double bottom = flame_lattice(x0, y0 + 1, period, seed) * (1. - sx)
		+ flame_lattice(x0 + 1, y0 + 1, period, seed) * sx;
	return top * (1. - sy) + bottom * sy;
}

} // namespace

SDL_Texture* make_flames (SDL_Renderer* renderer) {
	SDL_Surface* face = SDL_CreateRGBSurfaceWithFormat(0,
		kFlameW * kFlameFrames, kFlameH, 32, SDL_PIXELFORMAT_RGBA32);
	if (face == nullptr) {
		return nullptr;
	}
	Uint32* pixels = static_cast<Uint32*>(face->pixels);
	const int pitch = face->pitch / 4;
	// The lattice is coarse across and fine up, because fire stretches as
	// it rises. Three octaves is plenty here - the grid is only twenty-six
	// cells wide, and a fourth octave would land below one cell.
	constexpr double kAcross = 2.6;
	constexpr double kUp = 4.6;
	constexpr int kPeriod = 32;
	for (int frame = 0; frame < kFlameFrames; ++frame) {
		// One full lattice period over the whole strip: frame 15 hands back
		// to frame 0 with nothing to see at the seam.
		const double scroll = kPeriod * frame / static_cast<double>(kFlameFrames);
		for (int y = 0; y < kFlameH; ++y) {
			// foot is 1 along the bottom row, where the flame stands, and 0
			// at the tip. The surface's first row is the tip.
			const double foot = static_cast<double>(y) / (kFlameH - 1);
			// The whole tongue leans, and leans further the further it gets
			// from its base - which is what makes it lick rather than sit.
			const double sway = (flame_noise(foot * 3.,
				scroll + foot * 2., kPeriod, 91) - 0.5) * 1.1 * (1. - foot);
			for (int x = 0; x < kFlameW; ++x) {
				const double u = (x - (kFlameW - 1) / 2.) / ((kFlameW - 1) / 2.)
					- sway;
				// Wide at the foot, pinched at the tip.
				const double width = 0.26 + 0.74 * std::pow(foot, 0.7);
				const double across = 1. - (u / width) * (u / width);
				if (across <= 0.) {
					pixels[y * pitch + frame * kFlameW + x] = 0;
					continue;
				}
				// Nothing at all at the tip, full strength at the base.
				const double up = std::pow(foot, 0.5);
				double turbulence = 0.;
				double weight = 0.;
				double scale = 1.;
				for (int octave = 0; octave < 3; ++octave) {
					turbulence += flame_noise(
						(x / static_cast<double>(kFlameW)) * kAcross * scale,
						(y / static_cast<double>(kFlameH)) * kUp * scale + scroll * scale,
						kPeriod, 17 + octave) / scale;
					weight += 1. / scale;
					scale *= 2.;
				}
				turbulence /= weight;
				const double heat = std::clamp(
					across * up * (0.42 + 0.95 * turbulence) - 0.14, 0., 1.);
				if (heat <= 0.) {
					pixels[y * pitch + frame * kFlameW + x] = 0;
					continue;
				}
				// Straight onto one of five colours - no blend between
				// them. The banding is the whole point: a flame reads as
				// drawn rather than as blurred.
				const int band = std::clamp(
					static_cast<int>(heat * 5.6) + 1, 1, 5);
				const SDL_Color shade = kFireRamp[band];
				pixels[y * pitch + frame * kFlameW + x] = SDL_MapRGBA(
					face->format, shade.r, shade.g, shade.b, shade.a);
			}
		}
	}
	SDL_Texture* made = SDL_CreateTextureFromSurface(renderer, face);
	SDL_FreeSurface(face);
	if (made != nullptr) {
		// Blended rather than added: added light piles overlapping tongues
		// up into white and undoes the banding the palette exists for.
		SDL_SetTextureBlendMode(made, SDL_BLENDMODE_BLEND);
		// And scaled up in hard squares, which is what makes it pixel art
		// rather than a small picture of a fire.
		SDL_SetTextureScaleMode(made, SDL_ScaleModeNearest);
	}
	return made;
}

// Stamp the sprite over a rectangle, tinted and dimmed to taste.
void draw_glow (App& app, float x, float y, float w, float h,
		SDL_Color tint, double alpha) {
	if (app.glow == nullptr || alpha <= 0.) {
		return;
	}
	SDL_SetTextureColorMod(app.glow, tint.r, tint.g, tint.b);
	SDL_SetTextureAlphaMod(app.glow,
		static_cast<Uint8>(std::clamp(alpha, 0., 255.)));
	const SDL_Rect over{static_cast<int>(x - w / 2), static_cast<int>(y - h / 2),
		static_cast<int>(w), static_cast<int>(h)};
	SDL_RenderCopy(app.renderer, app.glow, nullptr, &over);
}

// Every centred panel is a plate off the same forge: dark iron, a bevel lit
// from above, rivets at the corners, a heat gradient banked up its lower
// half, and a rim that glows the way metal does when it has just come out
// of the fire. Called once, immediately after ImGui::Begin, so it lands
// under the window's own widgets.
void forge_panel (App& app) {
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetWindowPos();
	const ImVec2 size = ImGui::GetWindowSize();
	if (size.x <= 0.f || size.y <= 0.f) {
		return;
	}
	const ImVec2 low(at.x + size.x, at.y + size.y);
	const float round = ImGui::GetStyle().WindowRounding;
	const float inset = round;

	// The plate, over ImGui's flat window colour.
	draw->AddRectFilled(at, low, IM_COL32(24, 18, 13, 252), round);
	// Heat banked up from below - the plate sits over the fire, so its
	// lower half is the part that has been in it. Inset by the corner
	// radius because a gradient cannot follow the rounding.
	draw->AddRectFilledMultiColor(
		ImVec2(at.x + inset, at.y + size.y * 0.55f),
		ImVec2(low.x - inset, low.y - inset * 0.5f),
		IM_COL32(92, 38, 12, 0), IM_COL32(92, 38, 12, 0),
		IM_COL32(108, 44, 14, 150), IM_COL32(108, 44, 14, 150));

	// The bevel: lit along the top and the left, shadowed opposite.
	draw->AddLine(ImVec2(at.x + inset, at.y + 1.5f),
		ImVec2(low.x - inset, at.y + 1.5f), IM_COL32(96, 71, 52, 150), 1.5f);
	draw->AddLine(ImVec2(at.x + 1.5f, at.y + inset),
		ImVec2(at.x + 1.5f, low.y - inset), IM_COL32(84, 61, 44, 120), 1.5f);
	draw->AddLine(ImVec2(low.x - 1.5f, at.y + inset),
		ImVec2(low.x - 1.5f, low.y - inset), IM_COL32(8, 5, 3, 170), 1.5f);

	// The rim, pulsing between ember and gold, and hottest along the foot.
	const float beat = 0.5f + 0.5f * std::sin(app.backdrop_tick * 0.035f);
	const ImU32 rim = IM_COL32(255, static_cast<int>(118 + 56 * beat),
		static_cast<int>(44 + 32 * beat), 225);
	draw->AddRect(at, low, rim, round, 0, ui(2));
	// A softer second pass just outside it, so the rim reads as glowing
	// rather than as a border someone drew.
	draw->AddRect(ImVec2(at.x - ui(2), at.y - ui(2)),
		ImVec2(low.x + ui(2), low.y + ui(2)),
		IM_COL32(214, 92, 30, static_cast<int>(60 + 40 * beat)),
		round + ui(2), 0, ui(3));
	draw->AddLine(ImVec2(at.x + inset, low.y - 1.f),
		ImVec2(low.x - inset, low.y - 1.f),
		IM_COL32(255, 196, 96, static_cast<int>(170 + 60 * beat)), ui(2.5f));

	// Rivets, one at each corner, each a dark hole with a lit lip.
	const float dot = ui(3.2f);
	const float pad = round * 0.9f + ui(1);
	const ImVec2 studs[4] = {
		ImVec2(at.x + pad, at.y + pad), ImVec2(low.x - pad, at.y + pad),
		ImVec2(at.x + pad, low.y - pad), ImVec2(low.x - pad, low.y - pad)};
	for (const ImVec2& stud : studs) {
		draw->AddCircleFilled(stud, dot, IM_COL32(14, 10, 7, 255), 10);
		draw->AddCircleFilled(ImVec2(stud.x - dot * 0.28f, stud.y - dot * 0.28f),
			dot * 0.44f, IM_COL32(122, 92, 66, 220), 8);
	}
}

// A settled number between 0 and 1 for item `k`, so the drifting motes can
// each keep their own place and pace without a pool to store them in.
float drift_hash (int k, int salt) {
	unsigned h = static_cast<unsigned>(k) * 2654435761u
		+ static_cast<unsigned>(salt) * 40503u;
	h ^= h >> 15;
	h *= 2246822519u;
	h ^= h >> 13;
	return (h & 0xffffu) / 65535.f;
}

// How lit the ignition is: 1 the moment it fires, banking down as it runs
// out, 0 when it is not burning at all. Both halves of the Overdrive light
// read from this, so they rise and fall together.
float overdrive_lit (const App& app) {
	if (!app.session.has_value()) {
		return 0.f;
	}
	const Sim& sim = app.session->sim();
	if (!sim.overdrive()) {
		return 0.f;
	}
	const double span = std::max(1., sim.config().overdrive_secs * 50.);
	return 0.45f + 0.55f * static_cast<float>(
		std::clamp(sim.overdrive_left() / span, 0., 1.));
}

// Overdrive is light, not shapes. Behind the board: a sun the well stands
// in front of, and fine motes drifting up through the room. Both are the
// same soft falloff sprite every other glow in the game is made of, stamped
// once very big and many times very small - which is the whole point. The
// attempts before this drew *things*, tongues of flame and speed lines, and
// a drawn thing on a dark screen reads as a shape someone cut out. Light
// does not.
void draw_overdrive_bloom (App& app) {
	const float lit = overdrive_lit(app);
	if (lit <= 0.f) {
		return;
	}
	int screen_w = 0;
	int screen_h = 0;
	SDL_GetRendererOutputSize(app.renderer, &screen_w, &screen_h);
	const float cx = kBoardX + kBoardW * 0.5f;
	const float cy = kBoardY + kBoardH * 0.5f;
	const float tick = static_cast<float>(app.backdrop_tick);
	// A slow breath, so the light is never quite still.
	const float beat = 0.90f + 0.10f * std::sin(tick * 0.05f);

	// Two blooms, both kept close. The room must stay black: light spread
	// evenly over the whole screen does not read as a board that is glowing,
	// it reads as fog, and the contrast that makes the reference work is
	// exactly the dark around the light.
	draw_glow(app, cx, cy, screen_w * 1.5f, screen_h * 1.5f,
		{255, 168, 56, 255}, 15. * lit * beat);
	draw_glow(app, cx, cy, kBoardW * 2.3f, kBoardH * 1.35f,
		{255, 206, 102, 255}, 92. * lit * beat);

	// Motes drifting up through it. Small, soft and many - the reference is
	// dust in a light beam, not sparks off a fire.
	for (int k = 0; k < 54; ++k) {
		const float across = drift_hash(k, 1);
		const float pace = 0.00045f + 0.00115f * drift_hash(k, 3);
		const float rise = std::fmod(tick * pace + drift_hash(k, 2), 1.f);
		const float sway = std::sin(tick * 0.012f + k * 1.7f) * px(26);
		const float size = px(5) + px(14) * drift_hash(k, 4);
		// In at the floor, out at the ceiling, brightest in between.
		const float show = std::sin(rise * 3.14159265f);
		draw_glow(app, screen_w * across + sway, screen_h * (1.f - rise),
			size, size, {255, 232, 168, 255}, 150. * lit * show * show);
	}
}

// How much trouble this board is in, 0 to 1: a fuse nearly spent, garbage
// massing, a stack near the sky, the other board's Overdrive bearing down.
// The well's glow and the screen's vignette both read from it.
double danger_of (const Sim& sim) {
	if (!sim.config().fuse) {
		return 0.;
	}
	double danger = 0.;
	if (sim.fuse_total() > 0. && sim.piece_elapsed().has_value()) {
		danger = std::max(danger,
			(*sim.piece_elapsed() / sim.fuse_total() - 0.6) / 0.4);
	}
	danger = std::max(danger, sim.pending_garbage() / 8.);
	int top = kHeight;
	for (int y = 0; y < kHeight && top == kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (sim.board().at(x, y) >= 0) {
				top = y;
				break;
			}
		}
	}
	danger = std::max(danger, (12. - top) / 10.);
	if (sim.pressured()) {
		danger = std::max(danger, 0.55);
	}
	return std::clamp(danger, 0., 1.);
}

void fill (SDL_Renderer* renderer, int x, int y, int w, int h, SDL_Color c) {
	SDL_SetRenderDrawColor(renderer, c.r, c.g, c.b, c.a);
	const SDL_Rect rect{x, y, w, h};
	SDL_RenderFillRect(renderer, &rect);
}

// ...and the well itself lit like a filament: the rails either side and the
// lip above and below go white hot, each behind a wide soft bloom. Drawn
// after the board, so the frame stays crisp, and never inside it - the
// light frames the playfield, it does not sit on it.
void draw_overdrive_frame (App& app) {
	const float lit = overdrive_lit(app);
	if (lit <= 0.f) {
		return;
	}
	const float beat = 0.86f + 0.14f * std::sin(
		static_cast<float>(app.backdrop_tick) * 0.09f);
	const float glow = 215.f * lit * beat;
	const float cx = kBoardX + kBoardW * 0.5f;
	const float cy = kBoardY + kBoardH * 0.5f;
	const int bar = std::max(2, px(3));
	const int left = kBoardX - px(4);
	const int right = kBoardX + kBoardW + px(4);
	const int top = kBoardY - px(4);
	const int foot = kBoardY + kBoardH + px(4);
	const SDL_Color tint{255, 198, 84, 255};
	const SDL_Color core{255, 250, 224,
		static_cast<Uint8>(std::clamp(240.f * lit * beat, 0.f, 255.f))};

	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
	// The bloom behind each edge, then the filament itself on top of it.
	draw_glow(app, static_cast<float>(left), cy, px(46),
		kBoardH + px(70), tint, glow);
	draw_glow(app, static_cast<float>(right), cy, px(46),
		kBoardH + px(70), tint, glow);
	draw_glow(app, cx, static_cast<float>(top), kBoardW + px(80),
		px(74), tint, glow * 0.85);
	draw_glow(app, cx, static_cast<float>(foot), kBoardW + px(80),
		px(74), tint, glow * 0.85);
	fill(app.renderer, left - bar / 2, top, bar, foot - top, core);
	fill(app.renderer, right - bar / 2, top, bar, foot - top, core);
	fill(app.renderer, left, top - bar / 2, right - left, bar, core);
	fill(app.renderer, left, foot - bar / 2, right - left, bar, core);
}

// Garbage wears its own face: burnt slag, dark and cracked, so a row the
// other side sent never reads as one you built.
void draw_char_cell (SDL_Renderer* renderer, int x, int y, int size = kCell) {
	const int t = std::max(1, size / 8);
	fill(renderer, x + 1, y + 1, size - 2, size - 2, {58, 48, 44, 255});
	fill(renderer, x + 1, y + 1, size - 2, t, {84, 70, 62, 255});
	fill(renderer, x + 1, y + size - 1 - t, size - 2, t, {34, 27, 24, 255});
	// Two cracks, glowing faintly - the slag has not gone cold.
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	fill(renderer, x + size / 3, y + t + 1, std::max(1, t / 2),
		size - 2 * t - 2, {180, 74, 34, 120});
	fill(renderer, x + t + 1, y + size / 2, size / 3, std::max(1, t / 2),
		{180, 74, 34, 90});
}

void draw_cell (SDL_Renderer* renderer, int px, int py, SDL_Color c, int size = kCell) {
	// A block, not a swatch: the face, a lit lip on top and left, a shadow
	// at the foot and right - the cheap bevel that reads as depth at any
	// cell size. Ghost cells arrive with a low alpha and keep it.
	const auto scaled = [&c] (double factor, int lift) {
		return SDL_Color{
			static_cast<Uint8>(std::min(255., c.r * factor + lift)),
			static_cast<Uint8>(std::min(255., c.g * factor + lift)),
			static_cast<Uint8>(std::min(255., c.b * factor + lift)), c.a};
	};
	const int t = std::max(1, size / 8);
	fill(renderer, px + 1, py + 1, size - 2, size - 2, scaled(1.0, 0));
	const SDL_Color lit = scaled(1.25, 28);
	const SDL_Color shade = scaled(0.55, 0);
	fill(renderer, px + 1, py + 1, size - 2, t, lit);
	fill(renderer, px + 1, py + 1 + t, t, size - 2 - t, scaled(1.1, 12));
	fill(renderer, px + 1, py + size - 1 - t, size - 2, t, shade);
	fill(renderer, px + size - 1 - t, py + 1 + t, t, size - 2 - 2 * t,
		scaled(0.75, 0));
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

	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	// The halo: the well itself glows as the Flow gauge fills, so the room
	// warms towards an ignition before it happens.
	const double charge = sim.config().fuse ? sim.flow() / 100. : 0.;
	if (charge > 0.02) {
		const float beat = 0.85f + 0.15f * std::sin(sim.frame() * 0.12f);
		draw_glow(app, kBoardX + kBoardW / 2.f, kBoardY + kBoardH / 2.f,
			kBoardW * 2.4f, kBoardH * 1.5f, {255, 122, 44, 255},
			charge * beat * 150.);
	}
	fill(renderer, kBoardX - px(3), kBoardY - px(3), kBoardW + px(6), kBoardH + px(6), {58, 42, 30, 255});
	fill(renderer, kBoardX, kBoardY, kBoardW, kBoardH, {17, 12, 9, 255});
	// The fire is below: the well's floor smoulders, brighter the more
	// trouble the board is in.
	{
		const double danger = danger_of(sim);
		const int reach = 8;
		for (int y = kHeight - reach; y < kHeight; ++y) {
			const double depth = (y - (kHeight - reach) + 1.) / reach;
			fill(renderer, kBoardX, kBoardY + y * kCell, kBoardW, kCell,
				{168, 58, 20, static_cast<Uint8>(
					depth * depth * (10. + 26. * danger))});
		}
	}
	// A thin ember line inside the frame, the crucible's rim.
	SDL_SetRenderDrawColor(renderer, 122, 82, 50, 255);
	{
		const SDL_Rect rim{kBoardX - 1, kBoardY - 1, kBoardW + 2, kBoardH + 2};
		SDL_RenderDrawRect(renderer, &rim);
	}
	// The grid fades out towards the sky - the deep end of the well is
	// where the detail is - and the columns stay legible all the way up.
	for (int x = 1; x < kWidth; ++x) {
		SDL_SetRenderDrawColor(renderer, 46, 34, 25, 190);
		SDL_RenderDrawLine(renderer, kBoardX + x * kCell, kBoardY,
			kBoardX + x * kCell, kBoardY + kBoardH - 1);
	}
	for (int y = 1; y < kHeight; ++y) {
		const double depth = static_cast<double>(y) / kHeight;
		SDL_SetRenderDrawColor(renderer, 46, 34, 25,
			static_cast<Uint8>(40 + 150 * depth));
		SDL_RenderDrawLine(renderer, kBoardX, kBoardY + y * kCell,
			kBoardX + kBoardW - 1, kBoardY + y * kCell);
	}

	const Board& board = sim.board();
	for (int y = 0; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			const int form = board.at(x, y);
			if (form == GARBAGE) {
				draw_char_cell(renderer, kBoardX + x * kCell,
					kBoardY + y * kCell);
			} else if (form >= 0) {
				draw_cell(renderer, kBoardX + x * kCell, kBoardY + y * kCell,
					kFormColors[std::min(form, 7)]);
			}
		}
	}

	if (sim.entry() && sim.piece().form <= 6) {
		const Piece& piece = sim.piece();
		// Smooth motion: draw the piece part-way between its last two sim
		// steps. Only a plain step qualifies - same form and rotation, at
		// most one column across, zero or one row down. A rotation, spawn,
		// hard drop or teleport (ARR 0 to the wall, SDF 40 to the floor)
		// snaps, so the game keeps reading in cells.
		int ox = 0;
		int oy = 0;
		if (app.config.smooth && app.lerp_have && app.lerp_alpha < 1.f) {
			const Piece& was = app.lerp_prev;
			const int dx = piece.x - was.x;
			const int dy = piece.y - was.y;
			if (piece.form == was.form && piece.state == was.state
				&& dx >= -1 && dx <= 1 && (dy == 0 || dy == 1)) {
				const float back = 1.f - app.lerp_alpha;
				ox = -static_cast<int>(back * dx * kCell);
				oy = -static_cast<int>(back * dy * kCell);
			}
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		const Piece ghost = board.dropped(piece);
		if (ghost.y != piece.y) {
			// Outlined, not filled: a landing marker that never reads as
			// part of the stack it is standing on.
			const SDL_Color edge{kFormColors[piece.form].r,
				kFormColors[piece.form].g, kFormColors[piece.form].b, 190};
			const SDL_Color inner{kFormColors[piece.form].r,
				kFormColors[piece.form].g, kFormColors[piece.form].b, 28};
			const int t = std::max(1, kCell / 10);
			for (const Offset cell : cells_of(ghost)) {
				if (cell.y < 0) {
					continue;
				}
				// The ghost slides sideways with the piece but its row is the
				// landing, a fact, so y stays snapped.
				const int gx = kBoardX + cell.x * kCell + ox;
				const int gy = kBoardY + cell.y * kCell;
				fill(renderer, gx + 1, gy + 1, kCell - 2, kCell - 2, inner);
				fill(renderer, gx + 1, gy + 1, kCell - 2, t, edge);
				fill(renderer, gx + 1, gy + kCell - 1 - t, kCell - 2, t, edge);
				fill(renderer, gx + 1, gy + 1, t, kCell - 2, edge);
				fill(renderer, gx + kCell - 1 - t, gy + 1, t, kCell - 2, edge);
			}
		}
		for (const Offset cell : cells_of(piece)) {
			if (cell.y >= 0) {
				draw_cell(renderer, kBoardX + cell.x * kCell + ox,
					kBoardY + cell.y * kCell + oy, kFormColors[piece.form]);
			}
		}
		// The burn made visible: past sixty percent of the fuse the piece
		// itself smoulders - a pulsing overlay, white-hot when the other
		// board's Overdrive is bearing down - and past eighty it sheds
		// embers where it stands.
		if (sim.config().fuse && sim.fuse_total() > 0.
			&& sim.piece_elapsed().has_value()) {
			const double frac = *sim.piece_elapsed() / sim.fuse_total();
			if (frac > 0.6) {
				const double heat = std::min(1., (frac - 0.6) / 0.4);
				const float pulse
					= 0.7f + 0.3f * std::sin(sim.frame() * 0.45f);
				SDL_Color glow = sim.pressured()
					? SDL_Color{255, 236, 210, 0}
					: SDL_Color{255, 96, 40, 0};
				glow.a = static_cast<Uint8>(120. * heat * pulse);
				SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
				for (const Offset cell : cells_of(piece)) {
					if (cell.y >= 0) {
						fill(renderer, kBoardX + cell.x * kCell + ox + 1,
							kBoardY + cell.y * kCell + oy + 1,
							kCell - 2, kCell - 2, glow);
					}
				}
				if (frac > 0.8 && sim.frame() % 4 == 0) {
					const auto cells = cells_of(piece);
					const Offset cell = cells[app.seeds() % kCells];
					spawn_sparks_at(app,
						kBoardX + (cell.x + 0.5f) * kCell + ox,
						kBoardY + (cell.y + 0.5f) * kCell + oy,
						{255, 150, 70, 255}, 1, 1.6f);
				}
			}
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	}

	// The hold box and the coming pieces.
	// A slot in the furnace wall: sunk face, lit top edge, ember hairline.
	const auto draw_slot = [renderer] (int x, int y, int w, int h,
			bool spent) {
		fill(renderer, x - px(2), y - px(2), w + px(4), h + px(4),
			{54, 39, 28, 255});
		fill(renderer, x, y, w, h, spent
			? SDL_Color{22, 17, 14, 255} : SDL_Color{30, 22, 17, 255});
		fill(renderer, x, y, w, std::max(1, px(2)), {70, 51, 37, 255});
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		fill(renderer, x, y + h - std::max(1, px(2)), w,
			std::max(1, px(2)), {12, 8, 6, 200});
		if (spent) {
			// Hatched out: the hold has already been spent on this piece.
			for (int i = -h; i < w; i += px(9)) {
				for (int s = 0; s < px(2); ++s) {
					SDL_SetRenderDrawColor(renderer, 96, 70, 50, 90);
					SDL_RenderDrawLine(renderer, x + i + s, y + h - 1,
						x + i + h + s, y);
				}
			}
		}
	};
	draw_slot(kBoardX - px(122), kBoardY, px(104), px(86),
		sim.hold_locked());
	draw_preview(renderer, sim.stored(), kBoardX - px(122) + px(16), kBoardY + px(12), px(18));
	const auto& queue = sim.queue();
	for (int slot = 0; slot < kPreviews
		&& slot < static_cast<int>(queue.size()); ++slot) {
		// The queue recedes: the next piece is full size, the ones behind
		// it smaller, so what matters most reads first.
		const int shrink = kPortrait ? 0 : std::min(slot, 3) * px(6);
		const int w = px(104) - shrink;
		const int h = px(86) - shrink;
		const int x = kBoardX + kBoardW + px(18) + shrink / 2;
		const int y = kBoardY + slot * px(92) + shrink / 2;
		draw_slot(x, y, w, h, false);
		draw_preview(renderer, queue[slot], x + px(16) - shrink / 2,
			y + px(12), px(18) - shrink / 6);
	}
	// The furnace rail: a riveted pillar down the well's right flank. There
	// is no matching one on the left because the Flow gauge stands there and
	// is that pillar - anywhere a decorative twin would fit on that side is
	// already inside the hold box.
	if (!kPortrait) {
		const int side = kBoardX + kBoardW + px(8);
		fill(renderer, side, kBoardY - px(6), px(8),
			kBoardH + px(12), {44, 32, 23, 255});
		fill(renderer, side, kBoardY - px(6), std::max(1, px(2)),
			kBoardH + px(12), {68, 49, 35, 255});
		for (int at = px(14); at < kBoardH; at += px(64)) {
			fill(renderer, side + px(2), kBoardY + at, px(4), px(4),
				{96, 68, 46, 255});
		}
	}

	// The fuse wick (or, on trainer rules, the flat forced-drop meter):
	// how much of the piece's stay is left. Frozen solid cyan under
	// Overdrive - the one moment the wick stops burning.
	const bool fused = sim.config().fuse;
	const double limit = fused ? sim.fuse_total() : app.config.forced_delay;
	if (limit > 0.) {
		fill(renderer, kBoardX, kBoardY + kBoardH + px(10), kBoardW, px(8), {36, 27, 20, 255});
		const auto elapsed = sim.piece_elapsed();
		if (elapsed.has_value()) {
			const double part = std::min(1.0, *elapsed / limit);
			SDL_Color wick{static_cast<Uint8>(90 + 165 * part),
				static_cast<Uint8>(200 - 140 * part), 80, 255};
			if (fused && sim.pressured()) {
				wick = {255, 236, 210, 255};
			}
			if (fused && sim.overdrive()) {
				wick = {90, 220, 235, 255};
			}
			fill(renderer, kBoardX, kBoardY + kBoardH + px(10),
				static_cast<int>(kBoardW * (1.0 - part)), px(8), wick);
		}
	}

	// The Flow gauge, climbing the board's left flank. It stands well clear
	// of the well: the board's own edge is where Overdrive puts its lit
	// frame, and a gauge sitting in that is a gauge nobody can read.
	// Overdrive fills the rail solid, which is the point - the gauge is
	// spent and the ignition is what it bought.
	if (fused) {
		const int rail_x = kBoardX - px(38);
		const int rail_y = kBoardY + px(120);
		const int rail_h = kBoardH - px(130);
		fill(renderer, rail_x, rail_y, px(12), rail_h, {36, 27, 20, 255});
		fill(renderer, rail_x, rail_y, std::max(1, px(2)), rail_h,
			{68, 49, 35, 255});
		const bool burning = sim.overdrive();
		const int charge = burning ? rail_h
			: static_cast<int>(rail_h * (sim.flow() / 100.));
		if (charge > 0) {
			const SDL_Color glow = burning
				? SDL_Color{255, 214, 96, 255} : SDL_Color{255, 138, 58, 255};
			fill(renderer, rail_x, rail_y + rail_h - charge, px(12), charge,
				glow);
		}
	}
}

// --- The ImGui layers: labels, stat panels, menus. -------------------------

void draw_label (const char* text, float x, float y, ImU32 color = IM_COL32(176, 158, 140, 255)) {
	ImGui::GetBackgroundDrawList()->AddText(ImVec2(x, y), color, text);
}

// How hot the room is: the Flow gauge, the trouble the board is in, and
// whether Overdrive is lit. The backdrop paints from it and the mixer plays
// from it, so what is on the screen and what is in the speakers can never
// drift apart - they read the same board at the same moment.
struct Room {
	double heat = 0.;      // Flow, 0 to 1.
	double danger = 0.;    // Fuse spent, garbage massing, stack near the sky.
	double glare = 0.;     // heat with Overdrive banked on top, 0 to 1.4.
	bool burning = false;  // Overdrive.
	bool playing = false;  // A live board, as opposed to a menu.
};

Room room_of (App& app) {
	Room room;
	room.playing = app.screen == Screen::Game && app.session.has_value();
	if (room.playing) {
		const Sim& sim = app.session->sim();
		room.heat = sim.config().fuse ? sim.flow() / 100. : 0.;
		room.danger = danger_of(sim);
		room.burning = sim.overdrive();
	}
	room.glare = std::clamp(room.heat + (room.burning ? 0.6 : 0.), 0., 1.4);
	return room;
}

// The backdrop: a warm vertical gradient - lit faintly from above, the
// way a room lit by a fire is - with slow embers drifting up behind the
// menu screens. The game screens keep the gradient and skip the embers;
// a fight needs the air still.
void draw_backdrop (App& app) {
	int w = 0;
	int h = 0;
	SDL_GetRendererOutputSize(app.renderer, &w, &h);
	const float t = ++app.backdrop_tick * 0.01f;
	const int bands = 12;
	for (int i = 0; i < bands; ++i) {
		const double part = static_cast<double>(i) / (bands - 1);
		fill(app.renderer, 0, h * i / bands, w, h / bands + 1,
			{static_cast<Uint8>(26 - 14 * part),
			 static_cast<Uint8>(18 - 9 * part),
			 static_cast<Uint8>(13 - 6 * part), 255});
	}
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);

	// What the room is doing depends on what the board is doing.
	const Room room = room_of(app);
	const bool playing = room.playing;
	const double heat = room.heat;
	const double danger = room.danger;
	const bool burning = room.burning;
	const double glare = room.glare;

	// The fire the whole room stands over: a molten horizon along the
	// bottom, banked up brighter as the board gets into trouble.
	{
		const double lit = 34. + 96. * danger + 70. * glare;
		draw_glow(app, w / 2.f, static_cast<float>(h),
			w * 1.6f, std::max(static_cast<float>(px(150)), h / 2.4f),
			{206, 74, 26, 255}, lit);
		draw_glow(app, w / 2.f, h + px(6), w * 1.1f, px(120),
			{255, 150, 66, 255}, 30. + 70. * danger + 46. * glare);
	}

	// Heat shafts rising off it: soft columns that lean as they climb and
	// brighten as the Flow gauge fills. They stand behind the board, so
	// what shows is the room around it - and in a game they are aimed at
	// the margins either side, where there is room to be seen.
	if (playing || app.screen == Screen::Menu) {
		const int shafts = 6;
		for (int s = 0; s < shafts; ++s) {
			const float phase = t * (0.55f + 0.11f * s) + s * 1.7f;
			float base = w * (0.08f + 0.185f * s);
			if (playing && kBoardX > px(120)) {
				// Three down each margin rather than six across the board.
				const float left = kBoardX * (0.18f + 0.28f * (s % 3));
				const float right = kBoardX + kBoardW
					+ (w - kBoardX - kBoardW) * (0.30f + 0.26f * (s % 3));
				base = s < 3 ? left : right;
			}
			const float wide = px(150) + px(52) * std::sin(phase * 0.7f);
			const float tall = h * (0.72f + 0.10f * std::sin(phase));
			const float lean = std::sin(phase) * px(30);
			draw_glow(app, base + lean, h - tall * 0.34f, wide, tall,
				{255, 132, 54, 255}, 20. + 46. * glare + 14. * danger);
		}
	}

	// Smoke, catching the light on its way past.
	if (playing) {
		for (int band = 0; band < 3; ++band) {
			const float span = static_cast<float>(w + px(700));
			const float drift = std::fmod(
				t * (7.f + 3.f * band) + band * 260.f, span) - px(350);
			draw_glow(app, drift, h / 6.f + band * h / 6.5f,
				px(520), px(150), {166, 92, 52, 255}, 9. + 16. * glare);
		}
	}

	// The embers. On the menus they drift up the whole screen; in a game
	// they keep to the margins either side of the board, so nothing ever
	// crosses the piece you are placing.
	const int margin = kBoardX - px(46);
	const bool roomy = margin > px(70);
	const int rate = playing ? (burning ? 3 : (heat > 0.4 ? 6 : 11)) : 5;
	if ((!playing || roomy) && app.seeds() % rate == 0) {
		App::Spark& born = app.embers[app.ember_at];
		app.ember_at = (app.ember_at + 1) % app.embers.size();
		if (playing) {
			const int right = kBoardX + kBoardW + px(46);
			born.x = app.seeds() % 2 == 0
				? static_cast<float>(app.seeds() % std::max(1, margin))
				: static_cast<float>(right + app.seeds()
					% std::max(1, w - right));
		} else {
			born.x = static_cast<float>(app.seeds() % std::max(1, w));
		}
		born.y = static_cast<float>(h + px(4));
		born.vx = ((app.seeds() % 100) - 50) / 220.f;
		born.vy = (burning ? -0.7f : -0.4f)
			- (app.seeds() % 100) / (burning ? 90.f : 130.f);
		born.life = 240 + static_cast<int>(app.seeds() % 240);
		born.color = burning
			? SDL_Color{255, static_cast<Uint8>(180 + app.seeds() % 60),
				90, 255}
			: SDL_Color{255, static_cast<Uint8>(120 + app.seeds() % 80),
				50, 255};
	}
	const int cap = playing ? 70 : 90;
	for (App::Spark& ember : app.embers) {
		if (ember.life <= 0) {
			continue;
		}
		--ember.life;
		ember.x += ember.vx + std::sin(ember.life * 0.05f) * 0.3f;
		ember.y += ember.vy;
		draw_glow(app, ember.x, ember.y, px(14), px(14), ember.color,
			std::min(cap, ember.life / 3));
	}
}

// Sparks loosed from one point; the pool recycles its oldest embers.
void spawn_sparks_at (App& app, float x, float y, SDL_Color color, int count,
		float kick) {
	for (int i = 0; i < count; ++i) {
		App::Spark& spark = app.sparks[app.spark_at];
		app.spark_at = (app.spark_at + 1) % app.sparks.size();
		spark.x = x;
		spark.y = y;
		const float angle = (app.seeds() % 628) / 100.f;
		const float speed = kick * (0.5f + (app.seeds() % 100) / 100.f);
		spark.vx = std::cos(angle) * speed;
		spark.vy = std::sin(angle) * speed - kick * 0.6f;
		spark.life = 18 + static_cast<int>(app.seeds() % 16);
		spark.color = color;
	}
}

// A burst of sparks from the cells of the piece that just locked.
void spawn_sparks (App& app, SDL_Color color, int per_cell, float kick) {
	if (!app.session.has_value() || app.session->sim().locked().empty()) {
		return;
	}
	const Locked& lock = app.session->sim().locked().back();
	const Piece piece{lock.form, lock.state, lock.x, lock.y};
	for (const Offset cell : cells_of(piece)) {
		spawn_sparks_at(app, kBoardX + (cell.x + 0.5f) * kCell,
			kBoardY + (cell.y + 0.5f) * kCell, color, per_cell, kick);
	}
}

// The cues' visible half: what the ear hears, the eye sees.
void juice_cue (App& app, const std::string& cue) {
	if (cue == "clear") {
		spawn_sparks(app, {255, 150, 70, 255}, 3, 2.2f);
	} else if (cue == "tetris") {
		spawn_sparks(app, {255, 214, 96, 255}, 6, 3.2f);
		app.shake_until = app.session->sim().frame() + 8;
	} else if (cue == "tspin") {
		spawn_sparks(app, {200, 130, 255, 255}, 5, 2.8f);
		app.shake_until = app.session->sim().frame() + 6;
	} else if (cue == "perfect") {
		spawn_sparks(app, {255, 255, 255, 255}, 10, 4.0f);
		app.shake_until = app.session->sim().frame() + 10;
	} else if (cue == "overdrive") {
		spawn_sparks(app, {255, 214, 96, 255}, 8, 3.6f);
		app.shake_until = app.session->sim().frame() + 10;
	} else if (cue == "burn") {
		spawn_sparks(app, {255, 140, 60, 255}, 4, 2.6f);
	}
}

// Step and draw the pool: tiny embers under gravity, faded out by life.
void draw_sparks (App& app) {
	for (App::Spark& spark : app.sparks) {
		if (spark.life <= 0) {
			continue;
		}
		--spark.life;
		spark.x += spark.vx;
		spark.y += spark.vy;
		spark.vy += 0.18f;
		draw_glow(app, spark.x, spark.y, px(16), px(16), spark.color,
			std::min(230, spark.life * 15));
	}
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
		kWidth * cell + px(4), kHeight * cell + px(4), {58, 42, 30, 255});
	// The bot's Overdrive shows the way the player's does, scaled down:
	// the board rimmed in gold while it burns.
	if (theirs.overdrive()) {
		const SDL_Color rim{255, 214, 96, 255};
		const int wide = kWidth * cell;
		const int tall = kHeight * cell;
		fill(renderer, left - px(3), top - px(3), wide + px(6), px(3), rim);
		fill(renderer, left - px(3), top + tall, wide + px(6), px(3), rim);
		fill(renderer, left - px(3), top, px(3), tall, rim);
		fill(renderer, left + wide, top, px(3), tall, rim);
	}
	fill(renderer, left, top, kWidth * cell, kHeight * cell, {17, 12, 9, 255});
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
	draw_label("BOT", static_cast<float>(left), top - ui(22),
		theirs.overdrive() ? IM_COL32(255, 214, 96, 255)
			: IM_COL32(176, 158, 140, 255));
	// What the bot has drafted, under its board - the same build line the
	// player's pause screen shows, because an opponent's tempers are half
	// of reading the fight.
	if (!match.bot_tempers.empty()) {
		draw_label(temper_line(match.bot_tempers).c_str(),
			static_cast<float>(left), top + kHeight * cell + px(6),
			IM_COL32(176, 158, 140, 255));
	}
	// The bot's Flow rail on its board's right flank - watching the gauge
	// creep up is the warning the ignition deserves.
	if (theirs.config().fuse) {
		const int rail_x = left + kWidth * cell + px(4);
		const int rail_h = kHeight * cell;
		fill(renderer, rail_x, top, px(4), rail_h, {36, 27, 20, 255});
		const bool burning = theirs.overdrive();
		const int charge = burning ? rail_h
			: static_cast<int>(rail_h * (theirs.flow() / 100.));
		if (charge > 0) {
			fill(renderer, rail_x, top + rail_h - charge, px(4), charge,
				burning ? SDL_Color{255, 214, 96, 255}
					: SDL_Color{255, 138, 58, 255});
		}
	}
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
		ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
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
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	const ImVec2 at(kBoardX + (kBoardW - extent.x) / 2,
		kBoardY + kBoardH / 2.f - font->FontSize);
	// A ring winding in as the second runs out, then the numeral over it.
	const ImVec2 middle(kBoardX + kBoardW / 2.f,
		at.y + font->FontSize * 0.5f);
	const float second = (app.countdown % 50) / 50.f;
	draw->AddCircle(middle, font->FontSize * 1.1f,
		IM_COL32(255, 138, 58, 70), 48, ui(3));
	draw->PathArcTo(middle, font->FontSize * 1.1f, -1.5708f,
		-1.5708f + 6.2832f * second, 48);
	draw->PathStroke(IM_COL32(255, 214, 96, 220), 0, ui(4));
	draw->AddText(font, font->FontSize,
		ImVec2(at.x + ui(3), at.y + ui(3)), IM_COL32(40, 16, 6, 200), text);
	draw->AddText(font, font->FontSize, at,
		IM_COL32(255, 210, 74, 255), text);
}

// The F3 overlay: what the render loop has actually been doing, as numbers,
// so "it stutters" can arrive as a report someone can act on. On a healthy
// vsync-off desktop the frame time sits near a millisecond and the tick
// histogram is almost all 0s and 1s; a fat 2+ column means renders are
// arriving late and carrying bunched sim steps - the judder itself.
void draw_frame_stats (App& app) {
	float worst = 0.f;
	float sum = 0.f;
	int have = 0;
	for (const float ms : app.frame_ms) {
		if (ms > 0.f) {
			sum += ms;
			worst = std::max(worst, ms);
			++have;
		}
	}
	const float mean = have > 0 ? sum / have : 0.f;
	const long total = static_cast<long>(app.tick_hist[0])
		+ app.tick_hist[1] + app.tick_hist[2];
	ImGui::SetNextWindowPos(ImVec2(static_cast<float>(ui(8)),
		static_cast<float>(ui(8))));
	ImGui::SetNextWindowBgAlpha(0.65f);
	ImGui::Begin("##framestats", nullptr, ImGuiWindowFlags_NoDecoration
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs
		| ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("frame %.2f ms avg, %.1f worst (%.0f fps)",
		mean, worst, mean > 0.f ? 1000.f / mean : 0.f);
	if (total > 0) {
		ImGui::Text("sim ticks per frame  0: %d%%  1: %d%%  2+: %d%%",
			static_cast<int>(app.tick_hist[0] * 100 / total),
			static_cast<int>(app.tick_hist[1] * 100 / total),
			static_cast<int>(app.tick_hist[2] * 100 / total));
	}
	ImGui::Text("smooth %s   vsync %s", app.config.smooth ? "on" : "off",
		(kMobile || !app.config.lowlatency) ? "on" : "off");
	ImGui::End();
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
		draw->AddRectFilled(a, b, held ? IM_COL32(255, 138, 58, 70)
			: IM_COL32(255, 255, 255, 22), ui(12));
		draw->AddRect(a, b, IM_COL32(176, 158, 140, 90), ui(12));
		ImFont* font = app.fonts.head;
		const ImVec2 extent = font->CalcTextSizeA(
			font->FontSize, FLT_MAX, 0.f, button.label);
		draw->AddText(font, font->FontSize,
			ImVec2(a.x + (button.rect.w - extent.x) / 2,
				a.y + (button.rect.h - extent.y) / 2),
			IM_COL32(235, 222, 205, 210), button.label);
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
			app.editing ? ImVec4(0.30f, 0.21f, 0.13f, 0.9f) : ImVec4(0.10f, 0.075f, 0.055f, 0.65f));
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
		reset_effects(app);
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
	forge_panel(app);
	if (ImGui::BeginTabBar("settings", ImGuiTabBarFlags_None)) {
		if (ImGui::BeginTabItem("Handling")) {
			ImGui::Spacing();
			// The three sets, first, because the numbers below only mean
			// something to someone who already knows what they want.
			for (const Handling set : {Handling::Standard, Handling::Instant,
				Handling::Trainer}) {
				if (set != Handling::Standard) {
					ImGui::SameLine();
				}
				if (ImGui::Button(handling_name(set), ImVec2(ui(120), 0))) {
					apply_handling(app.config, set);
				}
				if (ImGui::IsItemHovered()) {
					ImGui::SetTooltip("%s", handling_note(set));
				}
			}
			ImGui::Spacing();
			// The sliders give up a third of their width to their labels:
			// these are the longest in the settings screen, and a label that
			// runs off the panel is worse than a shorter bar.
			ImGui::PushItemWidth(ui(230));
			// AlwaysClamp: ctrl-click turns a slider into a raw input box,
			// and an unclamped sdf of 0 would divide the gravity by zero.
			ImGui::SliderInt("DAS (ms)", &app.config.das, 0, 330,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("ARR (ms, 0 = instant)", &app.config.arr, 0, 83,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("DCD (ms)", &app.config.dcd, 0, 330,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("SDF (x, 40 = instant)", &app.config.sdf, 5, 40,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::SliderInt("ARE (ms)", &app.config.are, 0, 500,
				"%d", ImGuiSliderFlags_AlwaysClamp);
			ImGui::Checkbox("Clear delay", &app.config.clear_delay);
			ImGui::SameLine();
			ImGui::TextDisabled("%s", app.config.clear_delay
				? "clears animate; the board is frozen while they do"
				: "clears resolve on the lock frame");
			ImGui::Spacing();
			// What those numbers cost, in the units they are felt in. The
			// engine's frame is 20ms, so every one of these is a whole
			// number of frames whatever the sliders say.
			{
				const int das_f = py_round(app.config.das / 20.);
				const int arr_f = py_round(app.config.arr / 20.);
				// The press itself moves a column and arms DAS; the auto-shift
				// lands on the frame DAS expires, and at ARR 0 that one frame
				// covers the remaining eight columns rather than one of them.
				const int cross = das_f + 1 + (arr_f < 1 ? 0 : arr_f * 8);
				const int soft = app.config.sdf >= 40
					? 1 : 20 * std::max(1, py_round(30. / app.config.sdf));
				const int quad = app.config.clear_delay ? 29 : 1;
				ImGui::TextDisabled(
					"To the wall ~%dms - to the floor ~%dms - quad freeze %dms",
					cross * 20, soft * 20, quad * 20);
			}
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
			ImGui::PopItemWidth();
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
			// The clear *delay* moved to Handling: it is a rule on paper and
			// half a second of frozen board in the hand.
			const char* finesse_rules[] = {
				"Off", "Count faults", "Retry on fault"};
			ImGui::Combo("Finesse", &app.config.finesse_rule, finesse_rules, 3);
			ImGui::Checkbox("Wall kicks", &app.config.kicks);
			ImGui::Checkbox("Screen shake", &app.config.shake);
			if (!kMobile && ImGui::Checkbox("Low-latency rendering",
				&app.config.lowlatency)) {
				SDL_RenderSetVSync(app.renderer,
					app.config.lowlatency ? 0 : 1);
			}
			ImGui::SameLine();
			ImGui::TextDisabled("%s", kMobile ? ""
				: "vsync off; a frame or two less input lag");
			ImGui::Checkbox("Smooth motion", &app.config.smooth);
			ImGui::SameLine();
			ImGui::TextDisabled("draw the piece between engine steps");
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
			const char* const tracks[] = {"Forge", "Classic", "Off"};
			if (ImGui::Combo("Track", &app.config.music_mode, tracks, 3)) {
				app.audio.set_music_mode(
					static_cast<Audio::Music>(app.config.music_mode));
			}
			ImGui::TextDisabled("%s", "Forge is generated as you play");
			ImGui::Spacing();
			if (ImGui::Checkbox("Furnace ambience", &app.config.ambience)) {
				app.audio.set_ambience(app.config.ambience);
			}
			ImGui::TextDisabled("%s", "the room's roar, under Music");
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
	ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f), "%s", guess.rank);
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
	forge_panel(app);
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
	forge_panel(app);
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
		if (!kMobile) {
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImVec4(1.f, 0.88f, 0.5f, 1.f), "F11");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("Fullscreen (less latency on Windows)");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::TextColored(ImVec4(1.f, 0.88f, 0.5f, 1.f), "F3");
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted("Frame diagnostics overlay");
		}
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
			"The Flow rail climbs on quality: spins, quads, back-to-backs,\n"
			"combos and perfect clears fill it - haste alone barely moves\n"
			"it, though a lock inside the Flash window adds a little. Burn\n"
			"a fuse to the end and it drains.");
		ImGui::TextUnformatted(
			"A full rail ignites Overdrive: the fuse freezes, everything\n"
			"you send is multiplied, and every clear also burns a garbage\n"
			"row off your own floor - until it gutters out.");
		ImGui::TextUnformatted("");
		ImGui::TextUnformatted("The draft");
		ImGui::TextUnformatted(
			"Every ten lines is a heat. The forge tightens and offers three\n"
			"tempers; take one - the board waits while you choose. Fuel\n"
			"survives, Flow presses, Risk trades, Rule rewrites. A duel\n"
			"never stops: no drafts there - the bot brings a forged blade\n"
			"of its rank instead.");
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
	fill(renderer, left - px(3), top - px(3), w + px(6), h + px(6), {58, 42, 30, 255});
	fill(renderer, left, top, w, h, {17, 12, 9, 255});
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
	fill(app.renderer, kBoardX - px(122), kBoardY, px(104), px(86), {30, 22, 17, 255});
	draw_preview(app.renderer, place.stored,
		kBoardX - px(122) + px(16), kBoardY + px(12), px(18));
	for (size_t slot = 0; slot < place.queue.size() && slot < 3; ++slot) {
		fill(app.renderer, kBoardX + kBoardW + px(18),
			kBoardY + static_cast<int>(slot) * px(92), px(104), px(86),
			{30, 22, 17, 255});
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
	forge_panel(app);
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
	forge_panel(app);
	// The variant's tables first, the trainer's three behind them -
	// different files, different games, one screen. The variant's count is
	// the header's, so adding a mode adds a page here without a second
	// number to keep in step.
	static const char* kPages[] = {"Ignition", "Blaze", "Inferno", "Meltdown",
		"Bunker", "Duel", "Tempering", "Arcade", "Timed", "Free"};
	constexpr int kPageCount
		= static_cast<int>(sizeof kPages / sizeof kPages[0]);
	for (int page = 0; page < kPageCount; ++page) {
		if (page > 0 && page != hiscore::kFuseTables) {
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
	const hiscore::Table& table = app.score_page < hiscore::kFuseTables
		? fuse[app.score_page]
		: plain[app.score_page - hiscore::kFuseTables];
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
		IM_COL32(30, 22, 17, 255), ui(6));
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
			IM_COL32(255, 138, 58, 90), std::max(1.f, ui(1)));
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
			draw->AddLine(last, point, IM_COL32(255, 138, 58, 255),
				std::max(1.f, ui(2)));
		}
		last = point;
		started = true;
	}
	char text[48];
	std::snprintf(text, sizeof text, "%.1f", hi - pad);
	draw->AddText(ImVec2(origin.x + ui(6), origin.y + ui(2)),
		IM_COL32(176, 158, 140, 200), text);
	std::snprintf(text, sizeof text, "%.1f", lo + pad);
	draw->AddText(ImVec2(origin.x + ui(6), origin.y + height - ui(18)),
		IM_COL32(176, 158, 140, 200), text);
	std::snprintf(text, sizeof text, "%.1f",
		rolling / std::max<size_t>(1, window.size()));
	draw->AddText(ImVec2(origin.x + width - ui(56), origin.y + ui(2)),
		IM_COL32(255, 138, 58, 255), text);
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
	forge_panel(app);

	// The mode filter every tab reads.
	// Each filter matches its mode family under either ruleset's key - the
	// variant name and the trainer name are the same family of game.
	static const char* kFilters[] = {"All", "Ignition", "Blaze", "Inferno",
		"Meltdown", "Bunker", "Duel", "Tempering"};
	// Both spellings of each mode: a history file may hold games recorded
	// before the modes were renamed. Tempering has only ever had one name.
	static const char* kOld[] = {"", "free", "timed", "arcade", "cheese_race",
		"cheese_survival", "versus", "temper"};
	static const char* kNew[] = {"", "ignition", "blaze", "inferno",
		"meltdown", "bunker", "duel", "temper"};
	static int filter = 0;
	ImGui::SetNextItemWidth(ui(160));
	ImGui::Combo("Mode", &filter, kFilters,
		static_cast<int>(sizeof kFilters / sizeof kFilters[0]));
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

// The clear, made an event: every row that is full while the clearer is
// running goes white-hot, throws embers off both ends, and keeps burning
// for a few frames after the row itself is gone.
void light_burn_rows (App& app) {
	const Sim& sim = app.session->sim();
	if (!sim.clearing() || sim.locked().empty()) {
		return;
	}
	// Once per lock: the rows are still standing while the clearer runs.
	const long lock_frame = sim.locked().back().frame;
	if (lock_frame == app.burn_seen_lock) {
		return;
	}
	app.burn_seen_lock = lock_frame;
	for (int y = 0; y < kHeight; ++y) {
		bool full = true;
		for (int x = 0; x < kWidth; ++x) {
			if (sim.board().at(x, y) < 0) {
				full = false;
				break;
			}
		}
		if (!full) {
			continue;
		}
		App::BurnRow& row = app.burn_rows[app.burn_at];
		app.burn_at = (app.burn_at + 1) % app.burn_rows.size();
		row.row = y;
		row.life = 14;
		spawn_sparks_at(app, static_cast<float>(kBoardX),
			kBoardY + (y + 0.5f) * kCell, {255, 236, 190, 255}, 3, 2.4f);
		spawn_sparks_at(app, static_cast<float>(kBoardX + kBoardW),
			kBoardY + (y + 0.5f) * kCell, {255, 236, 190, 255}, 3, 2.4f);
	}
}

void draw_burn_rows (App& app) {
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
	for (App::BurnRow& row : app.burn_rows) {
		if (row.life <= 0) {
			continue;
		}
		--row.life;
		// White-hot at the start, cooling to ember, and spreading out from
		// the middle of the row as it goes.
		const double part = row.life / 14.;
		const int reach = static_cast<int>(kBoardW * (1.2 - part) / 2.);
		const int wide = std::min(kBoardW, std::max(kCell, reach * 2));
		fill(app.renderer, kBoardX + (kBoardW - wide) / 2,
			kBoardY + row.row * kCell, wide, kCell,
			{255, static_cast<Uint8>(150 + 100 * part),
			 static_cast<Uint8>(60 + 140 * part),
			 static_cast<Uint8>(230 * part)});
	}
}

// The heat closing in: red glow at the screen's edges, driven by the
// worst of the dangers - a fuse nearly spent, garbage massing, a stack
// near the sky, the other board's Overdrive bearing down.
void draw_heat (App& app) {
	const Sim& sim = app.session->sim();
	if (!sim.config().fuse) {
		return;
	}
	const double danger = danger_of(sim);
	if (danger <= 0.) {
		return;
	}
	const float pulse = 0.75f + 0.25f * std::sin(sim.frame() * 0.3f);
	int w = 0;
	int h = 0;
	SDL_GetRendererOutputSize(app.renderer, &w, &h);
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
	const int reach = px(46);
	for (int i = 0; i < 3; ++i) {
		const int band = reach * (3 - i) / 3;
		const Uint8 alpha = static_cast<Uint8>(
			danger * pulse * (26 + 18 * i));
		const SDL_Color heat{212, 44, 22, alpha};
		fill(app.renderer, 0, 0, w, band, heat);
		fill(app.renderer, 0, h - band, w, band, heat);
		fill(app.renderer, 0, 0, band, h, heat);
		fill(app.renderer, w - band, 0, band, h, heat);
	}
	// A fresh landing flashes the floor.
	if (app.hit_flash > 0) {
		--app.hit_flash;
		fill(app.renderer, kBoardX, kBoardY + kBoardH - px(40), kBoardW,
			px(40), {224, 60, 30,
				static_cast<Uint8>(app.hit_flash * 18)});
	}
}

// Attack in flight: ember streaks arcing between the boards, a burst
// where they land.
void draw_streaks (App& app) {
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
	for (App::Streak& streak : app.streaks) {
		if (streak.at < 0.f) {
			continue;
		}
		streak.at += 0.05f;
		if (streak.at >= 1.f) {
			spawn_sparks_at(app, streak.tx, streak.ty,
				{255, 150, 70, 255}, 3 + std::min(streak.rows, 6), 2.6f);
			streak.at = -1.f;
			continue;
		}
		const float arc = -std::sin(streak.at * 3.14159f) * px(70);
		for (int ghost = 0; ghost < 4; ++ghost) {
			const float t = std::max(0.f, streak.at - ghost * 0.03f);
			const float x = streak.sx + (streak.tx - streak.sx) * t;
			const float y = streak.sy + (streak.ty - streak.sy) * t
				- std::sin(t * 3.14159f) * px(70);
			fill(app.renderer, static_cast<int>(x), static_cast<int>(y),
				px(5) - ghost, px(5) - ghost,
				{255, static_cast<Uint8>(170 - 26 * ghost), 60,
					static_cast<Uint8>(230 - 52 * ghost)});
		}
		(void) arc;
	}
}

void launch_streak (App& app, float sx, float sy, float tx, float ty,
		int rows) {
	App::Streak& streak = app.streaks[app.streak_at];
	app.streak_at = (app.streak_at + 1) % app.streaks.size();
	streak = {sx, sy, tx, ty, 0.f, rows};
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

// A rank in a coloured tile, laid out as an inline item. The career ladder
// and the game over screen both show one, and they show the same one because
// this is the only place it is drawn.
void rank_badge (const char* name, ImU32 fill, ImU32 ink, float wide,
		float scale = 1.f) {
	ImDrawList* draw = ImGui::GetWindowDrawList();
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const float tall = ImGui::GetTextLineHeight() * scale + ui(4);
	draw->AddRectFilled(ImVec2(at.x, at.y), ImVec2(at.x + wide, at.y + tall),
		fill, ui(5));
	const ImVec2 extent = ImGui::CalcTextSize(name);
	draw->AddText(nullptr, ImGui::GetFontSize() * scale,
		ImVec2(at.x + (wide - extent.x * scale) / 2, at.y + ui(2)), ink, name);
	ImGui::Dummy(ImVec2(wide, tall));
}

void draw_career (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(24)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	const float wide
		= std::min(ui(490), ImGui::GetIO().DisplaySize.x - ui(16));
	ImGui::SetNextWindowSizeConstraints(ImVec2(wide, 0),
		ImVec2(wide, ImGui::GetIO().DisplaySize.y - ui(48)));
	ImGui::Begin("Career", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	forge_panel(app);
	ImGui::PushFont(app.fonts.head);
	ImGui::TextUnformatted("The Forge Road");
	ImGui::PopFont();
	ImGui::TextDisabled("Sixteen stages, each with its own fire. Clear one");
	ImGui::TextDisabled("to open the next; slag buys permanent metal below.");
	ImGui::TextColored(ImVec4(1.f, 0.76f, 0.42f, 1.f), "SLAG %d",
		app.campaign.slag);
	ImGui::Dummy(ImVec2(0.f, ui(4)));
	const std::vector<campaign::Stage>& road = campaign::stages();
	for (size_t at = 0; at < road.size(); ++at) {
		if (at % campaign::kPerChapter == 0) {
			ImGui::PushFont(app.fonts.head);
			ImGui::Text("Chapter %d",
				static_cast<int>(at / campaign::kPerChapter) + 1);
			ImGui::PopFont();
		}
		const campaign::Stage& stage = road[at];
		const bool open_stage = campaign::open(app.campaign, at);
		const auto found = app.campaign.stars.find(stage.id);
		const int stars
			= found != app.campaign.stars.end() ? found->second : 0;
		ImGui::PushID(static_cast<int>(at));
		if (ImGui::BeginTable("stage", 3,
			ImGuiTableFlags_SizingFixedFit)) {
			ImGui::TableSetupColumn("what", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("stars");
			ImGui::TableSetupColumn("go");
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			// A boss row wears the duel's gold; a locked one goes ashen.
			const ImVec4 name_ink = !open_stage
				? ImVec4(0.45f, 0.42f, 0.4f, 1.f)
				: stage.mode == 5 ? ImVec4(1.f, 0.84f, 0.38f, 1.f)
				: ImVec4(0.93f, 0.87f, 0.8f, 1.f);
			ImGui::TextColored(name_ink, "%d-%d  %s",
				static_cast<int>(at) / campaign::kPerChapter + 1,
				static_cast<int>(at) % campaign::kPerChapter + 1,
				stage.name);
			if (open_stage) {
				// Wrapped, not clipped: a gimmick line cut mid-word reads
				// like a bug, and the row can afford a second line.
				ImGui::PushTextWrapPos(0.f);
				ImGui::TextDisabled("%s", stage.blurb);
				ImGui::PopTextWrapPos();
			} else {
				ImGui::TextDisabled("Locked.");
			}
			ImGui::TableSetColumnIndex(1);
			char marks[8] = "- - -";
			for (int i = 0; i < stars && i < 3; ++i) {
				marks[i * 2] = '*';
			}
			ImGui::TextColored(stars > 0
				? ImVec4(1.f, 0.84f, 0.38f, 1.f)
				: ImVec4(0.45f, 0.5f, 0.58f, 1.f), "%s", marks);
			ImGui::TableSetColumnIndex(2);
			if (open_stage) {
				if (ImGui::Button(stars > 0 ? "Again" : "Fight",
					ImVec2(ui(64), 0))) {
					start_stage(app, static_cast<int>(at));
				}
			}
			ImGui::EndTable();
		}
		ImGui::PopID();
	}
	ImGui::Separator();
	ImGui::PushFont(app.fonts.head);
	ImGui::TextUnformatted("The Anvil");
	ImGui::PopFont();
	ImGui::TextDisabled("Permanent metal, paid in slag. It rides into every");
	ImGui::TextDisabled("stage on the road - and only there.");
	ImGui::Dummy(ImVec2(0.f, ui(2)));
	for (const campaign::Upgrade& sold : campaign::anvil()) {
		const auto held = app.campaign.forge.find(sold.id);
		const int level = held != app.campaign.forge.end() ? held->second : 0;
		ImGui::PushID(sold.id);
		if (level >= sold.levels) {
			ImGui::TextColored(ImVec4(1.f, 0.84f, 0.38f, 1.f), "%s %d/%d",
				sold.name, level, sold.levels);
		} else {
			const int cost = campaign::upgrade_cost(sold, level + 1);
			ImGui::Text("%s %d/%d", sold.name, level, sold.levels);
			ImGui::SameLine();
			ImGui::BeginDisabled(app.campaign.slag < cost);
			char label[32];
			std::snprintf(label, sizeof label, "Buy (%d)", cost);
			if (ImGui::SmallButton(label)) {
				app.campaign.slag -= cost;
				app.campaign.forge[sold.id] = level + 1;
				campaign::save(campaign::path(app.root), app.campaign);
			}
			ImGui::EndDisabled();
		}
		ImGui::TextDisabled("%s", sold.text);
		ImGui::PopID();
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

// A mode entry: the same button as before, with a coloured edge down its
// left side and a small emblem struck into it. `mark` picks the shape -
// 0 flame, 1 hourglass, 2 rising floor, 3 brick, 4 crossed blades.
bool mode_button (const char* label, int mark, ImU32 tint, float width,
		float height) {
	const ImVec2 at = ImGui::GetCursorScreenPos();
	const bool clicked = ImGui::Button(label, ImVec2(width, height));
	ImDrawList* draw = ImGui::GetWindowDrawList();
	// The edge: a thick warm stripe that names the mode's family.
	draw->AddRectFilled(ImVec2(at.x, at.y + ui(4)),
		ImVec2(at.x + ui(4), at.y + height - ui(4)), tint, ui(2));
	const float mid = at.y + height / 2.f;
	const float cx = at.x + ui(24);
	const float r = height * 0.26f;
	switch (mark) {
		case 0:   // A flame.
			draw->AddTriangleFilled(ImVec2(cx, mid - r * 1.3f),
				ImVec2(cx - r * 0.75f, mid + r * 0.7f),
				ImVec2(cx + r * 0.75f, mid + r * 0.7f), tint);
			draw->AddCircleFilled(ImVec2(cx, mid + r * 0.45f), r * 0.62f,
				tint);
			break;
		case 1:   // An hourglass.
			draw->AddTriangleFilled(ImVec2(cx - r, mid - r),
				ImVec2(cx + r, mid - r), ImVec2(cx, mid), tint);
			draw->AddTriangleFilled(ImVec2(cx - r, mid + r),
				ImVec2(cx + r, mid + r), ImVec2(cx, mid), tint);
			break;
		case 2:   // A floor, rising.
			draw->AddRectFilled(ImVec2(cx - r, mid + r * 0.5f),
				ImVec2(cx + r, mid + r), tint);
			draw->AddTriangleFilled(ImVec2(cx, mid - r),
				ImVec2(cx - r * 0.7f, mid + r * 0.1f),
				ImVec2(cx + r * 0.7f, mid + r * 0.1f), tint);
			break;
		case 3:   // Brickwork.
			for (int row = 0; row < 2; ++row) {
				for (int col = 0; col < 2; ++col) {
					const float ox = (row % 2) * r * 0.4f;
					draw->AddRectFilled(
						ImVec2(cx - r + ox + col * r * 0.95f,
							mid - r * 0.6f + row * r * 0.7f),
						ImVec2(cx - r * 0.2f + ox + col * r * 0.95f,
							mid - r * 0.05f + row * r * 0.7f), tint);
				}
			}
			break;
		case 5: {  // A hammer over an anvil: the blade being worked.
			draw->AddRectFilled(ImVec2(cx - r, mid + r * 0.35f),
				ImVec2(cx + r, mid + r * 0.8f), tint, ui(1));
			draw->AddRectFilled(ImVec2(cx - r * 0.5f, mid - r),
				ImVec2(cx + r * 0.95f, mid - r * 0.5f), tint, ui(1));
			draw->AddLine(ImVec2(cx - r * 0.25f, mid - r * 0.5f),
				ImVec2(cx - r * 0.6f, mid + r * 0.3f), tint, ui(3));
			break;
		}
		default:  // Crossed blades.
			draw->AddLine(ImVec2(cx - r, mid - r), ImVec2(cx + r, mid + r),
				tint, ui(3));
			draw->AddLine(ImVec2(cx + r, mid - r), ImVec2(cx - r, mid + r),
				tint, ui(3));
			break;
	}
	return clicked;
}

// One option in a picker row: drawn selected in the accent, and returns
// true when clicked.
bool option_button (const char* label, bool selected, float width) {
	if (selected) {
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.24f, 0.10f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.50f, 0.29f, 0.13f, 1.f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.58f, 0.34f, 0.16f, 1.f));
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
		forge_panel(app);
		// The wordmark: a tongue of real fire, then the letters cooling
		// from gold at the flame's edge to ember at the far end.
		ImGui::PushFont(app.fonts.title);
		ImFont* mark = app.fonts.title;
		ImDrawList* dl = ImGui::GetWindowDrawList();
		const ImVec2 at = ImGui::GetCursorScreenPos();
		const float tall = mark->FontSize;
		const float fx = at.x + tall * 0.28f;
		const float fy = at.y + tall * 0.52f;
		if (app.flames != nullptr) {
			// The mark burns for real: one tongue off the same strip the
			// room uses, cycling beside the letters. Through ImGui rather
			// than SDL, because the plate is drawn over the SDL pass and a
			// logo behind its own panel would be a strange thing to ship.
			const int frame = static_cast<int>(app.backdrop_tick / 3) % kFlameFrames;
			const float edge = static_cast<float>(frame) / kFlameFrames;
			const float wide = tall * 0.66f;
			dl->AddImage(reinterpret_cast<ImTextureID>(app.flames),
				ImVec2(fx - wide / 2, fy - tall * 0.52f),
				ImVec2(fx + wide / 2, fy + tall * 0.48f),
				ImVec2(edge, 0.f),
				ImVec2(edge + 1.f / kFlameFrames, 1.f));
		}
		float pen = at.x + tall * 0.66f;
		const char* word = "FORCETRIS";
		const int letters = 9;
		for (int i = 0; i < letters; ++i) {
			const char glyph[2] = {word[i], '\0'};
			const float part = static_cast<float>(i) / (letters - 1);
			const ImU32 shade = IM_COL32(255,
				static_cast<int>(214 - 76 * part),
				static_cast<int>(96 - 38 * part), 255);
			dl->AddText(mark, tall, ImVec2(pen + 2.f, at.y + 2.f),
				IM_COL32(40, 16, 6, 200), glyph);
			dl->AddText(mark, tall, ImVec2(pen, at.y), shade, glyph);
			pen += mark->CalcTextSizeA(tall, FLT_MAX, 0.f, glyph).x;
		}
		ImGui::Dummy(ImVec2(pen - at.x, tall));
		ImGui::PopFont();
		ImGui::TextDisabled("every piece burns");
		ImGui::Dummy(ImVec2(0.f, ui(10)));
		if (ImGui::Button("Play", ImVec2(ui(260), ui(44)))) {
			app.screen = Screen::Modes;
			app.mode_popup = 0;
		}
		ImGui::Dummy(ImVec2(0.f, ui(2)));
		if (ImGui::Button("Career", ImVec2(ui(260), ui(44)))) {
			app.career = career::load(career::path(app.root));
			app.campaign = campaign::load(campaign::path(app.root));
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
			forge_panel(app);
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
			forge_panel(app);
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
			// Seven modes of button and blurb outgrow a short display; the
			// cap turns the overflow into a scrollbar instead of running
			// the last mode off the bottom edge.
			ImGui::SetNextWindowSizeConstraints(ImVec2(ui(300), 0.f),
				ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y - ui(12)));
			ImGui::Begin("mode select", nullptr, box);
			forge_panel(app);
			ImGui::PushFont(app.fonts.head);
			ImGui::TextUnformatted("Choose a mode");
			ImGui::PopFont();
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (mode_button("Tempering", 5, IM_COL32(214, 128, 62, 255), ui(280), ui(44))) {
				start_game(app, 6);
			}
			ImGui::TextDisabled("The run: twelve heats, a temper drafted");
			ImGui::TextDisabled("at each. Forge the blade or go cold.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (mode_button("Ignition", 0, IM_COL32(255, 138, 58, 255), ui(280), ui(44))) {
				start_game(app, 0);
			}
			ImGui::TextDisabled("Endless. The fuse shortens, the drafts");
			ImGui::TextDisabled("keep coming; burn for as long as you can.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (mode_button("Blaze", 1, IM_COL32(255, 196, 78, 255), ui(280), ui(44))) {
				start_game(app, 1);
			}
			ImGui::TextDisabled("Three minutes on the clock; the multiplier");
			ImGui::TextDisabled("climbs as it drains. Make them count.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (mode_button("Inferno", 2, IM_COL32(232, 96, 44, 255), ui(280), ui(44))) {
				start_game(app, 2);
			}
			ImGui::TextDisabled("The floor rises, the levels ramp, and the");
			ImGui::TextDisabled("fuse keeps shrinking.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (mode_button("Meltdown / Bunker", 3, IM_COL32(196, 122, 70, 255), ui(280), ui(44))) {
				app.mode_popup = 1;
			}
			ImGui::TextDisabled("Race a stack of holey garbage down, or");
			ImGui::TextDisabled("outlast the rising floor. Cut to taste.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			if (mode_button("Duel", 4, IM_COL32(255, 214, 96, 255), ui(280), ui(44))) {
				app.mode_popup = 2;
			}
			ImGui::TextDisabled("Fight the bot, rank D through X.");
			ImGui::TextDisabled("Every rank carries its own blade.");
			ImGui::Dummy(ImVec2(0.f, ui(6)));
			if (ImGui::Button("Back", ImVec2(ui(280), 0))) {
				app.screen = Screen::Menu;
			}
			ImGui::End();
		}
		ImGui::PopStyleVar();
	} else if (app.screen == Screen::Game && !app.offers.empty()) {
		// The draft. The board is frozen behind it and the fuse with it, so
		// this is the one screen in the game that is allowed to take its
		// time - but the pick itself is one press, because ten lines from
		// now there will be another one.
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("temper", nullptr, box);
		forge_panel(app);
		ImGui::PushFont(app.fonts.head);
		ImGui::Text("HEAT %d of %d", app.heat + 1, temper::kHeats);
		ImGui::PopFont();
		ImGui::TextDisabled("The forge tightens. Take something with you.");
		// The coin line: what the run has earned and not yet spent, and the
		// two things it buys. Both buttons go quiet rather than vanish when
		// the purse is short, so the prices are always readable.
		{
			const int purse = ember_balance(app);
			ImGui::TextColored(ImVec4(1.f, 0.76f, 0.42f, 1.f),
				"EMBERS %d", purse);
			ImGui::SameLine();
			ImGui::BeginDisabled(app.offer_taken
				|| purse < temper::kRerollCost);
			char label[48];
			std::snprintf(label, sizeof label, "Reroll (%d)",
				temper::kRerollCost);
			if (ImGui::SmallButton(label)) {
				reroll_offer(app);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(app.extra_picks > 0 || app.offers.size() < 2
				|| purse < temper::kExtraPickCost);
			std::snprintf(label, sizeof label, "Second pick (%d)",
				temper::kExtraPickCost);
			if (ImGui::SmallButton(label)) {
				buy_extra_pick(app);
			}
			ImGui::EndDisabled();
			if (app.extra_picks > 0) {
				ImGui::SameLine();
				ImGui::TextDisabled("take two");
			}
		}
		ImGui::Dummy(ImVec2(0.f, ui(6)));
		// A card is its family's colour and word, a name, and one plain
		// line - no numbers anywhere on the face. The card either reads in
		// a second or it has failed; the arithmetic lives in the README
		// for whoever wants it.
		const float card_w = kMobile ? ui(300) : ui(224);
		for (size_t at = 0; at < app.offers.size(); ++at) {
			const temper::Temper* card = temper::find(app.offers[at]);
			if (card == nullptr) {
				continue;
			}
			// Side by side on a desk, stacked on a phone, where three
			// columns of readable text will not fit across the screen.
			if (!kMobile && at > 0) {
				ImGui::SameLine();
			}
			ImGui::PushID(static_cast<int>(at));
			ImGui::BeginGroup();
			{
				// The stamp above the name: glyph, then the family word,
				// both in the family's own ink.
				const ImU32 ink = family_ink(card->family);
				ImDrawList* draw = ImGui::GetWindowDrawList();
				const ImVec2 pen = ImGui::GetCursorScreenPos();
				const float glyph = ImGui::GetFontSize() * 0.9f;
				family_glyph(draw, card->family, pen, glyph, ink);
				draw->AddText(ImVec2(pen.x + glyph + ui(6), pen.y), ink,
					family_tag(card->family));
				ImGui::Dummy(ImVec2(card_w, glyph + ui(2)));
			}
			ImGui::PushStyleColor(ImGuiCol_Button, family_ink(card->family));
			ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(24, 16, 12, 255));
			// The cursor the arrow keys move sits on one card; a mouse can
			// ignore it entirely and click any of them.
			if (static_cast<int>(at) == app.offer_at) {
				ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(255, 236, 190, 255));
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, ui(2));
			}
			char label[96];
			std::snprintf(label, sizeof label, "%d  %s",
				static_cast<int>(at) + 1, card->name);
			if (ImGui::Button(label, ImVec2(card_w, ui(40)))) {
				take_temper(app, static_cast<int>(at));
			}
			if (static_cast<int>(at) == app.offer_at) {
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
			}
			ImGui::PopStyleColor(2);
			// The effect line carries the card, so it gets the body ink
			// rather than the greyed-out afterthought treatment.
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + card_w);
			ImGui::TextUnformatted(card->text);
			ImGui::PopTextWrapPos();
			ImGui::EndGroup();
			ImGui::PopID();
		}
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		ImGui::TextDisabled("%s", kMobile
			? "Tap one." : "1 2 3, or the arrows and Enter.");
		if (!app.tempers.empty()) {
			ImGui::Separator();
			ImGui::TextDisabled("%s", temper_line(app.tempers).c_str());
		}
		ImGui::End();
	} else if (app.screen == Screen::Game && app.paused) {
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("paused", nullptr, box);
		forge_panel(app);
		ImGui::TextUnformatted("Paused");
		if (!app.tempers.empty()) {
			ImGui::TextDisabled("%s", temper_line(app.tempers).c_str());
		}
		ImGui::Spacing();
		if (ImGui::Button("Resume", ImVec2(ui(240), 0))) {
			app.paused = false;
		}
		if (ImGui::Button("Restart", ImVec2(ui(240), 0))) {
			if (app.campaign_stage >= 0) {
				start_stage(app, app.campaign_stage);
			} else if (app.mode == 5) {
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
		// Never taller than the display. ImGui puts an auto-fitting window
		// through its size constraints and then decides on a scrollbar, so
		// capping the height here is what makes a panel that cannot fit
		// scroll instead of running its last button off the bottom edge.
		ImGui::SetNextWindowSizeConstraints(ImVec2(ui(300), 0.f),
			ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y - ui(12)));
		ImGui::Begin("game over", nullptr, box);
		forge_panel(app);
		const bool won = app.session.has_value() && app.session->sim().won();
		{
			// Struck into the plate: a cast shadow under letters that cool
			// from gold to ember, the way the wordmark does.
			const char* verdict = app.versus.has_value()
				? (app.versus->player_wins > app.versus->bot_wins
					? "You win the match!" : "You lose the match")
				: (app.mode == 6 ? (won ? "Forged!" : "Went cold")
					: (won ? "Finished!" : "Game over"));
			ImFont* font = app.fonts.head;
			ImDrawList* draw = ImGui::GetWindowDrawList();
			const ImVec2 at = ImGui::GetCursorScreenPos();
			const float tall = font->FontSize;
			float pen = at.x;
			for (const char* letter = verdict; *letter != '\0'; ++letter) {
				const char glyph[2] = {*letter, '\0'};
				const float part = static_cast<float>(letter - verdict)
					/ std::max<float>(1.f, std::strlen(verdict) - 1);
				draw->AddText(font, tall, ImVec2(pen + ui(2), at.y + ui(2)),
					IM_COL32(38, 15, 6, 190), glyph);
				draw->AddText(font, tall, ImVec2(pen, at.y),
					IM_COL32(255, static_cast<int>(214 - 76 * part),
						static_cast<int>(96 - 38 * part), 255), glyph);
				pen += font->CalcTextSizeA(tall, FLT_MAX, 0.f, glyph).x;
			}
			ImGui::Dummy(ImVec2(pen - at.x, tall));
		}
		if (app.versus.has_value()) {
			ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
				"You %d - %d Bot (%s)  first to %d",
				app.versus->player_wins, app.versus->bot_wins,
				bot::ranks()[app.versus->rank_index].name,
				app.versus->first_to);
		}
		if (won && app.mode == 3) {
			const double seconds = app.session->sim().frame() * 0.02;
			ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
				"All the cheese in %d:%05.2f",
				static_cast<int>(seconds) / 60, std::fmod(seconds, 60.));
		}
		if (app.mode == 6) {
			ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
				won ? "All twelve heats" : "Heat %d of %d",
				std::min(app.heat + 1, temper::kHeats), temper::kHeats);
		}
		if (app.campaign_stage >= 0) {
			// The stage's receipt: its name, the stars this attempt earned
			// (the road keeps the best), and the slag that came home.
			const campaign::Stage& stage = campaign::stages()[
				static_cast<size_t>(app.campaign_stage)];
			std::string stars;
			for (int i = 0; i < app.last_stage_stars; ++i) {
				stars += "* ";
			}
			ImGui::TextColored(ImVec4(1.f, 0.76f, 0.42f, 1.f),
				"%s  %s+%d slag", stage.name,
				stars.c_str(), app.last_slag_gain);
		}
		if (!app.tempers.empty()) {
			// What the run was carrying when it ended - two runs of the
			// same score are not the same run.
			ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ui(300));
			ImGui::TextDisabled("%s", temper_line(app.tempers).c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::Spacing();
		// The two numbers a finished run is about. Everything else it
		// produced is one button away in the analysis window, which draws it
		// from the very rows this screen used to repeat underneath.
		if (app.session.has_value()) {
			ImGui::TextDisabled("%s", "SCORE");
			ImGui::PushFont(app.fonts.title);
			ImGui::TextColored(ImVec4(1.f, 0.84f, 0.38f, 1.f), "%s",
				grouped(app.session->sim().final_score()).c_str());
			ImGui::PopFont();
		}
		ImGui::Spacing();
		{
			// The same estimate the analysis window's Rating tab reports,
			// out of the same call, so the two screens cannot name different
			// ranks for one run. An empty rank is the module's way of saying
			// the run was too small to place at all.
			rating::Estimate guess;
			if (app.last_replay.has_value()) {
				const replay::Summary sum = app.last_replay->summary(false);
				guess = rating::estimate(sum.apm, sum.pps, sum.vs);
			}
			ImGui::TextDisabled("%s", "RANK");
			if (guess.rank[0] != '\0') {
				rank_badge(guess.rank, IM_COL32(216, 124, 44, 255),
					IM_COL32(28, 16, 8, 255), ui(70), 1.5f);
				ImGui::SameLine();
				ImGui::PushFont(app.fonts.head);
				ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
					" %.0f TR", guess.tr);
				ImGui::PopFont();
			} else {
				ImGui::TextDisabled("Too short a run to place.");
			}
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
			if (app.campaign_stage >= 0) {
				start_stage(app, app.campaign_stage);
			} else if (app.mode == 5) {
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
			return !app.offers.empty() ? "temper"
				: app.paused ? "pause"
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

constexpr int kTour = 17;

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
			app.score_page = hiscore::kFuseTables + 1;
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
			app.campaign = campaign::load(campaign::path(app.root));
			app.screen = Screen::Career;
			break;
		case 15:
			// The draft, over a real game. The masher cannot reach this one
			// on its own - random presses do not clear ten lines - so the
			// tour starts a Tempering run and puts its first heat's cards
			// on the table.
			start_game(app, 6);
			// The countdown goes: cards are only ever dealt on a board that
			// is already running, and this stop must look like that too.
			app.countdown = 0;
			app.offers = temper::offer(app.temper_seed, 0, {});
			app.offer_at = 0;
			app.offer_shown = 0;
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
	// The timer subsystem is not decoration: on Windows it is what asks the
	// OS for 1ms sleep granularity (timeBeginPeriod). Without it the pacing
	// nap below the render loop - SDL_Delay(1) - can sleep up to ~15ms at a
	// time, which bunches sim ticks into some frames and starves others:
	// exactly the judder the low-latency mode exists to avoid.
	SDL_SetHint(SDL_HINT_TIMER_RESOLUTION, "1");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
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
		app.audio.set_music_mode(static_cast<Audio::Music>(app.config.music_mode));
		app.audio.set_ambience(app.config.ambience);
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
	// Low-latency mode drops vsync on a desk - the loop is paced by a
	// millisecond nap instead, so input is polled and simmed within a few
	// ms of arriving rather than waiting out a display refresh. Phones
	// keep vsync: their compositors enforce it anyway, and the nap-loop
	// would only heat the battery.
	const bool vsynced = kMobile || !app.config.lowlatency;
	app.renderer = SDL_CreateRenderer(app.window, -1,
		smoke ? SDL_RENDERER_SOFTWARE
		      : (SDL_RENDERER_ACCELERATED
		         | (vsynced ? SDL_RENDERER_PRESENTVSYNC : 0)));
	if (app.renderer == nullptr) {
		app.renderer = SDL_CreateRenderer(app.window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (app.renderer == nullptr) {
		SDL_Log("SDL_CreateRenderer: %s", SDL_GetError());
		return 1;
	}
	app.glow = make_glow(app.renderer);
	app.flames = make_flames(app.renderer);
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
			mode = std::clamp(std::atoi(forced), 0, 6);
		}
		if (mode == 4) {
			app.config.cheese_period = 150;
		}
		if (const char* stage = std::getenv("FORCETRIS_SMOKE_STAGE")) {
			// A Forge Road stage under the masher, so every recipe's whole
			// loop - launch, overrides, preset board, settlement - can be
			// proven headlessly, stage by stage. The campaign file loads
			// first, the way the Career screen would have loaded it on the
			// way in: the smoke's stage carries the file's Anvil - Preheat's
			// owed hand included - not a blank forge's.
			app.campaign = campaign::load(campaign::path(app.root));
			start_stage(app, std::clamp(std::atoi(stage), 0,
				static_cast<int>(campaign::stages().size()) - 1));
		} else if (mode == 5) {
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
			// The draft answers to keys the masher does not have, so a
			// scripted Tempering run would sit on the first card forever.
			// It waits a dozen frames before choosing, which is also long
			// enough for the screenshot tour to catch the screen.
			if (!app.offers.empty()) {
				if (++app.offer_shown > 10) {
					take_temper(app,
						static_cast<int>(mash() % app.offers.size()));
				}
				behind = 0.02;
				++frames;
			}
			if (!app.offers.empty()) {
				// No mashed keys while the cards are up: space is both the
				// hard drop and the take, and a masher slapping it would
				// snatch the card off the screenshot tour's table before
				// the camera fires. The auto-pick above still proves the
				// take path.
			} else if (frames % 3 == 0) {
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
			const double slice = static_cast<double>(now - previous)
				/ SDL_GetPerformanceFrequency();
			previous = now;
			behind = std::min(behind + slice, 0.25);
			app.frame_ms[app.frame_at] = static_cast<float>(slice * 1000.);
			app.frame_at = (app.frame_at + 1) % 120;
		}

		if (smoke) {
			app.input_nudge = false;
		}
		// Where the last drawn piece stood, taken before this batch of sim
		// ticks so the renderer can draw the travel between two 20ms steps.
		// A batch that carries input snaps instead: a pressed key's effect
		// must be on screen the very next frame, not slide in late.
		const bool nudged = app.input_nudge;
		Piece pre_piece;
		bool pre_entry = false;
		long pre_frame = -1;
		if (app.session.has_value() && app.screen == Screen::Game) {
			const Sim& sim = app.session->sim();
			pre_frame = sim.frame();
			pre_entry = sim.entry();
			if (pre_entry) {
				pre_piece = sim.piece();
			}
		}
		int ticks = 0;
		while (behind >= 0.02 || (app.input_nudge && behind >= 0.0)) {
			++ticks;
			behind -= 0.02;
			app.input_nudge = false;
			if (app.screen == Screen::Game && !app.paused && !app.editing
				&& app.offers.empty()) {
				if (app.countdown > 0) {
					// The pre-game breath: both boards stand frozen - the
					// versus step is skipped too, so the bot waits with you.
					--app.countdown;
					if (app.countdown == 0 && app.preheat_owed) {
						// The countdown has just burned out: now the owed
						// Preheat hand goes on the table, dealt from heat
						// zero the way the ten-line crossing would deal it.
						app.preheat_owed = false;
							app.offers = temper::offer(app.temper_seed, 0, {});
						app.offer_at = 0;
						app.offer_shown = 0;
					}
					++frames;
					continue;
				}
				const bool live = app.session->step();
				if (app.versus.has_value()) {
					if ((app.career_stage >= 0 || app.campaign_stage >= 0)
						&& app.session->sim().overdrive()) {
						app.career_od = true;
						app.campaign_od = true;
					}
					app.versus->step(*app.session);
					// No drafts in a duel, either side: the freeze that
					// lets a hand read cards has no business in a real-time
					// fight. The heats still tighten the fuse - that is
					// sim-side - and the builds are settled before the
					// first piece falls.
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
				} else {
					// Crossing a heat is where the forge tightens the fuse,
					// so it is also where it hands over a tool: the board
					// waits until a card is taken. offer_tempers itself
					// knows which games draft (fuse-rules ones) and which
					// never do.
					offer_tempers(app);
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
		if (!smoke) {
			++app.tick_hist[std::min(ticks, 2)];
		}
		if (app.session.has_value() && app.screen == Screen::Game
			&& app.session->sim().frame() != pre_frame) {
			// The sim moved: remember where the piece was so the next draws
			// can close the gap, unless this batch carried a key press.
			app.lerp_prev = pre_piece;
			app.lerp_have = !nudged && pre_entry && app.session->sim().entry();
		}
		// How far this drawn frame sits past the last tick, as a fraction of
		// one. A nudged tick leaves behind negative - the sim ran ahead of
		// the clock for the input's sake - and that draws as the current
		// position, exactly where the pressed key put the piece.
		app.lerp_alpha = behind <= 0. ? 1.f
			: std::min(1.f, static_cast<float>(behind / 0.02));
		if (app.session.has_value()) {
			for (const std::string& cue : app.session->take_cues()) {
				app.audio.play(cue);
				juice_cue(app, cue);
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

		SDL_SetRenderDrawColor(app.renderer, 14, 11, 9, 255);
		SDL_RenderClear(app.renderer);
		draw_backdrop(app);
		draw_overdrive_bloom(app);
		{
			// The room, handed to the mixer from the same reading the
			// backdrop just painted with: the furnace bed and the score's
			// layers rise and fall with the picture, not beside it.
			const Room room = room_of(app);
			app.audio.set_room(static_cast<float>(room.heat),
				static_cast<float>(room.danger), room.burning, room.playing);
		}
		if (app.session.has_value()
			&& (app.screen == Screen::Game || app.screen == Screen::Over)) {
			// The shudder: the whole board pane jolts a few pixels while a
			// quad, spin or Overdrive still rings - cosmetic, and off by a
			// Rules switch.
			const bool quaking = app.config.shake
				&& app.session->sim().frame() < app.shake_until;
			SDL_Rect quake{0, 0, 0, 0};
			if (quaking) {
				int w = 0;
				int h = 0;
				SDL_GetRendererOutputSize(app.renderer, &w, &h);
				quake = {static_cast<int>(app.seeds() % 7) - 3,
					static_cast<int>(app.seeds() % 7) - 3, w, h};
				SDL_RenderSetViewport(app.renderer, &quake);
			}
			light_burn_rows(app);
			draw_board(app);
			draw_burn_rows(app);
			draw_sparks(app);
			draw_streaks(app);
			draw_overdrive_frame(app);
			draw_heat(app);
			if (quaking) {
				SDL_RenderSetViewport(app.renderer, nullptr);
			}
			{
				const Sim& sim = app.session->sim();
				// Overdrive as an event: a flash and a banner on ignition.
				if (sim.overdrive() && !app.was_overdrive) {
					app.od_flash = 12;
					app.od_banner = 50;
				}
				app.was_overdrive = sim.overdrive();
				// The other board's heat arriving is worth hearing.
				if (sim.pressured() && !app.was_pressured) {
					app.audio.play("pressure");
				}
				app.was_pressured = sim.pressured();
				app.audio.set_music_rate(
					sim.overdrive() ? 1.15f : 1.f);
				// Garbage landing hits back: flash, thud, a small shudder.
				const int pending = sim.pending_garbage();
				if (pending > app.last_pending) {
					app.hit_flash = 8;
					app.audio.play("hit");
					if (app.config.shake) {
						app.shake_until = sim.frame() + 4;
					}
				}
				app.last_pending = pending;
				// The wire's traffic becomes streaks between the boards.
				if (app.versus.has_value()) {
					if (app.versus->wire_to_bot > 0) {
						launch_streak(app,
							kBoardX + kBoardW * 0.5f,
							kBoardY + kBoardH * 0.35f,
							kMiniX + kWidth * kMiniCell * 0.5f,
							kMiniY + kHeight * kMiniCell * 0.4f,
							app.versus->wire_to_bot);
					}
					if (app.versus->wire_to_player > 0) {
						launch_streak(app,
							kMiniX + kWidth * kMiniCell * 0.5f,
							kMiniY + kHeight * kMiniCell * 0.4f,
							kBoardX + kBoardW * 0.5f,
							kBoardY + kBoardH * 0.5f,
							app.versus->wire_to_player);
					}
					app.versus->wire_to_bot = 0;
					app.versus->wire_to_player = 0;
				}
				// The ignition flash and banner, over everything.
				if (app.od_flash > 0) {
					--app.od_flash;
					int w = 0;
					int h = 0;
					SDL_GetRendererOutputSize(app.renderer, &w, &h);
					SDL_SetRenderDrawBlendMode(app.renderer,
						SDL_BLENDMODE_BLEND);
					fill(app.renderer, 0, 0, w, h, {255, 214, 96,
						static_cast<Uint8>(app.od_flash * 10)});
				}
				if (app.od_banner > 0) {
					--app.od_banner;
					ImFont* font = app.fonts.title;
					const char* cry = "OVERDRIVE";
					const ImVec2 extent = font->CalcTextSizeA(
						font->FontSize, FLT_MAX, 0.f, cry);
					const float alpha = app.od_banner > 12
						? 1.f : app.od_banner / 12.f;
					ImGui::GetForegroundDrawList()->AddText(font,
						font->FontSize,
						ImVec2(kBoardX + (kBoardW - extent.x) / 2,
							kBoardY + kBoardH * 0.30f),
						IM_COL32(255, 214, 96,
							static_cast<int>(alpha * 255)), cry);
				}
				// Overdrive sheds sparks off the Flow rail while it burns.
				if (sim.overdrive() && sim.frame() % 5 == 0) {
					spawn_sparks_at(app,
						static_cast<float>(kBoardX - px(15)),
						kBoardY + px(120)
							+ static_cast<float>(app.seeds()
								% std::max(1, kBoardH - px(130))),
						{255, 214, 96, 255}, 1, 1.4f);
				}
			}
			draw_label("HOLD", kBoardX - ui(122), kBoardY - ui(24));
			draw_label("NEXT", kBoardX + kBoardW + ui(18), kBoardY - ui(24));
			if (app.session->sim().config().fuse) {
				draw_label("FLOW", kBoardX - px(114), kBoardY + px(98));
				if (app.session->sim().overdrive()) {
					draw_label("OVERDRIVE", kBoardX - px(114),
						kBoardY + kBoardH - px(4),
						IM_COL32(255, 214, 96, 255));
				}
			}
			if (app.session->sim().config().fuse) {
				// How deep into the forge, over the well, where the clock
				// would be in a mode that had one. Derived from the sim's
				// own counters - a duel drafts nothing, but its fuse still
				// tightens on the same rungs. Tempering counts to its
				// finish line; everywhere else the count just climbs.
				const Sim& sim = app.session->sim();
				const int rung = 1 + temper::heats_done(sim.lines_cleared(),
					sim.downstack(), app.mode == 3);
				char heat[32];
				if (app.mode == 6) {
					std::snprintf(heat, sizeof heat, "HEAT %d / %d",
						std::min(rung, temper::kHeats), temper::kHeats);
				} else {
					std::snprintf(heat, sizeof heat, "HEAT %d", rung);
				}
				draw_label(heat, kBoardX + ui(4), kBoardY - ui(24),
					IM_COL32(255, 196, 120, 255));
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
			if (app.screen == Screen::Game && !app.paused && !app.editing
				&& app.offers.empty()) {
				// The play buttons step aside while a draft is up - the
				// cards are the only thing a finger should be able to hit.
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
		if (app.show_frames) {
			draw_frame_stats(app);
		}

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
		if (!smoke && !kMobile && app.config.lowlatency) {
			SDL_Delay(1);   // Vsync is off; don't spin the whole core.
		}

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
				} else if (app.campaign_stage >= 0) {
					start_stage(app, app.campaign_stage);
				} else if (app.mode == 5) {
					start_versus(app);
				} else {
					start_game(app, app.mode);
				}
			} else if (toured > 0 && toured <= kTour
				&& app.screen != Screen::Viewer
				&& (app.screen != Screen::Game || app.paused || app.editing
					|| !app.offers.empty())) {
				if (++tour_frames >= 6) {
					tour_frames = 0;
					app.screen = Screen::Over;
					app.studying.reset();
					app.show_settings = false;
					app.editing = false;
					app.paused = false;
					// A draft left on the table would freeze every game
					// after this stop.
					app.offers.clear();
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
	if (app.flames != nullptr) {
		SDL_DestroyTexture(app.flames);
		app.flames = nullptr;
	}
	if (app.glow != nullptr) {
		SDL_DestroyTexture(app.glow);
	}
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
