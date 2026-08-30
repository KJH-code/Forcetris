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
#include "gfx.hpp"
#include "forcetris/campaign.hpp"
#include "forcetris/career.hpp"
#include "forcetris/hiscore.hpp"
#include "forcetris/munch.hpp"
#include "forcetris/profile.hpp"
#include "forcetris/rating.hpp"
#include "forcetris/replay.hpp"
#include "forcetris/temper.hpp"
#include "palette.hpp"
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
// The duel layout: during a versus game the opponent is not a mini panel
// in the margin but a full-size board beside the player's - two boards,
// same cells, side by side, so the fight is read at a glance. Portrait
// has no width for two full wells, so there the opponent merely grows.
bool kDuelSide = false;
int kCenterDX = 0;   // The landscape centering shift, reapplied on top.

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
	kCenterDX = 0;
}

void apply_duel_side (bool on) {
	kDuelSide = on;
	if (kMobile && kPortrait) {
		// The essential column stays where it was; the opponent's board
		// grows as far as the margin under the previews allows.
		kMiniCell = on ? px(11) : px(8);
		kMiniX = kBoardX + kBoardW + (on ? px(14) : px(18));
		kMiniY = kBoardY + (on ? px(284) : px(348));
		return;
	}
	kBoardX = px(on ? 150 : 300) + kCenterDX;
	kMiniX = px(on ? 690 : 940) + kCenterDX;
	kMiniY = kBoardY + (on ? 0 : px(40));
	kMiniCell = on ? kCell : px(13);
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
		kCenterDX = dx;
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

Fonts load_fonts (const std::string& root) {
	Fonts fonts;
	ImGuiIO& io = ImGui::GetIO();
	const auto [regular, bold] = find_font_files();
	// The bundled display faces (OFL, in gfx/fonts): Marcellus carries the
	// headings, Cinzel Decorative the wordmark and the big verdicts. The
	// body stays a system sans - reading is its whole job - and a checkout
	// without gfx/ falls back to the system bold for everything.
	const std::string display = first_file(
		{root + "/gfx/fonts/Marcellus-Regular.ttf"});
	const std::string mark = first_file(
		{root + "/gfx/fonts/CinzelDecorative-Bold.ttf"});
	if (!regular.empty()) {
		fonts.body = io.Fonts->AddFontFromFileTTF(regular.c_str(), ui(19.f));
		const std::string& heavy = bold.empty() ? regular : bold;
		fonts.head = io.Fonts->AddFontFromFileTTF(
			(display.empty() ? heavy : display).c_str(), ui(26.f));
		fonts.title = io.Fonts->AddFontFromFileTTF(
			(mark.empty() ? heavy : mark).c_str(), ui(42.f));
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
	// Every hue here comes from palette.hpp, so the chrome and the
	// generated art quote one set of numbers rather than two that drifted.
	const ImVec4 canvas = ink::vec(ink::kSoot, 0.98f);
	const ImVec4 well = ink::vec(ink::kWell);
	const ImVec4 wellHover(0.216f, 0.157f, 0.110f, 1.f);
	const ImVec4 wellActive(0.267f, 0.192f, 0.133f, 1.f);
	const ImVec4 accent = ink::vec(ink::kEmber);
	const ImVec4 accentDim = ink::vec(ink::kEmber, 0.28f);
	const ImVec4 edge(0.376f, 0.267f, 0.176f, 0.60f);
	const ImVec4 text = ink::vec(ink::kInk);
	const ImVec4 faded = ink::vec(ink::kMuted);

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
	colors[ImGuiCol_SliderGrabActive] = ink::vec(ink::kEmberHot);
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

// How long the polish beats run, in frames at the 50Hz sim clock the
// render loop is paced against.
constexpr int kCurtain = 16;   // A screen change: quick, or it is a wait.
constexpr int kCooling = 26;   // The well going out under a verdict.
constexpr int kStrike = 96;    // The maul, on a run's first frame.

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
	// The Forge Map: the run's graph (rebuilt from the save's chapter and
	// seed whenever a run is on), which node the battle in play came from
	// (-1 when the stage was launched outside the map, e.g. the stage
	// smoke), whether a reward hand is owed on the map, whether the last
	// settlement ended the run, and the difficulty picked at the door for
	// the next set-out.
	std::vector<campaign::MapNode> run_map;
	int run_node = -1;
	bool map_reward = false;
	bool run_ended = false;
	// --- The polish timers. Every one of these counts down to zero and
	// does nothing at all while it is there, so a screen that forgets to
	// start one simply looks the way it always did.
	//
	// The curtain, lifted on every screen change. Watched rather than
	// wired: there are forty-odd places that assign a screen, and a
	// transition every one of them had to remember would be a transition
	// half the game did not have.
	Screen screen_was = Screen::Menu;
	int curtain = 0;
	// The well coming up to heat behind the count, and cooling after the
	// last piece: a game should start and stop, not blink.
	int cool_down = 0;
	// The maul, on a run's first frame. Long, because it is the one
	// flourish in the game that is allowed to be.
	int forge_strike = 0;
	// A jolt of the WHOLE screen, in frames left. The board's own quake
	// only runs on the game screens and only moves the board pane, so a
	// blow landed anywhere else - the map, most of all - shook nothing at
	// all. This one shifts the viewport for the entire pass, ImGui
	// included, which is the only way a menu screen can be hit.
	int jolt = 0;
	int jolt_born = 1;
	float jolt_power = 0.f;
	// Where the tree stands, filled by the map each frame it draws: the
	// foot it is struck at, the head the shock travels to, and the span
	// the light sweeps across.
	ImVec2 map_foot{0.f, 0.f};
	ImVec2 map_head{0.f, 0.f};
	ImVec2 map_span{0.f, 0.f};
	bool map_seen = false;
	// The bed's own clock, so the molten runs move whether or not a game
	// is stepping behind them.
	float map_clock = 0.f;
	// How much of the room each foe's board is holding, eased toward the
	// aim so a switch is a movement and not a jump.
	static constexpr int kSeats = 6;
	std::array<float, kSeats> room_focus{};
	// The curse the climb just laid, named on the map header until the
	// next ring: it was not chosen, so it has to be told.
	std::string curse_shown;
	// The grade the last climb earned, kept after the run keys are gone so
	// the settlement screen can print it.
	campaign::Verdict last_verdict;
	// Where each foe's board was drawn last frame. A phone has no Tab key,
	// so the aim is moved by touching the board you want buried - which is
	// the gesture anyone would try first anyway.
	std::vector<SDL_Rect> foe_rects;
	// A won map battle is spent: no retry may fight the same node twice.
	bool node_done = false;
	int pick_difficulty = campaign::kMild;
	// A reward hand on the map takes into the run's build, not a session's.
	bool offer_reward = false;
	// A forge or event node being visited: the node index (already spent -
	// entering a stop consumes it, so quitting mid-visit can never farm
	// it), and whether this forge visit has drawn its free hand.
	int visiting = -1;
	bool forge_hand_used = false;
	// A life can be bought back once per forge visit, not farmed.
	bool forge_life_used = false;
	// The oils painted on the map, carried into the battle being launched:
	// consumed by the launch - one coat, one fight.
	bool oil_hot = false;
	bool oil_frost = false;
	// The chaos cards that curse the hands rather than the board, read off
	// the run's build at every launch the way the oils are: Crossed Wires
	// swaps the keys on the way in, the Loose Ratchet sends one turn too
	// many every third time. The sim never hears about either - it receives
	// whatever the cursed hands actually asked for.
	bool wires_crossed = false;
	bool ratchet_loose = false;
	int ratchet_turns = 0;
	bool tongs_sticky = false;
	int tongs_holds = 0;
	// The seen-not-simmed stage gimmicks, read from the recipe at launch:
	// dim lights only a lantern around the piece (the lantern glides after
	// it), fog smokes the queue over past the first piece.
	bool stage_dim = false;
	bool stage_fog = false;
	float lantern_x = 0.f;
	float lantern_y = 0.f;
	// How much garbage stood on the board last frame, so a survival rise
	// lands as an event - a shudder and a thud - not a silent shift.
	int last_garbage_cells = 0;
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
	// Shards: the other half of the vocabulary. A spark is a soft round
	// glow and everything in the game was made of them - a tetris, a spin
	// and a shattering row all read as the same puff of coloured dust in
	// three different colours. A shard is angular, it spins, and it falls
	// hard, which is what breaking looks like and what burning does not.
	struct Shard {
		float x = 0.f, y = 0.f, vx = 0.f, vy = 0.f;
		float turn = 0.f, spin = 0.f, size = 4.f;
		int life = 0;
		SDL_Color color{255, 255, 255, 255};
	};
	std::array<Shard, 192> shards{};
	size_t shard_at = 0;
	// Rings: an expanding circle from a point. A spin is a rotation and a
	// perfect clear is a wave, and neither of them is dust.
	struct Ring {
		float x = 0.f, y = 0.f, span = 0.f;
		int life = 0, born = 1;
		SDL_Color color{255, 255, 255, 255};
	};
	std::array<Ring, 12> rings{};
	size_t ring_at = 0;
	// A column of the well going bright: the shape a quad takes, so four
	// rows read as the well itself being punched through rather than as
	// more of the dust every single clear already throws.
	struct Beam {
		int left = 0, wide = 0, top = 0, tall = 0;
		int life = 0, born = 1;
		SDL_Color color{255, 255, 255, 255};
	};
	std::array<Beam, 4> beams{};
	size_t beam_at = 0;
	// Last frame's queued garbage, so the frame it rises can be caught.
	int was_pending = 0;
	// Which rows were frozen last frame, so the frame a row stops being
	// iron can be caught and shattered. Read from the board rather than
	// asked of the sim: the graded engine says nothing about this and does
	// not need to.
	std::array<bool, kHeight> was_iron{};
	long shake_until = -1;
	// The lock pulse: the piece that just landed flashes for a beat, so
	// every placement has weight even when nothing clears.
	Piece lock_piece{};
	long lock_flash = -1000;
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
	// A boss skill landing: the frames left of its full-screen flash, and
	// the ink it flashes in. Overdrive gets one of these when the player
	// earns it; the boss gets one when it takes something away.
	int skill_flash = 0;
	SDL_Color skill_ink{255, 96, 60, 255};
	// Rows going white-hot as they clear: the row, and the frames left of
	// its burn. Kept here so an instant clear - where the row is spliced
	// out on the lock frame - still gets its moment.
	struct BurnRow {
		int row = 0;
		int life = 0;
		// A row that shattered rather than burned: drawn cold and white
		// instead of white-hot, because ice is not fire.
		bool cold = false;
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

// The one owner of the mode-to-name mapping. The names are the modes'
// identities now, not a ruleset's: the board plays pure everywhere and
// the fuse is the Forge's own gimmick, so every new record keys on the
// mode alone. The old trainer strings (free/timed/arcade/...) survive
// only inside files already written, read back as-is.
const char* gametype_name (int mode) {
	switch (mode) {
		case 1: return "blaze";
		case 2: return "inferno";
		case 3: return "meltdown";
		case 4: return "bunker";
		case 5: return "duel";
		default: return "ignition";
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

// The ruleset block on a recording: which halves were up - the fuse, the
// Flow rail, or both - and every number they were played under. One
// writer, because a file that names a ruleset without saying under which
// numbers cannot be read back honestly, and a campaign stage starts from
// rules the settings screen never saw.
void stamp_fuse (replay::Meta& meta, const SimConfig& rules) {
	meta.fuse = rules.fuse;
	meta.flow = rules.flow_rail;
	if (!rules.fuse && !rules.flow_rail) {
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
	const SimConfig rules = config.sim();
	meta.forced_delay = rules.forced_delay;
	meta.finesse = config.finesse_rule;
	meta.spinrule = rules.spin_rule;
	meta.cleartype = rules.cleartype;
	meta.das = config.das;
	meta.arr = config.arr;
	meta.dcd = config.dcd;
	meta.sdf = config.sdf;
	meta.are = config.are;
	// The rules are the game's own now; the file still says them in full,
	// so a record from any build reads back honestly.
	meta.gametype = gametype_name(mode);
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
	app.skill_flash = 0;
	for (App::BurnRow& row : app.burn_rows) {
		row.life = 0;
	}
	app.burn_seen_lock = -1;
	app.was_overdrive = false;
	app.was_pressured = false;
	// The watchers start from a clean slate, or the first frame of a new
	// board reads last game's leftovers as a shatter and a flood.
	app.was_iron.fill(false);
	app.was_pending = 0;
	for (App::Spark& spark : app.sparks) {
		spark.life = 0;
	}
	for (App::Shard& shard : app.shards) {
		shard.life = 0;
	}
	for (App::Ring& ring : app.rings) {
		ring.life = 0;
	}
	for (App::Beam& beam : app.beams) {
		beam.life = 0;
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
	// No campaign build here, so no curse either: a chaos card taken on the
	// road must never follow the hands into the Training Yard.
	app.wires_crossed = false;
	app.ratchet_loose = false;
	app.ratchet_turns = 0;
	app.tongs_sticky = false;
	app.tongs_holds = 0;
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
	if (mode == 1) {
		// Blaze burns three minutes, not the old trainer's five. Its own
		// table keeps its scores, so the clock competes only with itself.
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
	app.session.emplace(config, seed, meta);
	app.screen = Screen::Game;
	app.paused = false;
	app.editing = false;
	app.place_panels = true;
	app.hiscore_place = -1;
	app.score_saved = false;
	app.countdown = app.start_delay;
	app.stage_dim = false;
	app.stage_fog = false;
	app.last_garbage_cells = 0;
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
// The player's own brands land on the foe at every round start: a
// frostbrand freezes the foe's iron so its clears take an extra beat,
// and hobnails open the round with rust already headed for its floor.
// Run cards only - a trainer duel carries no build - and read from the
// run itself, because the duel screen scrubs the display list.
void apply_brands (App& app) {
	if (app.campaign_stage < 0 || !app.versus.has_value()
		|| !app.versus->armed()) {
		return;
	}
	const std::vector<std::string>& worn = app.campaign.run.tempers;
	const auto has = [&worn] (const char* id) {
		return std::find(worn.begin(), worn.end(), std::string(id))
			!= worn.end();
	};
	if (has("frostbrand") || app.oil_frost) {
		for (auto& foe : app.versus->foes) {
			foe->sim.sim_mutable().impose_gimmick(0, true);
		}
	}
	if (has("hobnails")) {
		for (auto& foe : app.versus->foes) {
			foe->sim.receive_attack(2);
		}
	}
}

void deal_versus_round (App& app) {
	// A duel never drafts, so the temper state is scrubbed rather than
	// re-dealt: a leftover solo build would print on the pause screen, and
	// leftover offers would freeze the match under a card nobody can see.
	app.tempers.clear();
	app.offers.clear();
	const unsigned seed = app.seeds();
	// The rules this round is played under were fixed by start_versus (a
	// campaign duel overrides them), so the recording is stamped from the
	// config actually in force.
	replay::Meta meta = meta_for(app.config, 5);
	stamp_fuse(meta, app.temper_start);
	app.session.emplace(app.temper_start, seed, meta);
	// The bot arrives armed rather than drafting: its blade is the round's
	// identity, the same fight every time this rank is fought. A campaign
	// boss overrides both its base rules and its blade.
	app.versus->begin_round(app.seeds(), meta, app.versus_bot_base,
		app.versus_blade);
	apply_brands(app);
	app.countdown = app.start_delay;
	app.stage_dim = false;
	app.stage_fog = false;
	app.last_garbage_cells = 0;
	reset_effects(app);
}

void start_versus (App& app, int career_stage = -1) {
	app.mode = 5;
	app.career_stage = career_stage;
	app.campaign_stage = -1;
	app.career_od = false;
	app.campaign_od = false;
	app.daily_run = false;
	// A yard duel carries no campaign build, and so no curse.
	app.wires_crossed = false;
	app.ratchet_loose = false;
	app.ratchet_turns = 0;
	app.tongs_sticky = false;
	app.tongs_holds = 0;
	save_config(app.config, app.config_file);
	SimConfig config = app.config.sim();
	config.gametype = 5;
	config.cheese_holes = 1;
	config.cheese_messiness = 30;
	int rank = app.config.bot_rank;
	int first_to = app.config.first_to;
	if (career_stage >= 0) {
		// A ladder stage names its own terms: the stage's rank, and longer
		// matches up top. The rungs climb the bot alone - the board plays
		// pure here like everywhere off the Forge.
		rank = career_stage;
		first_to = career_stage >= 4 ? 2 : 1;
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
		&& app.versus->armed()) {
		if (auto other = app.versus->lead().sim.finish(true)) {
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
		// Chaos gets the one hue nothing else in the forge owns - a cold
		// violet against all that iron and ember, so a card that breaks
		// something is never mistaken for one that only helps.
		case temper::Family::Chaos: return IM_COL32(196, 122, 255, 255);
		// The ward is the one cold colour on the table - slate, the shade
		// of iron that has been let alone to cool. Nothing that defends
		// should look like fire.
		case temper::Family::Ward: return IM_COL32(120, 150, 186, 255);
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
		case temper::Family::Chaos: return "CHAOS";
		case temper::Family::Ward: return "WARD";
		case temper::Family::Fuel:
		default: return "FUEL";
	}
}

// The family's glyph, drawn in primitives the way the mode buttons draw
// theirs: a flame for fuel, a bolt for Flow, a blade for risk, a scroll
// bar for the rule cards, a shield for the wards. Small on purpose - it is a stamp, not an icon
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
		case temper::Family::Chaos:
			// A knot: two bars crossed over each other, the plainest way to
			// draw "this is not what you think it is".
			draw->AddLine(ImVec2(at.x + r * 0.3f, at.y + r * 0.3f),
				ImVec2(at.x + size - r * 0.3f, at.y + size - r * 0.3f),
				ink, std::max(1.f, r * 0.34f));
			draw->AddLine(ImVec2(at.x + size - r * 0.3f, at.y + r * 0.3f),
				ImVec2(at.x + r * 0.3f, at.y + size - r * 0.3f),
				ink, std::max(1.f, r * 0.34f));
			break;
		case temper::Family::Ward:
			// A shield: a straight shoulder over a point, the same two
			// strokes as the blade turned to the other purpose.
			draw->AddRectFilled(ImVec2(mid.x - r * 0.72f, at.y + r * 0.25f),
				ImVec2(mid.x + r * 0.72f, mid.y + r * 0.15f), ink);
			draw->AddTriangleFilled(
				ImVec2(mid.x - r * 0.72f, mid.y + r * 0.12f),
				ImVec2(mid.x + r * 0.72f, mid.y + r * 0.12f),
				ImVec2(mid.x, at.y + size - r * 0.15f), ink);
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
// machinery every ordinary game uses, plus the things a recipe can ask
// for that no ordinary game does: a preset board, a fuse that burns hot
// from the first frame, and the Anvil's permanent metal on the player's
// side only. A battle launched from the map (run_node >= 0) also carries
// the run's build: every temper picked on the climb, forged into the
// player's rules before the first piece falls - a stage never drafts
// mid-game, so this is the only door the build comes through.
void start_stage (App& app, int index, int run_node = -1) {
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
	app.run_node = run_node;
	app.run_ended = false;
	app.node_done = false;
	app.visiting = -1;
	app.mode = stage.mode;
	// The recipe's seen-not-simmed gimmicks, and the lantern starting over
	// the spawn point so a dark stage does not open on a black well.
	app.stage_dim = stage.dim;
	app.stage_fog = stage.fog;
	app.lantern_x = kBoardX + (kSpawnX + 0.5f) * kCell;
	app.lantern_y = kBoardY + 2.5f * kCell;
	app.last_garbage_cells = 0;
	app.offers.clear();
	app.offer_reward = false;
	app.offer_at = 0;
	app.offer_shown = 0;
	app.heat = 0;
	app.ember_spent = 0;
	app.offer_salt = 0;
	app.extra_picks = 0;
	app.offer_taken = false;
	app.ember_bonus = campaign::ember_bonus_percent(app.campaign.forge);
	// The pause screen's build line reads app.tempers; a map battle shows
	// the run's, a bare stage shows none.
	app.tempers = run_node >= 0
		? app.campaign.run.tempers : std::vector<std::string>{};
	save_config(app.config, app.config_file);

	SimConfig base = app.config.sim();
	base.gametype = stage.mode == 5 ? 5 : stage.mode;
	if (stage.mode == 5) {
		base.cheese_holes = 1;
		base.cheese_messiness = 30;
	}
	SimConfig mine = campaign::stage_config(stage, base, app.campaign.forge);
	app.oil_hot = false;
	app.oil_frost = false;
	app.wires_crossed = false;
	app.ratchet_loose = false;
	app.ratchet_turns = 0;
	app.tongs_sticky = false;
	app.tongs_holds = 0;
	if (run_node >= 0) {
		mine = temper::tempered(mine, app.campaign.run.tempers);
		// The two chaos cards the sim cannot carry: they curse the hands,
		// so the screen holds them for as long as this stage lasts.
		{
			const std::vector<std::string>& worn = app.campaign.run.tempers;
			const auto has = [&worn] (const char* id) {
				return std::find(worn.begin(), worn.end(), std::string(id))
					!= worn.end();
			};
			app.wires_crossed = has("crossed_wires");
			app.ratchet_loose = has("loose_ratchet");
			app.tongs_sticky = has("sticky_tongs");
		}
		if (app.campaign.run.endless) {
			// The climb's tightening, on top of everything else - the room
			// rules, then the flood's own weight, which is the dial that
			// keeps climbing after the rest have bottomed out.
			mine = campaign::endless_scaled(mine, app.campaign.run.ring);
			mine = campaign::endless_press(mine, app.campaign.run.ring);
		}
		// The oils spend themselves as the doors close: hot lands on this
		// config, frost is held for the duel wiring, and both are struck
		// off the run and saved - one coat, one fight.
		campaign::Run& oiled = app.campaign.run;
		if (!oiled.oils.empty()) {
			for (const std::string& oil : oiled.oils) {
				app.oil_hot = app.oil_hot || oil == "hot";
				app.oil_frost = app.oil_frost || oil == "frost";
			}
			oiled.oils.clear();
			campaign::save(campaign::path(app.root), app.campaign);
		}
		if (app.oil_hot) {
			mine.attack_scale += 0.5;
		}
	}
	const unsigned seed = app.seeds();
	app.temper_seed = seed;
	app.temper_start = mine;
	replay::Meta meta = meta_for(app.config, stage.mode);
	stamp_fuse(meta, mine);
	// Its own name in every record: fuse_table_for has no fall-through, so
	// an Anvil-boosted score can never reach an ordinary table even if the
	// probe gate below were lost.
	meta.gametype = "campaign";
	meta.tempers = app.tempers;

	if (stage.mode == 5) {
		app.versus_bot_base = campaign::bot_config(stage, base);
		int rank = stage.rank;
		if (run_node >= 0) {
			// The fire picked at the door is the whole run's difficulty,
			// so it moves the foe as well as the price of dying: a rung
			// down on mild, a rung up at white heat.
			rank = campaign::rank_for(rank, app.campaign.run.difficulty);
		}
		if (run_node >= 0 && app.campaign.run.endless) {
			// The climb tightens the foe's side too: its board obeys the
			// same squeeze, and its rank climbs half a rung a ring.
			app.versus_bot_base = campaign::endless_scaled(
				app.versus_bot_base, app.campaign.run.ring);
			// The steel first, off the rank it is about to be given: past
			// the ladder's top there is no rung left, so the promotion is
			// paid in what it sends instead.
			app.versus_bot_base = campaign::endless_edge(
				app.versus_bot_base, rank, app.campaign.run.ring);
			rank = campaign::endless_rank(rank, app.campaign.run.ring);
		}
		app.versus_blade = campaign::blade_of(stage);
		app.session.emplace(mine, seed, meta);
		app.versus.emplace(rank, stage.first_to);
		// A campaign boss fights with its own kit: telegraphed skills the
		// trainer's plain duels never carry.
		app.versus->arm_skills(stage.id);
		// How hard that kit lands: the recipe writes the blow, the fire
		// chosen at the door writes what it is worth, and a climb keeps
		// raising it ring by ring.
		app.versus->skill_scale = run_node >= 0
			? campaign::skill_scale(app.campaign.run.difficulty,
				app.campaign.run.endless, app.campaign.run.ring)
			: campaign::skill_scale(campaign::kForged, false, 0);
		app.versus->foe_name = stage.name;
		if (stage.raid != nullptr) {
			// A raid walks its rank list one foe per round - every one of
			// them moved by the run's fire the way a lone boss is.
			const int fire = run_node >= 0
				? app.campaign.run.difficulty : campaign::kForged;
			int listed = 0;
			for (const char* c = stage.raid; *c != '\0'; ++c) {
				if (*c >= '0' && *c <= '9') {
					listed = listed * 10 + (*c - '0');
				} else if (*c == ',') {
					app.versus->raid_ranks.push_back(
						campaign::rank_for(listed, fire));
					listed = 0;
				}
			}
			app.versus->raid_ranks.push_back(
				campaign::rank_for(listed, fire));
		}
		app.versus->begin_round(app.seeds(), meta, app.versus_bot_base,
			app.versus_blade);
		apply_brands(app);
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
// spent. Derived rather than accumulated, so it cannot drift. A reward
// hand on the map spends the run's banked embers instead - the climb's
// purse, not a battle's.
// What the smith charges this run, right now. Every ember price on every
// screen goes through here, so the number the button prints and the number
// the till takes can never drift apart.
// Whether this run has asked for the challenge tier. White heat only -
// one death and done - which is also every ring of the Endless Climb, so
// the climb is the one place the chaos cards turn up as a matter of
// course. Asked in one place so no screen can open the tier by accident.
int ember_price (const App& app, int base) {
	return campaign::priced(base, app.campaign.run);
}

int ember_balance (const App& app) {
	if (app.offer_reward) {
		return app.campaign.run.embers;
	}
	if (!app.session.has_value()) {
		return 0;
	}
	const Sim& sim = app.session->sim();
	const int earned = temper::embers_of(sim.lines_cleared(),
		sim.attack_sent());
	return earned + earned * app.ember_bonus / 100 - app.ember_spent;
}

// The spoils: three cards on the map after a won battle, one taken into
// the run - or none, the climb does not insist. The hand is a pure
// function of the run's seed and height, so a reroll can be paid for and
// the same climb re-deals the same spoils.
void deal_reward (App& app) {
	campaign::Run& run = app.campaign.run;
	if (!run.active) {
		return;
	}
	app.map_reward = false;
	app.tempers = run.tempers;
	app.temper_seed = run.seed
		^ (0x9e3779b9u * static_cast<unsigned>(run.depth + 1));
	app.heat = run.depth;
	app.offer_salt = 0;
	app.extra_picks = 0;
	app.offer_taken = false;
	app.offers = temper::offer(app.temper_seed, app.heat, run.tempers);
	app.offer_at = 0;
	app.offer_shown = 0;
	app.offer_reward = !app.offers.empty();
	if (app.offer_reward) {
		app.audio.play("overdrive");
	}
}

// A run put down, willingly or not: the leftover embers render down to
// slag - scaled by the difficulty, the prestige - and the keys leave the
// file. The stars already written stay written; they are the door to the
// next chapter.
void end_run (App& app, bool won) {
	campaign::Run& run = app.campaign.run;
	if (!run.active) {
		return;
	}
	// The grade, taken before the keys are cleared: it is the only thing
	// the climb leaves behind besides slag, and the run is about to stop
	// existing.
	app.last_verdict = campaign::grade_run(run, won);
	app.campaign.slag += (run.embers / 5)
		* campaign::slag_percent(run.difficulty) / 100;
	run = campaign::Run{};
	app.run_map.clear();
	app.map_reward = false;
	campaign::save(campaign::path(app.root), app.campaign);
}

// A new climb: the run keys written, the map stood up from its two
// numbers, and - if the Anvil's Preheat is bought - the first spoils
// dealt free at the door.
void begin_run (App& app, int chapter, int difficulty, unsigned seed,
		bool endless = false) {
	campaign::Run& run = app.campaign.run;
	run = campaign::Run{};
	run.active = true;
	run.chapter = chapter;
	run.seed = seed;
	// The Endless Climb is always white heat: one death ends it, the
	// prestige pays double, and a mid-fight R is the same surrender it
	// is anywhere at that heat.
	run.endless = endless;
	run.difficulty = endless ? campaign::kWhite : difficulty;
	// The Anvil's send-off: Forged Lifeblood adds a life (felt only on
	// forged fire), and the War Chest puts embers in the purse before the
	// first fight.
	const auto level = [&app] (const char* id) {
		const auto found = app.campaign.forge.find(id);
		return found != app.campaign.forge.end() ? found->second : 0;
	};
	app.curse_shown.clear();
	// The blow that opens a climb. It waits for the map to have drawn at
	// least once (draw_forge_strike checks), so a run resumed from a save
	// gets it too - and nothing about the run is gated on it.
	app.forge_strike = kStrike;
	app.map_seen = false;
	run.lives += level("lifeblood");
	run.embers += 12 * level("warchest");
	app.run_map = endless ? campaign::build_endless_map(0, seed)
		: campaign::build_map(chapter, seed);
	app.run_ended = false;
	app.map_reward = false;
	campaign::save(campaign::path(app.root), app.campaign);
	if (campaign::free_drafts(app.campaign.forge) > 0) {
		deal_reward(app);
	}
}

// Which nodes the climb can take next: the whole entrance row when
// nothing is fought yet, afterwards only what the last node's edges
// reach.
bool node_pickable (const App& app, int node) {
	const campaign::Run& run = app.campaign.run;
	// Not while the maul is still in the air. A run that opens with a blow
	// and can be clicked through before it lands has neither the blow nor
	// the click - and it is under two seconds, which is less than it takes
	// to read the bottom row.
	if (app.forge_strike > 0) {
		return false;
	}
	if (!run.active || node < 0
		|| node >= static_cast<int>(app.run_map.size())
		|| app.run_map[static_cast<size_t>(node)].depth != run.depth) {
		return false;
	}
	if (run.path.empty()) {
		return true;
	}
	const campaign::MapNode& from
		= app.run_map[static_cast<size_t>(run.path.back())];
	return std::find(from.next.begin(), from.next.end(), node)
		!= from.next.end();
}

void start_run_node (App& app, int node) {
	if (node_pickable(app, node)) {
		start_stage(app, app.run_map[static_cast<size_t>(node)].stage, node);
	}
}

// One card of choice at an event node. Every effect is instant - nothing
// lingers past the visit, so the save never needs a "next battle" field -
// and which event waits at a node is a pure function of the run's seed,
// the same promise the map itself makes.
struct MapEvent {
	const char* name;
	const char* text;
	const char* deed;   // The accept button's word.
};
const MapEvent kMapEvents[4] = {
	{"The Scrap Dealer",
		"A cart of old iron, and a hand that pays in embers.\n"
		"Sell what you are not carrying anyway.", "Sell (+14 embers)"},
	{"The Tithe",
		"A slot in the wall, old as the forge. What goes in\n"
		"does not come back - but the metal remembers.",
		"Pay 10 embers (+8 slag)"},
	{"A Stray Spark",
		"Something glowing in the ash, still alive. It will\n"
		"jump to whoever reaches first.", "Take it"},
	{"The Quench Trough",
		"Cold water, deep enough to unmake a temper. The last\n"
		"one you picked would go quietly.", "Quench it"},
};

int event_of (const App& app, int node) {
	return static_cast<int>((app.campaign.run.seed
		^ (0x9e3779b9u * static_cast<unsigned>(node + 1))) % 4);
}

void apply_event (App& app, int id) {
	campaign::Run& run = app.campaign.run;
	switch (id) {
		case 0:
			run.embers += 14;
			app.audio.play("hold");
			break;
		case 1:
			if (run.embers >= 10) {
				run.embers -= 10;
				app.campaign.slag += 8;
				app.audio.play("b2b");
			}
			break;
		case 2: {
			// The spark is whatever the forge would have offered here; a
			// dry pool pays embers instead of nothing.
			const std::vector<std::string> hand = temper::offer(
				run.seed ^ 0x27d4eb2fu, run.depth, run.tempers);
			if (!hand.empty()) {
				run.tempers.push_back(hand.front());
				app.tempers = run.tempers;
				app.audio.play("b2b");
			} else {
				run.embers += 6;
			}
			break;
		}
		case 3:
			if (!run.tempers.empty()) {
				run.tempers.pop_back();
				app.tempers = run.tempers;
				app.audio.play("clear");
			} else {
				run.embers += 4;
			}
			break;
		default:
			break;
	}
}

// A stop on the map, entered: the node is spent on the way in - path and
// depth advance immediately and are saved, so quitting mid-visit can
// never farm a forge twice - and the overlay stays up until Leave.
void enter_node (App& app, int node) {
	if (!node_pickable(app, node) || app.visiting >= 0) {
		return;
	}
	const campaign::MapNode& at = app.run_map[static_cast<size_t>(node)];
	if (at.kind != 2 && at.kind != 3) {
		start_run_node(app, node);
		return;
	}
	campaign::Run& run = app.campaign.run;
	run.path.push_back(node);
	run.depth += 1;
	app.visiting = node;
	app.forge_hand_used = false;
	app.forge_life_used = false;
	campaign::save(campaign::path(app.root), app.campaign);
	app.audio.play("hold");
}

void leave_visit (App& app) {
	app.visiting = -1;
	app.forge_hand_used = false;
	campaign::save(campaign::path(app.root), app.campaign);
}

// The retry, with the map in mind: a stage on a living run restarts as
// the same node with the same build. A node already won cannot be fought
// twice, and a run that just ended has no board to go back to - both
// retries walk to the map instead.
void restart_stage (App& app) {
	if (app.run_ended || app.node_done) {
		app.run_ended = false;
		app.node_done = false;
		app.campaign_stage = -1;
		app.run_node = -1;
		app.versus.reset();
		app.screen = Screen::Career;
		return;
	}
	// Above mild, a mid-fight restart is a surrender and costs what a
	// death costs - otherwise R is a free undo and the lives mean
	// nothing. A retry from the loss screen has already paid at the
	// settlement, so only a fight still running is charged.
	campaign::Run& run = app.campaign.run;
	if (app.screen == Screen::Game && run.active && app.run_node >= 0
		&& run.difficulty != campaign::kMild) {
		// A surrender is a death, and the grade counts it as one.
		++run.deaths;
		if (app.session.has_value()) {
			run.seconds += static_cast<int>(app.session->sim().frame() / 50);
		}
		if (run.difficulty == campaign::kWhite || --run.lives <= 0) {
			end_run(app, false);
			app.run_ended = false;
			app.campaign_stage = -1;
			app.run_node = -1;
			app.versus.reset();
			app.paused = false;
			app.screen = Screen::Career;
			return;
		}
		campaign::save(campaign::path(app.root), app.campaign);
	}
	start_stage(app, app.campaign_stage, app.run_node);
}

// A heat has been forged if the run's counter has crossed another rung -
// lines everywhere, dug rows in Meltdown. Put three cards on the table when
// it has; the loop stops stepping the sim while they are there, so the fuse
// waits with the player. Only a fuse-rules game off the Forge Road would
// draft mid-game, and none is left - the guards keep the machinery honest
// for whatever burn room ever wants it back.
void offer_tempers (App& app) {
	if (!app.session.has_value() || !app.session->sim().config().fuse) {
		return;
	}
	if (app.campaign_stage >= 0) {
		// A stage never drafts mid-game: the map hands out its cards
		// between battles, and the board on the Forge Road never stops.
		// The heats still tighten the fuse - that is the sim's own.
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
		// The pool ran dry - fifty-six stacks is all there is - and a draft
		// with nothing to draft must not stop the game.
		app.heat = forged;
		return;
	}
	app.audio.play("overdrive");
}

// One card taken: the run's rules are rebuilt from the start plus every
// temper in order, which is what makes a stack of the same card add up
// rather than each one overwrite the last. A reward hand on the map has
// no board to retune - the card goes into the run's build, where the next
// battle's start_stage forges it in.
void take_temper (App& app, int at) {
	if (at < 0 || at >= static_cast<int>(app.offers.size())) {
		return;
	}
	if (app.offer_reward) {
		campaign::Run& run = app.campaign.run;
		run.tempers.push_back(app.offers[at]);
		app.tempers = run.tempers;
		app.audio.play("b2b");
		if (app.extra_picks > 0 && app.offers.size() > 1) {
			--app.extra_picks;
			app.offer_taken = true;
			app.offers.erase(app.offers.begin() + at);
			app.offer_at = std::min(app.offer_at,
				static_cast<int>(app.offers.size()) - 1);
			return;
		}
		app.offers.clear();
		app.offer_reward = false;
		app.offer_shown = 0;
		app.offer_taken = false;
		app.extra_picks = 0;
		campaign::save(campaign::path(app.root), app.campaign);
		return;
	}
	if (!app.session.has_value()) {
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

// The spoils declined: the map does not insist. Nothing is banked for it -
// passing is a choice, not a saving - but the climb moves on unslowed.
void skip_reward (App& app) {
	if (!app.offer_reward) {
		return;
	}
	// Walking past the spoils untaken pays a small solace: skipping is a
	// real choice now, not just a refusal.
	if (app.campaign.run.active) {
		app.campaign.run.embers += ember_price(app, temper::kSkipSolace);
	}
	app.offers.clear();
	app.offer_reward = false;
	app.offer_shown = 0;
	app.offer_taken = false;
	app.extra_picks = 0;
	campaign::save(campaign::path(app.root), app.campaign);
}

// The two things the coin buys, both on the draft screen: the same heat
// dealt again, and a second card off the same table.
void reroll_offer (App& app) {
	const int cost = ember_price(app, temper::kRerollCost);
	if (app.offer_taken || ember_balance(app) < cost) {
		return;
	}
	if (app.offer_reward) {
		app.campaign.run.embers -= cost;
	} else {
		app.ember_spent += cost;
	}
	app.offers = temper::offer(app.temper_seed, app.heat, app.tempers,
		++app.offer_salt);
	app.offer_at = 0;
	app.audio.play("rotate");
}

void buy_extra_pick (App& app) {
	const int cost = ember_price(app, temper::kExtraPickCost);
	if (app.extra_picks > 0 || app.offers.size() < 2
		|| ember_balance(app) < cost) {
		return;
	}
	if (app.offer_reward) {
		app.campaign.run.embers -= cost;
	} else {
		app.ember_spent += cost;
	}
	app.extra_picks = 1;
	app.audio.play("hold");
}

void end_game (App& app) {
	// Saved rather than offered, the way the Python game does it: the moment
	// a run ends is the worst moment to ask someone whether they will want
	// to look at it.
	//
	// The well goes out under the verdict rather than the board simply
	// stopping - half a second of cooling, which is the pause a verdict
	// needs to land in. The board stays drawn underneath the whole time: a
	// loss screen over a well that vanished reads as a crash.
	app.cool_down = kCooling;
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
	// A battle fought on the map settles the run too: the battle's embers
	// bank into the climb on a win, the path climbs a row, and a death
	// costs what the difficulty says a death costs.
	if (app.campaign_stage >= 0) {
		const campaign::Stage& stage
			= campaign::stages()[static_cast<size_t>(app.campaign_stage)];
		bool won = false;
		int stars = 0;
		if (stage.mode == 5 && app.versus.has_value()) {
			won = app.versus->player_wins > app.versus->bot_wins;
			const bool sweep = won && app.versus->bot_wins == 0;
			stars = campaign::boss_stars(won, sweep, app.campaign_od);
		} else if (app.session.has_value()
			&& stage.survive_seconds > 0) {
			// A watch's stars: the clock is fixed, so the marks are what
			// was cleared while holding on.
			const Sim& sim = app.session->sim();
			won = sim.won();
			stars = campaign::survive_stars(won, sim.lines_cleared(),
				stage.quota);
		} else if (app.session.has_value()) {
			const Sim& sim = app.session->sim();
			won = sim.won();
			stars = campaign::solo_stars(won, sim.frame() * 0.02,
				stage.par_seconds, app.session->forced(),
				sim.config().fuse);
		}
		const auto held = app.campaign.stars.find(stage.id);
		const bool first_clear
			= held == app.campaign.stars.end() || held->second == 0;
		campaign::Run& run = app.campaign.run;
		const bool on_map = run.active && app.run_node >= 0
			&& app.run_node < static_cast<int>(app.run_map.size());
		const int scale = on_map ? campaign::slag_percent(run.difficulty)
			: 100;
		const int slag = campaign::slag_award(stage, first_clear, won, stars,
			ember_balance(app)) * scale / 100;
		app.campaign.slag += slag;
		if (stars > app.campaign.stars[stage.id]) {
			app.campaign.stars[stage.id] = stars;
		}
		app.last_stage_stars = stars;
		app.last_slag_gain = slag;
		app.run_ended = false;
		if (on_map) {
			// What the climb has cost so far, for the grade it earns when
			// it ends. Battle seconds, not wall clock - a player reading
			// the map is not spending the run.
			if (app.session.has_value()) {
				run.seconds += static_cast<int>(
					app.session->sim().frame() / 50);
			}
			if (!won) {
				++run.deaths;
			}
			if (won) {
				// The battle's unspent embers bank into the climb, and the
				// path takes the node. The boss row banks nothing to spend -
				// end_run renders whatever is left down to slag.
				app.node_done = true;
				run.embers += ember_balance(app);
				run.path.push_back(app.run_node);
				run.depth += 1;
				if (run.endless) {
					// The record moves with every node taken - written now,
					// so a death or a walk-away later never loses it.
					app.campaign.endless_best = std::max(
						app.campaign.endless_best,
						campaign::endless_rows(run));
				}
				if (run.depth >= campaign::kMapDepth) {
					if (run.endless) {
						// The ring is climbed, not the run: the next one
						// stands up on the spot, with the gatekeeper's
						// spoils dealt between the floors.
						run.ring += 1;
						run.depth = 0;
						run.path.clear();
						// Every second ring the climb takes something back.
						// The build only ever grew before - a card a node,
						// forever - while the fights ran out of rungs to
						// climb, and one bag of the right shape ended every
						// room after that. A curse is the counterweight:
						// it is not offered, it is laid, and it lands in
						// the same list the cards do so everything that
						// already reads a build reads it too.
						const int want = temper::curses_by(run.ring);
						for (int at = 0; at < want; ++at) {
							const std::string curse
								= temper::curse_at(run.seed, at);
							if (curse.empty()
								|| std::find(run.tempers.begin(),
									run.tempers.end(), curse)
									!= run.tempers.end()) {
								continue;
							}
							run.tempers.push_back(curse);
							app.curse_shown = curse;
						}
						app.run_map = campaign::build_endless_map(
							run.ring, run.seed);
						app.map_reward = true;
					} else {
						end_run(app, true);
						app.run_ended = true;
					}
				} else {
					app.map_reward = true;
				}
			} else if (run.difficulty == campaign::kWhite
				|| (run.difficulty == campaign::kForged
					&& --run.lives <= 0)) {
				// White heat breaks on any death; a forged run breaks when
				// the lives run out. Either way the climb is over and the
				// leftover embers render down - the prestige.
				end_run(app, false);
				app.run_ended = true;
			}
			// A mild death changes nothing: the same node is still open
			// on the map, and the retry costs only the walk back.
		}
		campaign::save(campaign::path(app.root), app.campaign);
	}
	// Would this run make the table? The probe carries the raw clock value,
	// exactly as eval_loss probes it - the conversion to stored centiseconds
	// only happens if a name is entered and the score actually submitted.
	// The loss-time counters: eval_loss probes before a still-resolving
	// clear lands its points, so the snapshot does too.
	if (app.campaign_stage >= 0) {
		// A stage never enters a table: its rules carry the Anvil's metal,
		// and its record already says "campaign" - a name no table owns.
		app.hiscore_place = -1;
	} else {
		// Every Training Yard mode competes in its own named table. The
		// trainer file - the Python game's, three tables, byte-compatible -
		// is history now: still read, never written.
		const Sim& sim = app.session->sim();
		hiscore::Entry probe;
		probe.score = static_cast<std::uint64_t>(
			std::max<long long>(0, sim.final_score()));
		probe.lines = static_cast<std::uint32_t>(std::max(0, sim.final_lines()));
		probe.timer = static_cast<std::uint32_t>(std::max(0L, sim.timer_ms()));
		const int at = hiscore::place_fuse(
			hiscore::load_fuse(hiscore::folder(app.root)),
			gametype_name(app.mode), probe);
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

// Every press and release the player makes, on its way to the sim - and the
// one place the run's cursed hands are honoured, so the keyboard and the
// touch buttons are cursed alike and neither can leave a key stuck (the
// same translation runs on both edges, and the cards cannot change hands
// mid-stage). The sim itself never learns of any of this: it is handed the
// keys it is asked for, and the recording is the honest journey they made.
void player_key (App& app, Key key, bool down) {
	if (!app.session.has_value()) {
		return;
	}
	if (app.wires_crossed) {
		key = crossed(key);
	}
	// A boss can take the tongs away outright for a few seconds. Like the
	// sticky tongs it swallows the press and never the release, because
	// hold is edge-triggered and an orphan release is harmless.
	if (down && key == Key::Hold && app.versus.has_value()
		&& app.versus->imposed_hold_bar) {
		return;
	}
	if (down && app.tongs_sticky && sticks(app.tongs_holds, key)) {
		// The press never reaches the board. Only the press: hold is
		// edge-triggered, so the release behind it is already a no-op,
		// and swallowing that too would be the one way this could leave
		// a key stuck.
		return;
	}
	if (down && app.ratchet_loose && overshoots(app.ratchet_turns, key)) {
		// The overshoot rides in as a whole extra tap ahead of the real
		// press, so the turn goes one further and the release still
		// matches the key that made it.
		app.session->key(key, true);
		app.session->key(key, false);
	}
	app.session->key(key, down);
}

// Lift everything the board still thinks is held. The sim tracks a key as
// down until a release arrives, so any moment the window can stop hearing
// releases - alt-tab, a phone call, the task switcher - has to end with
// every key let go, or the player comes back to a piece already running.
void release_all_keys (App& app) {
	if (!app.session.has_value()) {
		return;
	}
	for (int at = static_cast<int>(Key::Left);
			at <= static_cast<int>(Key::Flip); ++at) {
		app.session->key(static_cast<Key>(at), false);
	}
}

void handle_event (App& app, const SDL_Event& event) {
	ImGui_ImplSDL2_ProcessEvent(&event);
	if (event.type == SDL_QUIT) {
		app.quit = true;
		return;
	}
	// Focus gone, keys gone. Bypasses player_key deliberately: the cursed
	// hands rewrite which key a press means, and a blanket release wants
	// to reach every key the sim could be holding, crossed or not.
	if (event.type == SDL_WINDOWEVENT
		&& (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST
			|| event.window.event == SDL_WINDOWEVENT_MINIMIZED
			|| event.window.event == SDL_WINDOWEVENT_LEAVE)) {
		release_all_keys(app);
		app.input_nudge = true;
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
			// Touching a foe's board aims at it. Tried before the play
			// buttons because the two never overlap and this is the more
			// specific target.
			if (app.versus.has_value() && app.versus->foes.size() > 1) {
				for (size_t i = 0; i < app.foe_rects.size()
					&& i < app.versus->foes.size(); ++i) {
					const SDL_Rect& box = app.foe_rects[i];
					if (x >= box.x && x < box.x + box.w
						&& y >= box.y && y < box.y + box.h
						&& !app.versus->foes[i]->down) {
						app.versus->target = static_cast<int>(i);
						app.audio.play("rotate");
						return;
					}
				}
			}
			if (app.screen == Screen::Game && !app.paused && !app.editing
				&& app.countdown <= 0 && app.offers.empty()
				&& app.session.has_value()) {
				for (size_t i = 0; i < app.touch.size(); ++i) {
					const SDL_Rect& rect = app.touch[i].rect;
					if (x >= rect.x && x < rect.x + rect.w
						&& y >= rect.y && y < rect.y + rect.h) {
						app.touch_held[event.tfinger.fingerId] = i;
						player_key(app, app.touch[i].key, true);
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
					player_key(app, app.touch[found->second].key, false);
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
			// A stage restarts as the stage - same node, same run build -
			// or the retry would silently shed the recipe and play a plain
			// game in its clothes.
			restart_stage(app);
		} else if (app.mode == 5) {
			start_versus(app, app.career_stage);
		} else {
			start_game(app, app.mode);
		}
		return;
	}
	// The draft has the keyboard while it is up - in a game, or as the
	// spoils on the map: 1/2/3 takes a card outright, the arrows walk the
	// row and Enter or space takes the one under the cursor. No confirm
	// step - a pick that costs two presses would be two presses too many.
	if (down
		&& (app.screen == Screen::Game || app.screen == Screen::Career)
		&& !app.offers.empty()
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
		// A press is refused here; a RELEASE never is. Everything in this
		// condition can turn true between a key going down and coming back
		// up - ImGui wants the keyboard for a frame, the countdown starts,
		// a panel opens - and a swallowed release leaves the sim holding a
		// key the player let go of. The next press of that key is then a
		// no-op, because it is already down, and the game reads as if it
		// ate the input. Releases can only ever un-stick something, so they
		// are always allowed through to the board that is still there.
		if (!down && app.session.has_value()) {
			if (const auto key = key_for(app.config, event.key.keysym.scancode)) {
				player_key(app, *key, false);
			}
		}
		return;
	}
	// The aim, in a room with more than one thing in it. Tab is the key
	// every game with a target list already uses, and it is not a game key
	// so it cannot collide with a bind.
	if (down && event.key.keysym.scancode == SDL_SCANCODE_TAB
		&& app.versus.has_value() && app.versus->foes.size() > 1) {
		app.versus->aim_next();
		app.audio.play("rotate");
		return;
	}
	if (const auto key = key_for(app.config, event.key.keysym.scancode)) {
		if (kMobile) {
			// A hardware keyboard is talking; the buttons step aside until
			// the next touch.
			app.touch_shown = false;
		}
		player_key(app, *key, down);
		app.input_nudge = true;
	}
}

// --- The board and its trimmings, in plain rectangles. ---------------------

void spawn_sparks_at (App& app, float x, float y, SDL_Color color, int count,
	float kick);
// The rest of the effect vocabulary, declared here because the cues below
// reach for it before the drawing half of the file defines it.
void spawn_shards (App& app, float x, float y, SDL_Color color, int count,
	float kick, float size);
void spawn_swirl (App& app, float x, float y, SDL_Color color, int count,
	float reach, float kick, float size);
void spawn_ring (App& app, float x, float y, SDL_Color color, float span,
	int life);
void spawn_beam (App& app, int left, int wide, int top, int tall,
	SDL_Color color, int life);

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
		IM_COL32(255, 214, 94, static_cast<int>(170 + 60 * beat)), ui(2.5f));

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
		{255, 176, 60, 255}, 15. * lit * beat);
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

// Is this board's Flow gauge live? The fuse brings the rail with it, and
// the rail stands on its own everywhere else - so the glow, the gauge and
// the labels all ask this rather than the fuse.
bool charging (const Sim& sim) {
	return sim.config().fuse || sim.config().flow_rail;
}

// How much trouble this board is in, 0 to 1: a fuse nearly spent, garbage
// massing, a stack near the sky, the other board's Overdrive bearing down.
// The well's glow and the screen's vignette both read from it. A pure room
// has no fuse to spend but every other danger is still real.
double danger_of (const Sim& sim) {
	double danger = 0.;
	if (sim.config().fuse && sim.fuse_total() > 0.
		&& sim.piece_elapsed().has_value()) {
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
	const SDL_Color tint{255, 214, 94, 255};
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
void draw_cell (SDL_Renderer* renderer, int px, int py, SDL_Color c,
	int size = kCell);


// Rubble: the same cast block as everything else, in dead iron, with two
// cracks still glowing through it. Sharing draw_cell rather than drawing
// its own flat rectangles is the point - garbage should read as the same
// material as the player's stack, only cooled and spoiled, and a slab that
// missed the chamfer everything else has just looked unfinished.
void draw_char_cell (SDL_Renderer* renderer, int x, int y, int size = kCell) {
	draw_cell(renderer, x, y, SDL_Color{78, 66, 60, 255}, size);
	const int t = std::max(1, size / 8);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	fill(renderer, x + size / 3, y + t + 1, std::max(1, t / 2),
		size - 2 * t - 2, {196, 82, 34, 150});
	fill(renderer, x + t + 1, y + size / 2, size / 3, std::max(1, t / 2),
		{196, 82, 34, 110});
}

void draw_cell (SDL_Renderer* renderer, int px, int py, SDL_Color c, int size) {
	// A block of poured metal, not a swatch. Five things make it read as an
	// object rather than a coloured square, and all five are cheap fills:
	// a dark seat so neighbours never bleed into one another, a face that
	// is lit at the top and cools toward the foot, a mitred bevel that
	// stops short of the corners the way a real chamfer does, a shadow
	// under the foot, and one small specular where the light catches.
	// Ghost cells arrive with a low alpha and every band keeps it.
	const auto scaled = [&c] (double factor, int lift) {
		return SDL_Color{
			static_cast<Uint8>(std::clamp(c.r * factor + lift, 0., 255.)),
			static_cast<Uint8>(std::clamp(c.g * factor + lift, 0., 255.)),
			static_cast<Uint8>(std::clamp(c.b * factor + lift, 0., 255.)),
			c.a};
	};
	// The seat: a near-black rim in the cell's own hue, so a wall of one
	// colour still reads as many blocks.
	fill(renderer, px, py, size, size, scaled(0.22, 0));
	const int t = std::max(1, size / 7);
	const int in = px + 1;
	const int top = py + 1;
	const int wide = size - 2;
	const int tall = size - 2;
	// The face, poured in bands: hot at the crown, cooling downwards. Four
	// bands is enough to read as a gradient and cheap enough to do for
	// every cell of a full board every frame.
	const int bands = 4;
	for (int band = 0; band < bands; ++band) {
		const int y = top + tall * band / bands;
		const int h = top + tall * (band + 1) / bands - y;
		const double lit = 1.06 - 0.16 * band / std::max(1, bands - 1);
		fill(renderer, in, y, wide, h, scaled(lit, 6 - 4 * band));
	}
	// The chamfer, mitred: each lip stops a thickness short of the corner,
	// which is what keeps the block from looking like a picture frame.
	const SDL_Color lip = scaled(1.34, 40);
	const SDL_Color side = scaled(1.16, 18);
	const SDL_Color foot = scaled(0.46, 0);
	const SDL_Color flank = scaled(0.66, 0);
	fill(renderer, in + t, top, wide - 2 * t, t, lip);
	fill(renderer, in, top + t, t, tall - 2 * t, side);
	fill(renderer, in + t, top + tall - t, wide - 2 * t, t, foot);
	fill(renderer, in + wide - t, top + t, t, tall - 2 * t, flank);
	// One specular, top-left, where the forge light would actually land.
	const int spot = std::max(1, size / 6);
	fill(renderer, in + t, top + t, spot, std::max(1, spot / 2),
		scaled(1.5, 70));
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
	const double charge = charging(sim) ? sim.flow() / 100. : 0.;
	if (charge > 0.02) {
		const float beat = 0.85f + 0.15f * std::sin(sim.frame() * 0.12f);
		draw_glow(app, kBoardX + kBoardW / 2.f, kBoardY + kBoardH / 2.f,
			kBoardW * 2.4f, kBoardH * 1.5f, {255, 122, 46, 255},
			charge * beat * 150.);
	}
	// The crucible. A hairline rectangle was never going to read as a
	// furnace, so the well is built the way the blocks are: an iron frame
	// with a chamfer, a shadow it casts inward, and a floor that is hotter
	// than the sky because the fire is underneath.
	{
		const int wall = px(9);
		const int fx = kBoardX - wall;
		const int fy = kBoardY - wall;
		const int fw = kBoardW + wall * 2;
		const int fh = kBoardH + wall * 2;
		// The frame's own body, lit at the crown and cooling down the
		// flanks - the same light the blocks are lit by.
		fill(renderer, fx, fy, fw, fh, {44, 32, 24, 255});
		const int lip = std::max(1, wall / 3);
		fill(renderer, fx + lip, fy, fw - 2 * lip, lip, {96, 72, 52, 255});
		fill(renderer, fx, fy + lip, lip, fh - 2 * lip, {74, 55, 40, 255});
		fill(renderer, fx + lip, fy + fh - lip, fw - 2 * lip, lip,
			{26, 19, 14, 255});
		fill(renderer, fx + fw - lip, fy + lip, lip, fh - 2 * lip,
			{34, 25, 18, 255});
		// Rivets down both flanks, spaced by the cell so the frame reads
		// at the same rhythm as the board it holds.
		for (int y = kBoardY + kCell; y < kBoardY + kBoardH; y += kCell * 4) {
			const int r = std::max(1, wall / 4);
			fill(renderer, fx + lip + r, y, r * 2, r * 2, {118, 88, 62, 255});
			fill(renderer, fx + fw - lip - r * 3, y, r * 2, r * 2,
				{118, 88, 62, 255});
		}
		// The interior, and the shadow the walls throw onto it.
		fill(renderer, kBoardX, kBoardY, kBoardW, kBoardH, {14, 10, 8, 255});
		const int cast = std::max(1, wall / 2);
		for (int i = 0; i < cast; ++i) {
			const Uint8 a = static_cast<Uint8>(96 - 96 * i / cast);
			fill(renderer, kBoardX, kBoardY + i, kBoardW, 1, {0, 0, 0, a});
			fill(renderer, kBoardX + i, kBoardY, 1, kBoardH, {0, 0, 0, a});
			fill(renderer, kBoardX + kBoardW - 1 - i, kBoardY, 1, kBoardH,
				{0, 0, 0, a});
		}
	}
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
	// Sealed Columns: the walled-off files drawn as riveted steel plate,
	// floor to sky, so the narrowed forge reads at a glance. board.at()
	// reports these cells as wall for the rules; here they get their own
	// dress instead of the garbage char.
	const int sealed = board.sealed();
	for (int x = 0; x < kWidth; ++x) {
		if (!(sealed >> x & 1)) {
			continue;
		}
		const int wx = kBoardX + x * kCell;
		fill(renderer, wx, kBoardY, kCell, kBoardH, {46, 48, 54, 255});
		fill(renderer, wx + 1, kBoardY, std::max(1, kCell / 10), kBoardH,
			{78, 82, 92, 140});
		fill(renderer, wx + kCell - 1 - std::max(1, kCell / 10), kBoardY,
			std::max(1, kCell / 10), kBoardH, {22, 24, 28, 180});
		// Plate seams every third row, a rivet centred on each seam.
		for (int y = 0; y < kHeight; y += 3) {
			const int wy = kBoardY + y * kCell;
			fill(renderer, wx, wy, kCell, std::max(1, kCell / 12),
				{28, 30, 34, 255});
			fill(renderer, wx + kCell / 2 - std::max(1, kCell / 12), wy
				+ kCell / 2, std::max(2, kCell / 6), std::max(2, kCell / 6),
				{96, 100, 110, 200});
		}
	}
	for (int y = 0; y < kHeight; ++y) {
		for (int x = 0; x < kWidth; ++x) {
			if (sealed >> x & 1) {
				continue;
			}
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
	// The lock pulse: the cells that just landed flash and settle in a
	// few frames - weight the eye can feel even when nothing cleared.
	{
		const long age = sim.frame() - app.lock_flash;
		if (age >= 0 && age < 5) {
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			const Uint8 a = static_cast<Uint8>(140 - age * 28);
			for (const Offset cell : cells_of(app.lock_piece)) {
				if (cell.y >= 0) {
					fill(renderer, kBoardX + cell.x * kCell + 1,
						kBoardY + cell.y * kCell + 1, kCell - 2, kCell - 2,
						{255, 240, 220, a});
				}
			}
		}
	}
	// Cold Iron: a frozen row wears a steel-blue sheen and a frost line, so
	// "why did my clear not clear" answers itself on sight.
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	for (int y = 0; y < kHeight; ++y) {
		if (!board.iron_row(y)) {
			continue;
		}
		const int wy = kBoardY + y * kCell;
		fill(renderer, kBoardX, wy, kBoardW, kCell, {150, 190, 230, 84});
		fill(renderer, kBoardX, wy, kBoardW, std::max(1, kCell / 8),
			{222, 240, 255, 150});
		// Frost, grown down from the seam: needles of four different
		// lengths in a fixed cycle, so ice reads as ice and not as a blue
		// highlighter. The pattern is keyed to the row so two frozen rows
		// never look stamped from the same die.
		const int needles[4] = {kCell / 2, kCell / 5, kCell / 3, kCell / 8};
		const int wide = std::max(1, kCell / 10);
		for (int step = 0; step * wide * 3 < kBoardW; ++step) {
			const int deep = needles[(step + y) % 4];
			fill(renderer, kBoardX + step * wide * 3, wy, wide,
				std::max(1, deep), {236, 248, 255, 110});
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
						{255, 176, 60, 255}, 1, 1.6f);
				}
			}
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
	}

	if (app.stage_dim || (app.versus.has_value() && app.versus->imposed_dark)) {
		// The Dark Gallery: only a lantern around the falling piece lights
		// the well. The lantern glides after the piece rather than jumping
		// with it - and when there is no piece (a clear resolving, the
		// countdown) it simply stays where it last stood, so the dark
		// never blinks. Presentation only: the sim under it is unchanged,
		// and the HUD outside the well stays lit.
		if (sim.entry() && sim.piece().form <= 6) {
			float cx = 0.f;
			float cy = 0.f;
			const auto cells = cells_of(sim.piece());
			for (const Offset cell : cells) {
				cx += cell.x + 0.5f;
				cy += cell.y + 0.5f;
			}
			const float tx = kBoardX + cx / kCells * kCell;
			const float ty = kBoardY + cy / kCells * kCell;
			app.lantern_x += (tx - app.lantern_x) * 0.2f;
			app.lantern_y += (ty - app.lantern_y) * 0.2f;
		}
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		const float lit = 3.2f * kCell;    // Full light inside this...
		const float dark = 6.5f * kCell;   // ...and full dark past this.
		for (int y = 0; y < kHeight; ++y) {
			for (int x = 0; x < kWidth; ++x) {
				const float dx
					= kBoardX + (x + 0.5f) * kCell - app.lantern_x;
				const float dy
					= kBoardY + (y + 0.5f) * kCell - app.lantern_y;
				const float span = std::sqrt(dx * dx + dy * dy);
				const float gone
					= std::clamp((span - lit) / (dark - lit), 0.f, 1.f);
				const Uint8 shade = static_cast<Uint8>(gone * 232.f);
				if (shade > 6) {
					fill(renderer, kBoardX + x * kCell,
						kBoardY + y * kCell, kCell, kCell,
						{6, 4, 3, shade});
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
		if ((app.stage_fog
			|| (app.versus.has_value() && app.versus->imposed_fog))
			&& slot > 0) {
			// Smoke in the Rafters: everything past the first piece is
			// smoked over - the slot stands, its contents do not read.
			const int size = px(18) - shrink / 6;
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
			for (int cell = 0; cell < 3; ++cell) {
				const float drift
					= std::sin(sim.frame() * 0.05f + slot * 1.7f + cell);
				fill(renderer,
					x + px(24) + cell * size
						+ static_cast<int>(drift * px(3)),
					y + px(20) + (cell % 2) * (size / 2), size, size,
					{92, 88, 84, static_cast<Uint8>(120 + 40 * drift)});
			}
			SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
			continue;
		}
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

	// The fuse wick: how much of the piece's stay is left. Only a burning
	// room draws it - the Forge's burn recipes and campaign duels - the
	// pure board has no clock to show. Frozen solid cyan under Overdrive,
	// the one moment the wick stops burning.
	const bool fused = sim.config().fuse;
	const double limit = fused ? sim.fuse_total() : 0.;
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
	if (charging(sim)) {
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
				? SDL_Color{255, 214, 94, 255} : SDL_Color{255, 122, 46, 255};
			fill(renderer, rail_x, rail_y + rail_h - charge, px(12), charge,
				glow);
		}
		// The bar the gauge has to reach, when a draught has pulled it
		// below the top. Without this the rail visibly never fills before
		// Overdrive lights, which reads as a fault rather than a card.
		if (!burning && sim.config().flow_ignite < 100.) {
			const int mark = static_cast<int>(
				rail_h * (sim.config().flow_ignite / 100.));
			fill(renderer, rail_x, rail_y + rail_h - mark, px(12),
				std::max(1, px(2)), {255, 236, 190, 255});
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
		room.heat = charging(sim) ? sim.flow() / 100. : 0.;
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
	// The hall itself is a painting now - gfx/backdrop.png, scaled to
	// cover - with the living light drawn over it. A checkout without the
	// art keeps the old banded gradient.
	{
		SDL_Rect cover{0, 0, w, h};
		const float want = 1600.f / 900.f;
		if (w < static_cast<int>(h * want)) {
			cover.w = static_cast<int>(h * want);
			cover.x = (w - cover.w) / 2;
		} else {
			cover.h = static_cast<int>(w / want);
			cover.y = h - cover.h;   // Keep the furnace glow on the floor.
		}
		if (!gfx::draw("backdrop", cover)) {
			const int bands = 12;
			for (int i = 0; i < bands; ++i) {
				const double part = static_cast<double>(i) / (bands - 1);
				fill(app.renderer, 0, h * i / bands, w, h / bands + 1,
					{static_cast<Uint8>(26 - 14 * part),
					 static_cast<Uint8>(18 - 9 * part),
					 static_cast<Uint8>(13 - 6 * part), 255});
			}
		}
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
			{255, 176, 60, 255}, 30. + 70. * danger + 46. * glare);
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
				{255, 122, 46, 255}, 20. + 46. * glare + 14. * danger);
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

// Where the last locked piece sat, in pixels. Left alone if nothing has
// locked yet, so the caller's fallback stands.
void lock_centre (App& app, float& x, float& y) {
	if (!app.session.has_value() || app.session->sim().locked().empty()) {
		return;
	}
	const Locked& lock = app.session->sim().locked().back();
	const Piece piece{lock.form, lock.state, lock.x, lock.y};
	float sx = 0.f;
	float sy = 0.f;
	int n = 0;
	for (const Offset cell : cells_of(piece)) {
		sx += cell.x + 0.5f;
		sy += cell.y + 0.5f;
		++n;
	}
	if (n == 0) {
		return;
	}
	x = kBoardX + sx / n * kCell;
	y = kBoardY + sy / n * kCell;
}

// Remember the just-locked piece for the pulse the board draws over it.
void note_lock (App& app) {
	if (!app.session.has_value() || app.session->sim().locked().empty()) {
		return;
	}
	const Locked& lock = app.session->sim().locked().back();
	app.lock_piece = Piece{lock.form, lock.state, lock.x, lock.y};
	app.lock_flash = app.session->sim().frame();
}

// The cues' visible half: what the ear hears, the eye sees.
void juice_cue (App& app, const std::string& cue) {
	if (cue == "clear") {
		// Sparks, no shudder: an ordinary clear happens every few seconds,
		// and a field that jolts on every one of them reads as a seizure,
		// not a reward. The big events below keep the quake.
		spawn_sparks(app, {255, 176, 60, 255}, 3, 2.2f);
	} else if (cue == "lock") {
		note_lock(app);
	} else if (cue == "drop") {
		// The slam felt in the hands: the piece's own pulse and dust off
		// the landing cells - the field itself stays still.
		note_lock(app);
		spawn_sparks(app, {200, 170, 130, 255}, 1, 1.5f);
	} else if (cue == "forced") {
		// The accident hits harder than the choice - and only burn rooms
		// have one, so the jolt stays rare.
		note_lock(app);
		spawn_sparks(app, {255, 122, 46, 255}, 2, 2.0f);
		app.shake_until = std::max(app.shake_until,
			app.session->sim().frame() + 2);
	} else if (cue == "tetris") {
		// The rows themselves are broken by light_burn_rows, which knows
		// which four they were. What belongs here is the well: a quad
		// punches a hole clean through it, so the column goes bright.
		const int shaft = static_cast<int>(kBoardW * 0.55f);
		spawn_beam(app, kBoardX + (kBoardW - shaft) / 2, shaft, kBoardY,
			kBoardH, {255, 214, 94, 255}, 14);
		app.shake_until = app.session->sim().frame() + 8;
	} else if (cue == "tspin") {
		// A turn, drawn as one: a ring opening off the piece and its
		// fragments leaving along the circle rather than away from it.
		float cx = kBoardX + kBoardW * 0.5f;
		float cy = kBoardY + kBoardH * 0.5f;
		lock_centre(app, cx, cy);
		spawn_ring(app, cx, cy, {200, 130, 255, 255},
			static_cast<float>(kCell) * 4.5f, 20);
		spawn_swirl(app, cx, cy, {222, 174, 255, 255}, 12,
			static_cast<float>(kCell) * 0.9f, 3.2f,
			static_cast<float>(kCell) * 0.26f);
		app.shake_until = app.session->sim().frame() + 6;
	} else if (cue == "perfect") {
		// Nothing left standing: the whole well goes white and a wave
		// runs out of its middle. The rarest event on the board gets the
		// only effect that touches every pixel of it.
		spawn_beam(app, kBoardX, kBoardW, kBoardY, kBoardH,
			{255, 255, 255, 255}, 18);
		spawn_ring(app, kBoardX + kBoardW * 0.5f, kBoardY + kBoardH * 0.5f,
			{255, 255, 255, 255}, static_cast<float>(kBoardH) * 0.6f, 30);
		spawn_sparks(app, {255, 255, 255, 255}, 10, 4.0f);
		app.shake_until = app.session->sim().frame() + 10;
	} else if (cue == "overdrive") {
		spawn_sparks(app, {255, 214, 94, 255}, 8, 3.6f);
		app.shake_until = app.session->sim().frame() + 10;
	} else if (cue == "burn") {
		spawn_sparks(app, {255, 122, 46, 255}, 4, 2.6f);
	} else if (cue == "cascade") {
		// The whole chain resolved in one frame - make the collapse felt:
		// a wide rubble-toned burst and a longer rumble than a plain clear.
		spawn_sparks(app, {214, 138, 82, 255}, 8, 3.6f);
		spawn_sparks(app, {255, 176, 60, 255}, 4, 2.4f);
		app.shake_until = app.session->sim().frame() + 8;
	} else if (cue == "freeze") {
		// Cold iron taking hold: frost motes, no violence - the shatter a
		// lock later arrives as the clear it pays for.
		spawn_sparks(app, {180, 216, 255, 255}, 5, 2.2f);
	} else if (cue == "crit") {
		// Loaded dice landing: a white-gold burst and a jolt, so the
		// doubled blow is felt going out.
		spawn_sparks(app, {255, 244, 190, 255}, 7, 3.4f);
		app.shake_until = std::max(app.shake_until,
			app.session->sim().frame() + 5);
	}
}

// Throw shards out of a point: angular fragments that spin and fall. The
// spread is a fan rather than a circle when `down` is false, so a row
// breaking throws its pieces sideways and a slab dropping throws them up.
void spawn_shards (App& app, float x, float y, SDL_Color color, int count,
		float kick, float size) {
	for (int i = 0; i < count; ++i) {
		App::Shard& shard = app.shards[app.shard_at];
		app.shard_at = (app.shard_at + 1) % app.shards.size();
		const float angle = static_cast<float>(app.seeds() % 628) / 100.f;
		const float speed = kick * (0.4f
			+ static_cast<float>(app.seeds() % 100) / 100.f);
		shard.x = x;
		shard.y = y;
		shard.vx = std::cos(angle) * speed;
		// Biased upward: gravity brings them down and the arc is what
		// reads as debris rather than a spray.
		shard.vy = std::sin(angle) * speed - kick * 0.35f;
		shard.turn = static_cast<float>(app.seeds() % 628) / 100.f;
		shard.spin = (static_cast<float>(app.seeds() % 40) - 20.f) / 90.f;
		shard.size = size * (0.6f
			+ static_cast<float>(app.seeds() % 90) / 100.f);
		shard.life = 26 + static_cast<int>(app.seeds() % 16);
		shard.color = color;
	}
}

// Shards laid on a circle and thrown along it rather than out of it: a
// spin throws its debris the way it turned, which is what tells the eye a
// T-spin apart from a clear that merely happened to be violent.
void spawn_swirl (App& app, float x, float y, SDL_Color color, int count,
		float reach, float kick, float size) {
	const float lead = static_cast<float>(app.seeds() % 628) / 100.f;
	for (int i = 0; i < count; ++i) {
		App::Shard& shard = app.shards[app.shard_at];
		app.shard_at = (app.shard_at + 1) % app.shards.size();
		const float angle = lead + i * 6.2832f / count;
		shard.x = x + std::cos(angle) * reach;
		shard.y = y + std::sin(angle) * reach;
		// Tangent, not radius: -sin, cos is the direction the circle
		// travels at that point, so every fragment leaves along the turn.
		shard.vx = -std::sin(angle) * kick;
		shard.vy = std::cos(angle) * kick - kick * 0.4f;
		shard.turn = angle;
		shard.spin = 0.24f;     // All one way, because the piece turned.
		shard.size = size;
		shard.life = 24 + static_cast<int>(app.seeds() % 12);
		shard.color = color;
	}
}

// Every event effect is thrown by something that happened in the well, so
// it is clipped to the well: a fragment tumbling out over the NEXT column
// reads as a bug, not as debris.
struct WellClip {
	explicit WellClip (App& app) : renderer(app.renderer) {
		SDL_RenderGetClipRect(renderer, &was);
		had = SDL_RenderIsClipEnabled(renderer) == SDL_TRUE;
		const SDL_Rect well{kBoardX, kBoardY, kBoardW, kBoardH};
		SDL_RenderSetClipRect(renderer, &well);
	}
	~WellClip () {
		SDL_RenderSetClipRect(renderer, had ? &was : nullptr);
	}
	SDL_Renderer* renderer;
	SDL_Rect was{0, 0, 0, 0};
	bool had = false;
};

void spawn_beam (App& app, int left, int wide, int top, int tall,
		SDL_Color color, int life) {
	App::Beam& beam = app.beams[app.beam_at];
	app.beam_at = (app.beam_at + 1) % app.beams.size();
	beam.left = left;
	beam.wide = wide;
	beam.top = top;
	beam.tall = tall;
	beam.life = life;
	beam.born = life;
	beam.color = color;
}

// Beams, drawn behind the debris: a bright core that narrows to a seam as
// it fades, so the column collapses inward instead of merely dimming.
void draw_beams (App& app) {
	const WellClip clip(app);
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_ADD);
	for (App::Beam& beam : app.beams) {
		if (beam.life <= 0) {
			continue;
		}
		--beam.life;
		const float part = beam.life / static_cast<float>(beam.born);
		const int wide = std::max(2, static_cast<int>(beam.wide * part));
		SDL_Color ink = beam.color;
		// Cubic, not linear: a beam that fades evenly sits on the board
		// like a coat of paint. This one is gone almost as fast as it
		// arrived, which is what makes it a flash and not a wash.
		ink.a = static_cast<Uint8>(150 * part * part * part);
		fill(app.renderer, beam.left + (beam.wide - wide) / 2, beam.top,
			wide, beam.tall, ink);
		// A hotter seam down the middle, half as wide and twice as bright.
		ink.a = static_cast<Uint8>(150 * part * part);
		fill(app.renderer, beam.left + (beam.wide - wide / 2) / 2, beam.top,
			std::max(2, wide / 2), beam.tall, ink);
	}
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
}

void spawn_ring (App& app, float x, float y, SDL_Color color, float span,
		int life) {
	App::Ring& ring = app.rings[app.ring_at];
	app.ring_at = (app.ring_at + 1) % app.rings.size();
	ring.x = x;
	ring.y = y;
	ring.span = span;
	ring.life = life;
	ring.born = life;
	ring.color = color;
}

// Rings, drawn as they go: wide and thin by the end, so the eye reads an
// expanding wave rather than a circle sitting still and fading.
void draw_rings (App& app) {
	const WellClip clip(app);
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
	for (App::Ring& ring : app.rings) {
		if (ring.life <= 0) {
			continue;
		}
		--ring.life;
		const float grow = 1.f - ring.life / static_cast<float>(ring.born);
		const float reach = ring.span * grow;
		SDL_Color ink = ring.color;
		ink.a = static_cast<Uint8>(220 * (1.f - grow) * (1.f - grow));
		// Enough steps that the stamps overlap into a line at the widest
		// the ring ever gets, or it reads as a ring of dots.
		const int steps = 72;
		const float thick = std::max(2.f, px(6) * (1.f - grow));
		for (int i = 0; i < steps; ++i) {
			const float a = i * 6.2832f / steps;
			fill(app.renderer,
				static_cast<int>(ring.x + std::cos(a) * reach - thick / 2),
				static_cast<int>(ring.y + std::sin(a) * reach - thick / 2),
				static_cast<int>(thick), static_cast<int>(thick), ink);
		}
	}
}

// Step and draw the shards: quads under a heavier gravity than the sparks
// carry, spinning as they go, drawn as flat facets rather than glows.
void draw_shards (App& app) {
	const WellClip clip(app);
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
	for (App::Shard& shard : app.shards) {
		if (shard.life <= 0) {
			continue;
		}
		--shard.life;
		shard.x += shard.vx;
		shard.y += shard.vy;
		shard.vy += 0.42f;      // Heavier than an ember: this is a solid.
		shard.vx *= 0.99f;
		shard.turn += shard.spin;
		const float half = shard.size * (0.4f + shard.life / 60.f);
		const float cos = std::cos(shard.turn);
		const float sin = std::sin(shard.turn);
		SDL_Color ink = shard.color;
		ink.a = static_cast<Uint8>(std::min(255, shard.life * 9));
		// A spinning quad, drawn as its own two triangles so it keeps a
		// hard edge at every angle - a rotated rectangle fill would not.
		SDL_Vertex quad[4];
		const float ox[4] = {-half, half, half, -half};
		const float oy[4] = {-half, -half * 0.6f, half, half * 0.6f};
		for (int i = 0; i < 4; ++i) {
			quad[i].position.x = shard.x + ox[i] * cos - oy[i] * sin;
			quad[i].position.y = shard.y + ox[i] * sin + oy[i] * cos;
			quad[i].color = ink;
			quad[i].tex_coord = SDL_FPoint{0.f, 0.f};
		}
		const int order[6] = {0, 1, 2, 0, 2, 3};
		SDL_Vertex tris[6];
		for (int i = 0; i < 6; ++i) {
			tris[i] = quad[order[i]];
		}
		SDL_RenderGeometry(app.renderer, nullptr, tris, 6, nullptr, 0);
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
		IM_COL32(255, 214, 94, static_cast<int>(alpha * 255)), banner.text.c_str());
}

// The colour a skill announces itself in - the charge, the bolt, the plate
// rim and the impact all wear it, so a player learns "blue means the iron
// is about to freeze" before they have finished reading the words.
SDL_Color skill_hue (const std::string& id) {
	if (id == "coldsnap") {
		return {150, 202, 246, 255};
	}
	if (id == "vaultdark" || id == "smokescreen") {
		return {162, 136, 206, 255};
	}
	if (id == "deadweight" || id == "forgestrike") {
		return {236, 172, 92, 255};
	}
	if (id == "sealgate" || id == "pincer" || id == "tongslock") {
		return {182, 192, 206, 255};
	}
	return {255, 122, 46, 255};   // Rust, heat, and anything added later.
}

// Where a cast is: 0 at the warning, 1 at the blow, past 1 while the blow
// is still ringing. Negative when nothing is being cast, which is most of
// the time - every drawing below leaves early on it.
float cast_wind (const App& app, const VersusMatch& match) {
	if (!app.session.has_value() || match.skill_banner.empty()
		|| match.skill_fires_at < 0) {
		return -1.f;
	}
	const long frame = app.session->sim().frame();
	if (frame >= match.skill_banner_until) {
		return -1.f;
	}
	const long lead = std::max(1L,
		match.skill_fires_at - match.skill_warned_at);
	return static_cast<float>(frame - match.skill_warned_at) / lead;
}

// The two ends of the flight: the foe's well, and yours.
ImVec2 foe_middle () {
	return ImVec2(kMiniX + kWidth * kMiniCell * 0.5f,
		kMiniY + kHeight * kMiniCell * 0.5f);
}

ImVec2 mine_middle () {
	return ImVec2(kBoardX + kBoardW * 0.5f, kBoardY + kBoardH * 0.5f);
}

// How hard the foe's well is moving: a tremor that builds through the
// wind-up, then a recoil away from the player as it lets go. Read by
// draw_versus_panel, which offsets its whole board by it - a caster that
// stacks placidly through its own spell is the reason none of this landed
// as a skill.
ImVec2 cast_kick (const App& app, const VersusMatch& match) {
	const float wind = cast_wind(app, match);
	if (wind < 0.f) {
		return ImVec2(0.f, 0.f);
	}
	const long frame = app.session->sim().frame();
	const float away = foe_middle().x >= mine_middle().x ? 1.f : -1.f;
	if (wind >= 1.f) {
		const long since = frame - match.skill_fires_at;
		if (since > 12) {
			return ImVec2(0.f, 0.f);
		}
		const float ease = 1.f - since / 12.f;
		return ImVec2(ui(16) * ease * ease * away, 0.f);
	}
	// Nothing for the first half; then a shudder that grows fast, so the
	// last second is visibly worse than the first.
	const float tremor = std::max(0.f, wind - 0.45f) / 0.55f;
	const float amp = ui(4) * tremor * tremor;
	return ImVec2(std::sin(frame * 1.9f) * amp, std::cos(frame * 2.7f) * amp);
}

// The whole cast, drawn: the foe's well charging, the bolt crossing, the
// plate that names it, and the ring where it lands.
//
// The plate alone was not enough, and the reason is now obvious - it was
// static, and it hung over the wrong board. A boss that casts without its
// own well doing anything is not a boss casting, it is a timer on your
// side of the screen. So the charge gathers over there, the blow travels,
// and it arrives on the frame the rules say it arrives.
void draw_caster_plate (App& app, const VersusMatch& match) {
	const float wind = cast_wind(app, match);
	if (wind < 0.f) {
		return;
	}
	const long frame = app.session->sim().frame();
	const bool struck = wind >= 1.f;
	const float after = struck
		? std::clamp(static_cast<float>(frame - match.skill_fires_at)
			/ std::max(1L, match.skill_banner_until - match.skill_fires_at),
			0.f, 1.f)
		: 0.f;
	const float alpha = struck ? 1.f - after * after : 1.f;
	const SDL_Color hue = skill_hue(match.skill_caster);
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	const auto tint = [&hue] (float lift, int a) {
		return IM_COL32(
			std::min(255, static_cast<int>(hue.r * lift)),
			std::min(255, static_cast<int>(hue.g * lift)),
			std::min(255, static_cast<int>(hue.b * lift)), a);
	};
	const auto fade = [alpha] (int r, int g, int b, int a) {
		return IM_COL32(r, g, b, static_cast<int>(a * alpha));
	};

	const ImVec2 kick = cast_kick(app, match);
	const ImVec2 from(foe_middle().x + kick.x, foe_middle().y + kick.y);
	const ImVec2 to = mine_middle();

	// --- The charge, over the foe's well. ---------------------------------
	if (!struck) {
		const float foe_w = kWidth * kMiniCell;
		const float foe_h = kHeight * kMiniCell;
		// The rim heats through the wind-up.
		const float rim = ui(2) + ui(3) * wind;
		draw->AddRect(ImVec2(from.x - foe_w / 2 - rim, from.y - foe_h / 2 - rim),
			ImVec2(from.x + foe_w / 2 + rim, from.y + foe_h / 2 + rim),
			tint(0.6f + 0.6f * wind, static_cast<int>(90 + 165 * wind)),
			ui(3), 0, rim);
		// Motes drawn in from the rim to the middle, arriving together: the
		// well is gathering something, and you can see how much is left.
		for (int i = 0; i < 10; ++i) {
			const float turn = i * 0.6283f + frame * 0.04f;
			const float pull = 1.f - std::pow(
				std::clamp((wind - i * 0.03f), 0.f, 1.f), 1.6f);
			const float reach = (foe_w * 0.62f) * pull;
			const ImVec2 at(from.x + std::cos(turn) * reach,
				from.y + std::sin(turn) * reach * 1.35f);
			draw->AddCircleFilled(at, ui(2) + ui(2) * wind,
				tint(1.15f, static_cast<int>(70 + 185 * wind)));
		}
		// The core, brightening to white as the last beat runs out.
		draw->AddCircleFilled(from, ui(4) + ui(13) * wind * wind,
			tint(0.9f + 0.9f * wind, static_cast<int>(60 + 150 * wind)));
	}

	// --- The flight. ------------------------------------------------------
	// The bolt is in the air for the last twenty frames of the wind-up and
	// arrives on the blow, so what hits you is the thing you watched.
	const long out = match.skill_fires_at - VersusMatch::kFlight;
	if (frame >= out && frame <= match.skill_fires_at + 2) {
		for (int bolt = 0; bolt < 3; ++bolt) {
			const long lag = bolt * 2;
			const float run = static_cast<float>(frame - out - lag)
				/ VersusMatch::kFlight;
			if (run < 0.f || run > 1.f) {
				continue;
			}
			// Eased so it leaves fast and drives in faster still.
			const float t = run * run * (3.f - 2.f * run);
			const float sway = (bolt - 1) * ui(22) * std::sin(run * 3.1416f);
			const ImVec2 head(from.x + (to.x - from.x) * t,
				from.y + (to.y - from.y) * t + sway);
			const float back = std::max(0.f, t - 0.16f);
			const ImVec2 tail(from.x + (to.x - from.x) * back,
				from.y + (to.y - from.y) * back + sway);
			draw->AddLine(tail, head, tint(0.7f, 120), ui(7));
			draw->AddLine(tail, head, tint(1.25f, 220), ui(3));
			draw->AddCircleFilled(head, ui(6), tint(1.4f, 240));
			draw->AddCircleFilled(head, ui(3), IM_COL32(255, 250, 240, 250));
		}
	}

	// --- The landing. -----------------------------------------------------
	if (struck) {
		const long since = frame - match.skill_fires_at;
		if (since < 16) {
			const float grow = since / 16.f;
			draw->AddCircle(to, ui(30) + ui(190) * grow,
				tint(1.2f, static_cast<int>(220 * (1.f - grow))), 48,
				ui(6) * (1.f - grow) + ui(1));
			draw->AddCircle(to, ui(10) + ui(120) * grow,
				IM_COL32(255, 250, 240,
					static_cast<int>(170 * (1.f - grow))), 40,
				ui(3) * (1.f - grow) + ui(1));
		}
	}

	// --- The plate. -------------------------------------------------------
	// Landscape hangs it over the foe's well, where the announcement
	// belongs. A phone has no room for that - the foe's board is a tenth of
	// the screen, and a plate wide enough to read would sit squarely over
	// the player's own stack, hiding it at the exact moment they need to
	// read it. So portrait puts the plate in the strip above both boards
	// and draws a tether to the caster instead.
	const ImVec2 screen = ImGui::GetIO().DisplaySize;
	const float tall = ui(66);
	const float wide = kPortrait
		? screen.x - ui(12)
		: std::max(kWidth * kMiniCell * 1.f, ui(250));
	const float left = kPortrait
		? ui(6)
		: std::clamp(from.x - wide / 2, ui(6), screen.x - wide - ui(6));
	const float right = left + wide;
	const float top = kPortrait
		? ui(4)
		: std::max(ui(6), kMiniY - tall - ui(10));
	// The tether: a line from the plate to the well that is casting, so a
	// plate that could not sit over its caster still points at it.
	{
		const ImVec2 anchor(std::clamp(from.x, left, right), top + tall);
		if (from.y > anchor.y + ui(8)) {
			draw->AddLine(anchor, ImVec2(from.x, from.y - ui(6)),
				tint(1.f, static_cast<int>((struck ? 90 : 60 + 120 * wind)
					* alpha)), ui(2));
		}
	}
	const int heat = static_cast<int>(30 + 90 * (struck ? 1.f : wind));
	draw->AddRectFilled(ImVec2(left + ui(3), top + ui(4)),
		ImVec2(right + ui(3), top + tall + ui(4)), fade(0, 0, 0, 150),
		ui(4));
	draw->AddRectFilledMultiColor(ImVec2(left, top), ImVec2(right, top + tall),
		fade(34, 26, 21, 245), fade(34, 26, 21, 245),
		fade(heat, heat / 3, 20, 245), fade(heat, heat / 3, 20, 245));
	draw->AddRect(ImVec2(left, top), ImVec2(right, top + tall),
		tint(struck ? 1.6f : 0.7f + 0.7f * wind,
			static_cast<int>(255 * alpha)), ui(3), 0, ui(2));

	ImFont* small_font = app.fonts.body;
	if (!match.foe_name.empty()) {
		const char* who = match.foe_name.c_str();
		const ImVec2 size = small_font->CalcTextSizeA(
			small_font->FontSize, FLT_MAX, 0.f, who);
		draw->AddText(small_font, small_font->FontSize,
			ImVec2(left + (wide - size.x) / 2, top + ui(8)),
			fade(214, 190, 164, 220), who);
	}
	ImFont* font = app.fonts.head;
	const char* what = match.skill_banner.c_str();
	// The name shrinks to fit rather than running off a phone's board, and
	// snaps a little larger on the frame it lands.
	float size = font->FontSize * (struck ? 1.32f : 1.15f);
	ImVec2 extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, what);
	const float room = wide - ui(16);
	if (extent.x > room) {
		size *= room / extent.x;
		extent = font->CalcTextSizeA(size, FLT_MAX, 0.f, what);
	}
	const float name_y = top + tall - ui(16) - extent.y;
	draw->AddText(font, size,
		ImVec2(left + (wide - extent.x) / 2 + ui(2), name_y + ui(2)),
		fade(20, 8, 4, 200), what);
	draw->AddText(font, size, ImVec2(left + (wide - extent.x) / 2, name_y),
		struck ? fade(255, 246, 232, 255) : tint(1.15f, 255), what);

	// The wind-up bar along the plate's foot, so the dread has a length.
	const float bar_y = top + tall - ui(7);
	const float bar_w = wide - ui(12);
	draw->AddRectFilled(ImVec2(left + ui(6), bar_y),
		ImVec2(right - ui(6), bar_y + ui(4)), fade(0, 0, 0, 160), ui(2));
	draw->AddRectFilled(ImVec2(left + ui(6), bar_y),
		ImVec2(left + ui(6) + bar_w * std::min(wind, 1.f), bar_y + ui(4)),
		struck ? fade(255, 250, 236, 255) : tint(1.25f, 255), ui(2));
}

// The other board, small, and the wire's state around both: the bot's
// stack with its live piece, the scoreboard, and each side's incoming
// garbage as a red column beside its board.
void draw_versus_panel (App& app) {
	if (!app.versus.has_value() || !app.versus->armed()) {
		return;
	}
	VersusMatch& match = *app.versus;
	const Sim& theirs = match.lead().sim.sim();
	SDL_Renderer* renderer = app.renderer;
	const int cell = kMiniCell;
	// The whole foe board rides its own cast: a tremor building through the
	// wind-up, a recoil as it lets go. Everything below is drawn from these
	// two, so the stack, the piece and the frame move together the way a
	// struck object does.
	const ImVec2 kick = cast_kick(app, match);
	app.foe_rects.clear();
	// A room of foes shares the width one foe had: three boards side by
	// side at a third of the cell, one board at full size. The player's
	// well never moves for either.
	const int room = static_cast<int>(match.foes.size());
	const int left = kMiniX + static_cast<int>(kick.x);
	const int top = kMiniY + static_cast<int>(kick.y);
	// A room takes the whole band to the right of the player, not the
	// width one foe had. Squeezing three boards into one board's width put
	// them at four pixels a cell with the rest of that half of the screen
	// empty underneath - unreadable, and it looked like a mistake because
	// it was one. The band is measured off the real window so nothing can
	// overflow a phone.
	int out_w = 0;
	int out_h = 0;
	SDL_GetRendererOutputSize(renderer, &out_w, &out_h);
	const int band = room > 1
		? std::max(kWidth * cell, out_w - left - px(30))
		: kWidth * cell;
	const int gap = room > 1 ? px(10) : 0;
	// And the room is not three equal thumbnails. The one decision this
	// mode has is who you are aiming at, so the aimed board is the big one
	// and the others sit back - and the sizes travel when the aim moves,
	// which is what makes the switch felt rather than merely noted. A
	// beaten board shrinks further still: it is furniture now.
	std::array<float, App::kSeats> weight{};
	float shares = 0.f;
	for (int slot = 0; slot < room && slot < App::kSeats; ++slot) {
		const VersusMatch::Foe& foe = *match.foes[static_cast<size_t>(slot)];
		const bool aimed = slot == match.target && !foe.down;
		float& focus = app.room_focus[static_cast<size_t>(slot)];
		focus += ((aimed ? 1.f : 0.f) - focus) * 0.16f;
		weight[static_cast<size_t>(slot)]
			= (foe.down ? 0.68f : 1.f) + 0.85f * focus;
		shares += weight[static_cast<size_t>(slot)];
	}
	const int usable = band - (room - 1) * gap;
	std::array<int, App::kSeats> seats{};
	int tallest = cell;
	for (int slot = 0; slot < room && slot < App::kSeats; ++slot) {
		seats[static_cast<size_t>(slot)] = room > 1
			? std::max(px(2), static_cast<int>(
				usable * weight[static_cast<size_t>(slot)] / shares)
				/ kWidth)
			: cell;
		tallest = std::max(tallest, seats[static_cast<size_t>(slot)]);
	}
	if (room == 1) {
		tallest = cell;
	} else {
		// The band is wide enough to make the aimed board taller than the
		// window has room for, and the scoreboard lives under the room.
		// Height is capped last so the width above can be generous without
		// pushing the readout off the bottom of the screen.
		const int fits = (out_h - top - static_cast<int>(ui(120))) / kHeight;
		tallest = std::max(px(2), std::min(tallest, fits));
		for (int slot = 0; slot < room && slot < App::kSeats; ++slot) {
			int& seat = seats[static_cast<size_t>(slot)];
			seat = std::min(seat, tallest);
		}
	}
	// One floor under all of them, so boards of different heights read as
	// a room rather than as a misaligned row.
	const int floor_y = top + kHeight * tallest;
	int pen = left;
	for (int slot = 0; slot < room && slot < App::kSeats; ++slot) {
		const VersusMatch::Foe& foe = *match.foes[static_cast<size_t>(slot)];
		const Sim& board = foe.sim.sim();
		const int seat = seats[static_cast<size_t>(slot)];
		const int seat_w = kWidth * seat;
		const int bx = pen;
		const int by = floor_y - kHeight * seat;
		pen += seat_w + gap;
		const bool marked = slot == match.target && !foe.down;
		// The seat: the aimed foe wears an ember frame, because where the
		// player's garbage is going has to be legible at a glance in a
		// room where three boards are moving at once.
		fill(renderer, bx - px(2), by - px(2), seat_w + px(4),
			kHeight * seat + px(4), foe.down ? SDL_Color{40, 34, 30, 255}
				: marked ? SDL_Color{188, 96, 40, 255}
				         : SDL_Color{58, 42, 30, 255});
		if (board.overdrive() && !foe.down) {
			const SDL_Color rim{255, 214, 94, 255};
			const int wide = seat_w;
			const int tall = kHeight * seat;
			fill(renderer, bx - px(3), by - px(3), wide + px(6), px(3), rim);
			fill(renderer, bx - px(3), by + tall, wide + px(6), px(3), rim);
			fill(renderer, bx - px(3), by, px(3), tall, rim);
			fill(renderer, bx + wide, by, px(3), tall, rim);
		}
		fill(renderer, bx, by, seat_w, kHeight * seat, {17, 12, 9, 255});
		if (room > 1) {
			// A generous target: the board plus its label strip, because a
			// thumb is wider than a three-pixel cell.
			// ui() is a float and SDL_Rect is not: the NDK's clang refuses
			// the narrowing that gcc waved through.
			app.foe_rects.push_back(SDL_Rect{bx - px(3),
				by - static_cast<int>(ui(24)), seat_w + px(6),
				kHeight * seat + static_cast<int>(ui(26))});
		}
		// The same metal on both sides of the table. A duel seats the foe's
		// board at the player's own cell size, so a flat swatch there next
		// to a poured block here read as two different games; the bevel
		// only gives up below the size where its own chamfer would be
		// wider than the cell.
		const bool cast = seat >= 6;
		const auto put = [&] (int cx, int cy, SDL_Color ink) {
			if (cast) {
				draw_cell(renderer, bx + cx * seat, by + cy * seat, ink, seat);
			} else {
				fill(renderer, bx + cx * seat + 1, by + cy * seat + 1,
					seat - 2, seat - 2, ink);
			}
		};
		for (int y = 0; y < kHeight; ++y) {
			for (int x = 0; x < kWidth; ++x) {
				const int form = board.board().at(x, y);
				if (form >= 0) {
					SDL_Color ink = kFormColors[std::min(form, 7)];
					if (foe.down) {
						// A beaten board cools rather than vanishing: the
						// room should show what is already finished.
						ink = SDL_Color{
							static_cast<Uint8>(ink.r / 3),
							static_cast<Uint8>(ink.g / 3),
							static_cast<Uint8>(ink.b / 3), 255};
					}
					put(x, y, ink);
				}
			}
		}
		if (!foe.down && board.entry() && board.piece().form <= 6) {
			for (const Offset at : cells_of(board.piece())) {
				if (at.y >= 0) {
					put(at.x, at.y, kFormColors[board.piece().form]);
				}
			}
		}
		if (room > 1) {
			// In a room every board carries its own two readouts, because
			// the decision here is which of three to bury and both of
			// these are how you make it. A single rail drawn at the old
			// fixed offset landed on top of the next board along.
			const int rail_h = kHeight * seat;
			if (charging(board) && !foe.down) {
				const int rail_x = bx + seat_w + px(2);
				fill(renderer, rail_x, by, px(3), rail_h, {36, 27, 20, 255});
				const bool burning = board.overdrive();
				const int charge = burning ? rail_h
					: static_cast<int>(rail_h * (board.flow() / 100.));
				if (charge > 0) {
					fill(renderer, rail_x, by + rail_h - charge, px(3),
						charge, burning ? SDL_Color{255, 214, 94, 255}
							: SDL_Color{255, 122, 46, 255});
				}
			}
			const int owed = std::min(board.pending_garbage(), kHeight);
			if (owed > 0 && !foe.down) {
				fill(renderer, bx - px(6), by + rail_h - owed * seat,
					px(4), owed * seat, {224, 82, 82, 255});
			}
		}
		// Who this is, and whether it is still in the fight. A board too
		// narrow to carry a word gets its slot number instead - the
		// scoreboard names who is aimed at, and the frame shows which one
		// that is, so the label here only has to tell them apart.
		std::string tag;
		if (room == 1) {
			tag = "BOT";
		} else if (seat_w >= ui(52)) {
			tag = bot::might_of(foe.rank_index);
			if (foe.down) {
				tag += " - down";
			}
		} else {
			tag = std::to_string(slot + 1);
			if (foe.down) {
				tag += "x";
			}
		}
		draw_label(tag.c_str(), static_cast<float>(bx), by - ui(22),
			foe.down ? IM_COL32(120, 112, 104, 255)
				: marked ? IM_COL32(255, 176, 60, 255)
				: board.overdrive() ? IM_COL32(255, 214, 94, 255)
				: IM_COL32(176, 158, 140, 255));
	}
	// What the bot has drafted, under its board - the same build line the
	// player's pause screen shows, because an opponent's tempers are half
	// of reading the fight. Portrait has no width for the line there, so
	// it rides in the scoreboard instead.
	if (!match.bot_tempers.empty() && !kPortrait) {
		draw_label(temper_line(match.bot_tempers).c_str(),
			static_cast<float>(left), floor_y + px(6),
			IM_COL32(176, 158, 140, 255));
	}
	// The bot's Flow rail on its board's right flank - watching the gauge
	// creep up is the warning the ignition deserves. A room draws its own,
	// one per board, up in the loop.
	if (room == 1 && charging(theirs)) {
		const int rail_x = left + kWidth * cell + px(4);
		const int rail_h = kHeight * cell;
		fill(renderer, rail_x, top, px(4), rail_h, {36, 27, 20, 255});
		const bool burning = theirs.overdrive();
		const int charge = burning ? rail_h
			: static_cast<int>(rail_h * (theirs.flow() / 100.));
		if (charge > 0) {
			fill(renderer, rail_x, top + rail_h - charge, px(4), charge,
				burning ? SDL_Color{255, 214, 94, 255}
					: SDL_Color{255, 122, 46, 255});
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
	if (room == 1) {
		meter(left - px(10), top + kHeight * cell,
			theirs.pending_garbage(), cell);
	}
	// The scoreboard: beside the opponent's full board in the duel layout,
	// under the mini board otherwise - except in portrait, where the right
	// margin is too narrow for it and it sits under the player's.
	if (kPortrait) {
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(kBoardX),
			static_cast<float>(kBoardY + kBoardH) + ui(24)));
	} else if (room > 1) {
		// Under the room, not beside it: the boards want the whole band,
		// and there is a screen's worth of empty floor below them.
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(left) - ui(4),
			static_cast<float>(floor_y) + ui(26)));
	} else if (kDuelSide) {
		ImGui::SetNextWindowPos(ImVec2(
			static_cast<float>(left + kWidth * cell) + ui(16),
			static_cast<float>(top) + ui(4)));
	} else {
		ImGui::SetNextWindowPos(ImVec2(static_cast<float>(left) - ui(4),
			static_cast<float>(top + kHeight * cell) + ui(10)));
	}
	ImGui::Begin("versus score", nullptr, ImGuiWindowFlags_NoTitleBar
		| ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove
		| ImGuiWindowFlags_NoSavedSettings);
	ImGui::Text("You %d - %d %s", match.player_wins, match.bot_wins,
		match.foe_title().c_str());
	if (match.raid()) {
		ImGui::TextDisabled("%d of %d still standing  -  Tab to switch aim",
			match.standing(), static_cast<int>(match.foes.size()));
	} else {
		ImGui::TextDisabled("first to %d  round %d",
			match.first_to, match.round);
	}
	if (kPortrait && !match.bot_tempers.empty()) {
		ImGui::TextDisabled("%s", temper_line(match.bot_tempers).c_str());
	}
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
			IM_COL32(255, 214, 94, 255), verdict);
	}
	draw_caster_plate(app, match);
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
		IM_COL32(255, 122, 46, 70), 48, ui(3));
	draw->PathArcTo(middle, font->FontSize * 1.1f, -1.5708f,
		-1.5708f + 6.2832f * second, 48);
	draw->PathStroke(IM_COL32(255, 214, 94, 220), 0, ui(4));
	draw->AddText(font, font->FontSize,
		ImVec2(at.x + ui(3), at.y + ui(3)), IM_COL32(40, 16, 6, 200), text);
	draw->AddText(font, font->FontSize, at,
		IM_COL32(255, 214, 94, 255), text);
}

// --- The polish pass: what happens between the things that happen. -------

// The curtain, over everything including ImGui.
//
// Screens used to cut. Forty-odd places in this file assign a screen, and
// a transition each of them had to remember to start would have been a
// transition half the game did not have - so nothing announces a change;
// the curtain watches for one. What it draws is a soot veil lifting off a
// hot seam: the seam runs across at the height the veil has reached, so
// the eye follows one bright line up and out rather than watching a
// rectangle get less grey.
void draw_curtain (App& app) {
	if (app.curtain <= 0) {
		return;
	}
	--app.curtain;
	int w = 0;
	int h = 0;
	SDL_GetRendererOutputSize(app.renderer, &w, &h);
	const float part = app.curtain / static_cast<float>(kCurtain);
	// Cubic out: most of the veil is gone in the first third, so a screen
	// change feels quick and only the seam lingers.
	const float lift = 1.f - part * part * part;
	const float edge = h * lift;
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	draw->AddRectFilled(ImVec2(0.f, edge), ImVec2(static_cast<float>(w),
		static_cast<float>(h)),
		IM_COL32(16, 10, 7, static_cast<int>(236 * part)));
	// The seam: a hot line at the veil's edge, with its own falloff above
	// and below, brightest while there is still veil to burn off.
	const int glow = static_cast<int>(210 * part);
	draw->AddRectFilledMultiColor(
		ImVec2(0.f, edge - ui(26)), ImVec2(static_cast<float>(w), edge),
		IM_COL32(255, 122, 46, 0), IM_COL32(255, 122, 46, 0),
		IM_COL32(255, 176, 60, glow), IM_COL32(255, 176, 60, glow));
	draw->AddRectFilled(ImVec2(0.f, edge - ui(2)),
		ImVec2(static_cast<float>(w), edge + ui(2)),
		IM_COL32(255, 236, 190, glow));
}

// The well coming up to heat, behind the count.
//
// A game used to begin with a board that was simply there. Now the
// crucible lights from the floor up over the three seconds - the rim
// heats, the grid arrives - so the first piece falls into somewhere that
// was made ready rather than somewhere that was always on.
void draw_preheat (App& app) {
	if (app.countdown <= 0 || !app.session.has_value()) {
		return;
	}
	const float part = std::clamp(
		app.countdown / static_cast<float>(std::max(1, app.start_delay)),
		0.f, 1.f);
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	// Soot over the well, thinning as the heat comes up.
	draw->AddRectFilled(
		ImVec2(static_cast<float>(kBoardX), static_cast<float>(kBoardY)),
		ImVec2(static_cast<float>(kBoardX + kBoardW),
			static_cast<float>(kBoardY + kBoardH)),
		IM_COL32(12, 8, 6, static_cast<int>(200 * part)));
	// And the floor's own glow climbing the well as it thins.
	const float reach = kBoardH * (1.f - part) * 0.7f;
	if (reach > 1.f) {
		draw->AddRectFilledMultiColor(
			ImVec2(static_cast<float>(kBoardX),
				kBoardY + kBoardH - reach),
			ImVec2(static_cast<float>(kBoardX + kBoardW),
				static_cast<float>(kBoardY + kBoardH)),
			IM_COL32(255, 122, 46, 0), IM_COL32(255, 122, 46, 0),
			IM_COL32(255, 148, 52, 90), IM_COL32(255, 148, 52, 90));
	}
}

// And the well going out, after the last piece. The board is still drawn
// underneath - a loss screen over a board that vanished reads as a crash -
// but it cools to soot over half a second, which is the pause the verdict
// needs to land in.
void draw_cooldown (App& app) {
	if (app.cool_down <= 0) {
		return;
	}
	--app.cool_down;
	const float part = 1.f - app.cool_down / static_cast<float>(kCooling);
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	draw->AddRectFilled(
		ImVec2(static_cast<float>(kBoardX), static_cast<float>(kBoardY)),
		ImVec2(static_cast<float>(kBoardX + kBoardW),
			static_cast<float>(kBoardY + kBoardH)),
		IM_COL32(10, 7, 5, static_cast<int>(150 * part)));
}

// The maul, on the first frame of a run.
//
// A climb used to begin with a map appearing. It is the one moment in the
// game that deserves a flourish, so it gets one: the maul comes down out
// of the top right, lands on the foot of the tree - the bottom row, where
// a run is actually started - and the shock runs up the map lighting the
// road as it goes.
//
// It is drawn over ImGui rather than inside the map window, so the swing
// can start off-screen and the impact can shake the whole frame. If the
// map has not drawn yet (`map_seen`), nothing happens at all - the blow
// has nowhere to land, and a hammer hitting the middle of a blank screen
// would be worse than no hammer.
void draw_forge_strike (App& app) {
	if (app.forge_strike <= 0) {
		return;
	}
	--app.forge_strike;
	if (!app.map_seen || app.screen != Screen::Career) {
		app.forge_strike = 0;
		return;
	}
	const int gone = kStrike - app.forge_strike;
	const int kLands = 38;
	ImDrawList* draw = ImGui::GetForegroundDrawList();
	// The anvil the blow lands on. A hammer swinging at nothing was the
	// first version and it read as a hammer swinging at nothing: the maul
	// needs something to hit, and the thing a forge hits is a stithy.
	// Sized and seated so it stands UNDER the tree rather than on top of
	// it: a first pass put a two-hundred-pixel anvil across the bottom two
	// rows, which buried the very nodes the run is about to choose between.
	const float stand = ui(150);
	const ImVec2 seat(app.map_foot.x - stand * 0.5f,
		app.map_foot.y + ui(8) - stand * 0.345f);
	// The worked face, a third of the way down the sprite - the line the
	// hammer actually strikes, and so where everything else happens.
	const ImVec2 hit(app.map_foot.x, app.map_foot.y + ui(8));
	if (SDL_Texture* iron = gfx::get("stithy")) {
		// It settles in ahead of the swing rather than being there from
		// nothing, and it takes a small recoil of its own on the blow.
		const float in = std::min(1.f, gone / 10.f);
		const float rung = gone >= kLands
			? std::max(0.f, 1.f - (gone - kLands) / 8.f) : 0.f;
		const float drop = stand * 0.10f * (1.f - in) + ui(7) * rung;
		draw->AddImage(reinterpret_cast<ImTextureID>(iron),
			ImVec2(seat.x, seat.y + drop),
			ImVec2(seat.x + stand, seat.y + stand + drop),
			ImVec2(0.f, 0.f), ImVec2(1.f, 1.f),
			IM_COL32(255, 255, 255, static_cast<int>(255 * in)));
	}
	SDL_Texture* tex = gfx::get("maul");
	// The maul is drawn through the swing AND for a while after it lands:
	// it rests on the face, then lifts away. A hammer that vanished on the
	// frame it struck read as a glitch, not a blow.
	const int kRests = kLands + 16;
	if (gone <= kRests && tex != nullptr) {
		// The swing: in from the upper right, turning as it comes, and
		// accelerating - a maul is heavy and the last third of a swing is
		// where all of it happens.
		const float t = std::min(1.f, gone / static_cast<float>(kLands));
		const float fall = t * t * t;
		const float reach = ui(520);
		// After the blow it rides the anvil's own recoil for a few frames
		// and then lifts back out the way it came.
		const float away = gone <= kLands ? 0.f
			: std::pow((gone - kLands) / static_cast<float>(kRests - kLands),
				2.2f);
		const ImVec2 head(hit.x + reach * ((1.f - fall) + away) * 0.85f,
			hit.y - reach * ((1.f - fall) + away) - ui(30)
				+ ui(7) * (gone > kLands && gone < kLands + 8 ? 1.f : 0.f));
		const float turn = -1.15f * ((1.f - fall) + away);
		const float size = ui(190);
		// The head of the sprite sits at its top; the blow lands there, so
		// the quad is hung from that point and turned about it.
		const float cos = std::cos(turn);
		const float sin = std::sin(turn);
		const auto put = [&] (float ox, float oy) {
			return ImVec2(head.x + ox * cos - oy * sin,
				head.y + ox * sin + oy * cos);
		};
		draw->AddImageQuad(reinterpret_cast<ImTextureID>(tex),
			put(-size * 0.5f, 0.f), put(size * 0.5f, 0.f),
			put(size * 0.5f, size), put(-size * 0.5f, size),
			ImVec2(0.f, 0.f), ImVec2(1.f, 0.f), ImVec2(1.f, 1.f),
			ImVec2(0.f, 1.f), IM_COL32(255, 244, 232, 255));
		// A smear behind the head through the fast part of the swing, so
		// the eye reads speed rather than a sprite teleporting.
		if (t > 0.55f) {
			const float lag = std::min(1.f, (t - 0.55f) * 3.f);
			draw->AddLine(ImVec2(head.x + reach * 0.30f * lag,
				head.y - reach * 0.34f * lag), head,
				IM_COL32(255, 214, 94, static_cast<int>(90 * lag)),
				ui(10) * lag);
		}
	}
	if (gone == kLands) {
		// Landed. The whole screen takes it - the board's own quake is no
		// use here, there is no board - and it is deliberately the hardest
		// jolt in the game, because it is the only one that opens a run.
		app.jolt = 22;
		app.jolt_born = 22;
		app.jolt_power = ui(26);
		app.audio.play("crit");
	}
	if (gone >= kLands) {
		const float since = (gone - kLands) / static_cast<float>(
			std::max(1, kStrike - kLands));
		// The flash at the anvil, gone in a few frames. Small on purpose:
		// a first attempt used a hundred and twenty pixels at nearly full
		// alpha and it simply erased the bottom third of the map, hammer
		// included.
		const float flare = std::max(0.f, 1.f - since * 9.f);
		if (flare > 0.f) {
			draw->AddCircleFilled(hit, ui(52) * flare,
				IM_COL32(255, 244, 214, static_cast<int>(150 * flare)));
			draw->AddCircleFilled(hit, ui(20) * flare,
				IM_COL32(255, 255, 246, static_cast<int>(220 * flare)));
		}
		// A ring off the impact, and sparks thrown along the ground.
		const float ring = ui(40) + ui(300) * std::min(1.f, since * 2.2f);
		const int ink = static_cast<int>(
			220 * std::max(0.f, 1.f - since * 2.2f));
		if (ink > 0) {
			draw->AddCircle(hit, ring, IM_COL32(255, 176, 60, ink), 64,
				ui(5) * std::max(0.2f, 1.f - since * 2.f));
		}
		// And the shock running up the tree: a band of light climbing from
		// the foot to the head, lighting the road on its way. This is the
		// part that says the run has started rather than merely appeared.
		const float climb = std::min(1.f, since * 1.35f);
		const float y = hit.y + (app.map_head.y - hit.y) * climb;
		const int band = static_cast<int>(200 * (1.f - climb));
		if (band > 0) {
			draw->AddRectFilledMultiColor(
				ImVec2(app.map_span.x, y - ui(70)),
				ImVec2(app.map_span.y, y),
				IM_COL32(255, 176, 60, 0), IM_COL32(255, 176, 60, 0),
				IM_COL32(255, 214, 94, band), IM_COL32(255, 214, 94, band));
			draw->AddRectFilled(ImVec2(app.map_span.x, y - ui(2)),
				ImVec2(app.map_span.y, y + ui(2)),
				IM_COL32(255, 236, 190, band * 3 / 4));
		}
	}
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
		draw->AddRectFilled(a, b, held ? IM_COL32(255, 122, 46, 70)
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
			ImGui::PopItemWidth();
			ImGui::Spacing();
			ImGui::TextDisabled("Handling applies from the next game.");
			ImGui::TextDisabled(
				"The engine runs 20ms frames; values land on that grid.");
			ImGui::EndTabItem();
		}
		// Feel, not rules: the game's rules are its own now (spins, clears,
		// kicks, the fuse are fixed or the Forge's gimmicks), and what is
		// left to set is how the game trains and how it is presented.
		if (ImGui::BeginTabItem("Feel")) {
			ImGui::Spacing();
			const char* finesse_rules[] = {
				"Off", "Count faults", "Retry on fault"};
			ImGui::Combo("Finesse", &app.config.finesse_rule, finesse_rules, 3);
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
	ImGui::TextUnformatted("The Fuse");
	ImGui::TextUnformatted(
		"The board plays pure - no clock on your pieces. Three rooms of\n"
		"the Forge burn, and only those three: in a burn room each piece\n"
		"carries a fuse, and when it runs out the piece is slammed down\n"
		"where it stands. Clears refuel the pieces to come; spins, quads\n"
		"and perfect clears refuel hardest. A duel never burns - what\n"
		"presses you there is the foe.");
	ImGui::TextUnformatted("");
	ImGui::TextUnformatted("Flow and Overdrive");
	ImGui::TextUnformatted(
		"In a burning room the Flow rail climbs on quality: spins, quads,\n"
		"back-to-backs, combos and perfect clears fill it. A full rail\n"
		"ignites Overdrive: the fuse freezes, everything you send is\n"
		"multiplied, and every clear also burns a garbage row off your\n"
		"own floor - until it gutters out.");
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
			IM_COL32(255, 122, 46, 90), std::max(1.f, ui(1)));
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
			draw->AddLine(last, point, IM_COL32(255, 122, 46, 255),
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
		IM_COL32(255, 122, 46, 255), text);
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

// Cold Iron breaking, watched rather than announced.
//
// The sim says "freeze" when a row locks solid and says nothing at all
// when it breaks - the shatter arrives as an ordinary clear, which is
// exactly why it looked like one. The mask is public, so the screen keeps
// last frame's copy and shatters whatever stopped being iron: no sim
// change, nothing graded, and the moment the room is named after finally
// looks like breaking instead of burning.
void watch_the_ice (App& app) {
	const Sim& sim = app.session->sim();
	if (!sim.config().cold_iron) {
		app.was_iron.fill(false);
		return;
	}
	for (int y = 0; y < kHeight; ++y) {
		const bool iron = sim.board().iron_row(y);
		if (app.was_iron[static_cast<size_t>(y)] && !iron) {
			// It went. Pale shards off every cell of the row, a crack of
			// white along it, and a short cold jolt - ice does not rumble
			// the way a cascade does, it snaps.
			const float mid = kBoardY + (y + 0.5f) * kCell;
			for (int x = 0; x < kWidth; ++x) {
				spawn_shards(app, kBoardX + (x + 0.5f) * kCell, mid,
					{206, 236, 255, 255}, 3, 3.4f,
					static_cast<float>(kCell) * 0.22f);
			}
			spawn_sparks_at(app, kBoardX + kBoardW * 0.5f, mid,
				{236, 250, 255, 255}, 6, 2.6f);
			App::BurnRow& row = app.burn_rows[app.burn_at];
			app.burn_at = (app.burn_at + 1) % app.burn_rows.size();
			row.row = y;
			row.life = 10;
			row.cold = true;
			app.shake_until = std::max(app.shake_until, sim.frame() + 4);
		}
		app.was_iron[static_cast<size_t>(y)] = iron;
	}
}

// Garbage arriving, watched rather than announced.
//
// The sim raises the queued rows on a lock that clears nothing and says
// nothing about it, so the flood used to slide in silently under the
// stack. The queue is public: when it shrinks, that many rows just came
// up from the floor, and the floor gets the impact it deserves.
void watch_the_floor (App& app) {
	const Sim& sim = app.session->sim();
	const int now = sim.pending_garbage();
	const int rose = app.was_pending - now;
	app.was_pending = now;
	if (rose <= 0) {
		return;
	}
	// Dust off the floor line, thrown up the way rubble goes when
	// something heavy lands beneath it, and a jolt that grows with the
	// size of the blow but never outshouts a quad.
	const float floor = static_cast<float>(kBoardY + kBoardH);
	for (int x = 0; x < kWidth; ++x) {
		spawn_shards(app, kBoardX + (x + 0.5f) * kCell, floor,
			{186, 142, 108, 255}, std::min(3, 1 + rose), 2.4f + rose * 0.3f,
			static_cast<float>(kCell) * 0.22f);
	}
	spawn_sparks_at(app, kBoardX + kBoardW * 0.5f, floor,
		{214, 138, 82, 255}, 4 + rose, 2.4f);
	spawn_ring(app, kBoardX + kBoardW * 0.5f, floor, {214, 138, 82, 255},
		static_cast<float>(kBoardW) * 0.5f, 14);
	app.shake_until = std::max(app.shake_until,
		sim.frame() + std::min(7, 2 + rose));
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
	// Which rows are going, counted before any of them is drawn: what a
	// clear is worth is how many rows it took, and the size of the effect
	// should say so before a number on the HUD does.
	std::array<int, kHeight> going{};
	int count = 0;
	for (int y = 0; y < kHeight; ++y) {
		bool full = true;
		for (int x = 0; x < kWidth; ++x) {
			if (sim.board().at(x, y) < 0) {
				full = false;
				break;
			}
		}
		if (full) {
			going[static_cast<size_t>(count++)] = y;
		}
	}
	// A single row is the game's heartbeat and stays quiet; two start
	// throwing debris, and a quad throws gold off every cell it took.
	const bool quad = count >= 4;
	const SDL_Color grit = quad ? SDL_Color{255, 222, 120, 255}
		: SDL_Color{226, 168, 104, 255};
	const int per_cell = count >= 4 ? 2 : (count >= 2 ? 1 : 0);
	for (int i = 0; i < count; ++i) {
		const int y = going[static_cast<size_t>(i)];
		App::BurnRow& row = app.burn_rows[app.burn_at];
		app.burn_at = (app.burn_at + 1) % app.burn_rows.size();
		row.row = y;
		row.life = 14;
		row.cold = false;
		spawn_sparks_at(app, static_cast<float>(kBoardX),
			kBoardY + (y + 0.5f) * kCell, {255, 236, 190, 255}, 3, 2.4f);
		spawn_sparks_at(app, static_cast<float>(kBoardX + kBoardW),
			kBoardY + (y + 0.5f) * kCell, {255, 236, 190, 255}, 3, 2.4f);
		if (per_cell == 0) {
			continue;
		}
		const float mid = kBoardY + (y + 0.5f) * kCell;
		for (int x = 0; x < kWidth; ++x) {
			spawn_shards(app, kBoardX + (x + 0.5f) * kCell, mid, grit,
				per_cell, quad ? 3.6f : 2.6f,
				static_cast<float>(kCell) * (quad ? 0.24f : 0.18f));
		}
	}
}

void draw_burn_rows (App& app) {
	SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
	for (App::BurnRow& row : app.burn_rows) {
		if (row.life <= 0) {
			continue;
		}
		--row.life;
		if (row.cold) {
			// Ice: a crack of white that runs the row's full width the
			// instant it goes, then thins to nothing. It does not spread
			// out of the middle the way a burn does - it is already
			// broken along its whole length.
			const double part = row.life / 10.;
			const int thin = std::max(1,
				static_cast<int>(kCell * (0.15 + 0.5 * part)));
			fill(app.renderer, kBoardX,
				kBoardY + row.row * kCell + (kCell - thin) / 2,
				kBoardW, thin,
				{static_cast<Uint8>(214 + 40 * part), 240, 255,
				 static_cast<Uint8>(240 * part)});
			continue;
		}
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
				{255, 176, 60, 255}, 3 + std::min(streak.rows, 6), 2.6f);
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

// A nine-slice through ImGui's draw list, for art that has to live inside
// a window (the SDL pass is under every window, so plates drawn there
// would vanish behind the panel).
void nine_patch (ImDrawList* dl, SDL_Texture* tex, ImVec2 a, ImVec2 b,
		float src_border, float dst_border, ImU32 tint = IM_COL32_WHITE) {
	int tw = 0;
	int th = 0;
	SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
	if (tw <= 0 || th <= 0) {
		return;
	}
	const float u = src_border / tw;
	const float v = src_border / th;
	const float d = std::min({dst_border, (b.x - a.x) / 2, (b.y - a.y) / 2});
	const float xs[4] = {a.x, a.x + d, b.x - d, b.x};
	const float ys[4] = {a.y, a.y + d, b.y - d, b.y};
	const float us[4] = {0.f, u, 1.f - u, 1.f};
	const float vs[4] = {0.f, v, 1.f - v, 1.f};
	for (int row = 0; row < 3; ++row) {
		for (int col = 0; col < 3; ++col) {
			dl->AddImage(reinterpret_cast<ImTextureID>(tex),
				ImVec2(xs[col], ys[row]), ImVec2(xs[col + 1], ys[row + 1]),
				ImVec2(us[col], vs[row]), ImVec2(us[col + 1], vs[row + 1]),
				tint);
		}
	}
}

// The molten bed under the map.
//
// A run's tree used to stand on a flat panel, which is why the whole
// screen read as a diagram - a list of rooms with lines between them
// rather than a road out of a forge. The lava is drawn behind the nodes:
// a pool at the foot, veins climbing between the lanes, and a heat that
// runs high while the maul's shock is travelling and settles to an ember
// glow once it has passed.
//
// Everything here is a pure function of the frame count and the map's own
// extent, so it costs nothing to keep running and nothing to leave out.
void draw_lava_bed (App& app, ImDrawList* bed) {
	const float lo = app.map_span.x;
	const float hi = app.map_span.y;
	const float foot = app.map_foot.y + ui(10);
	const float head = app.map_head.y - ui(16);
	if (hi <= lo || foot <= head) {
		return;
	}
	// Hot while the blow's shock is climbing, ember the rest of the time.
	const float heat = app.forge_strike > 0
		? 0.45f + 0.55f * (app.forge_strike / static_cast<float>(kStrike))
		: 0.45f;
	const float clock = app.map_clock;
	const float tall = foot - head;
	// The pool: a deep glow banked along the foot, brightest at the very
	// bottom and gone about a third of the way up.
	bed->AddRectFilledMultiColor(ImVec2(lo, foot - tall * 0.42f),
		ImVec2(hi, foot),
		IM_COL32(120, 34, 8, 0), IM_COL32(120, 34, 8, 0),
		IM_COL32(214, 74, 16, static_cast<int>(150 * heat)),
		IM_COL32(214, 74, 16, static_cast<int>(150 * heat)));
	bed->AddRectFilledMultiColor(ImVec2(lo, foot - tall * 0.16f),
		ImVec2(hi, foot),
		IM_COL32(255, 132, 30, 0), IM_COL32(255, 132, 30, 0),
		IM_COL32(255, 168, 52, static_cast<int>(190 * heat)),
		IM_COL32(255, 168, 52, static_cast<int>(190 * heat)));
	// The veins: slow molten runs climbing between the lanes, each one
	// wobbling on its own phase so the bed moves without anything in it
	// being animation. They thin and dim as they rise, the way a run of
	// metal does when it is losing its heat.
	const int veins = 5;
	for (int at = 0; at < veins; ++at) {
		const float seat = (at + 0.5f) / veins;
		const float base = lo + (hi - lo) * seat;
		const float sway = ui(44) + ui(26) * std::sin(at * 2.1f);
		const float lead = at * 1.7f + clock * 0.6f;
		ImVec2 was(base, foot);
		const int steps = 12;
        for (int step = 1; step <= steps; ++step) {
			const float up = step / static_cast<float>(steps);
			const float y = foot - tall * up * 0.86f;
			const float x = base + std::sin(lead + up * 3.1f) * sway * up;
			const int ink = static_cast<int>(
				(1.f - up) * (1.f - up) * 190 * heat);
			if (ink > 3) {
				bed->AddLine(was, ImVec2(x, y),
					IM_COL32(255, 146, 40, ink),
					ui(7) * (1.f - up * 0.75f));
				bed->AddLine(was, ImVec2(x, y),
					IM_COL32(255, 226, 170, ink / 2),
					ui(2.5f) * (1.f - up * 0.8f));
			}
			was = ImVec2(x, y);
		}
	}
}

// A number with its coin: the icon when the art exists, then the figure.
void coin_stat (const char* icon, const ImVec4& ink, const char* text) {
	if (SDL_Texture* tex = gfx::get(icon)) {
		ImGui::Image(reinterpret_cast<ImTextureID>(tex),
			ImVec2(ui(18), ui(18)));
		ImGui::SameLine(0.f, ui(5));
	}
	ImGui::TextColored(ink, "%s", text);
}

void draw_career (App& app) {
	ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2, ui(24)),
		ImGuiCond_Always, ImVec2(0.5f, 0.f));
	// Wide enough for the map's widest row. The tree grew to three or four
	// lanes when it stopped funnelling into one boss, and at the old width
	// the right-hand lane ran off the panel.
	const float wide
		= std::min(ui(680), ImGui::GetIO().DisplaySize.x - ui(16));
	ImGui::SetNextWindowSizeConstraints(ImVec2(wide, 0),
		ImVec2(wide, ImGui::GetIO().DisplaySize.y - ui(48)));
	ImGui::Begin("Career", nullptr, ImGuiWindowFlags_AlwaysAutoResize
		| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
		| ImGuiWindowFlags_NoSavedSettings);
	forge_panel(app);
	ImGui::PushFont(app.fonts.head);
	ImGui::TextUnformatted("The Forge Map");
	ImGui::PopFont();
	{
		char coin[32];
		std::snprintf(coin, sizeof coin, "SLAG %d", app.campaign.slag);
		coin_stat("slag", ImVec4(1.f, 0.76f, 0.42f, 1.f), coin);
	}
	campaign::Run& run = app.campaign.run;
	if (run.active && app.run_map.empty()) {
		// Resumed from the file: the graph stands back up from its two
		// numbers, exactly as it stood when the run was put down.
		app.run_map = run.endless
			? campaign::build_endless_map(run.ring, run.seed)
			: campaign::build_map(run.chapter, run.seed);
	}
	if (app.map_reward && app.offers.empty()) {
		// The spoils owed from the last battle, dealt the moment the map
		// is back on the table.
		deal_reward(app);
	}
	if (!run.active) {
		// --- The door: pick a chapter, pick how hot, set out. -----------
		ImGui::TextDisabled("A chapter is one climb: pick a path up the");
		ImGui::TextDisabled("map, and your spoils ride the whole run.");
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		ImGui::TextUnformatted("The fire");
		struct Fire {
			int level;
			const char* name;
			const char* note;
		};
		const Fire fires[3] = {
			{campaign::kMild, "Mild",
				"a death re-offers the node; foes fight a rung down"},
			{campaign::kForged, "Forged",
				"three lives, half again the slag; foes as written"},
			{campaign::kWhite, "White-hot",
				"one death ends it, double slag; foes a rung up"},
		};
		for (const Fire& fire : fires) {
			if (ImGui::RadioButton(fire.name, &app.pick_difficulty,
				fire.level)) {
			}
			ImGui::SameLine();
			ImGui::TextDisabled("- %s", fire.note);
		}
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		int flat = 0;
		for (size_t c = 0; c < campaign::chapters().size(); ++c) {
			const campaign::Chapter& chapter = campaign::chapters()[c];
			const bool open_chapter
				= campaign::chapter_open(app.campaign, static_cast<int>(c));
			int stars = 0;
			for (int s = 0; s < chapter.stages; ++s) {
				const auto found = app.campaign.stars.find(
					campaign::stages()[static_cast<size_t>(flat + s)].id);
				if (found != app.campaign.stars.end()) {
					stars += found->second;
				}
			}
			ImGui::PushID(static_cast<int>(c));
			ImGui::PushFont(app.fonts.head);
			ImGui::TextColored(open_chapter
				? ImVec4(0.93f, 0.87f, 0.8f, 1.f)
				: ImVec4(0.45f, 0.42f, 0.4f, 1.f),
				"Chapter %d - %s", static_cast<int>(c) + 1, chapter.name);
			ImGui::PopFont();
			if (open_chapter) {
				ImGui::TextDisabled("%s", chapter.blurb);
				{
					char tally[32];
					std::snprintf(tally, sizeof tally, "%d / %d", stars,
						chapter.stages * 3);
					coin_stat("star", ImVec4(1.f, 0.84f, 0.38f, 1.f), tally);
				}
				ImGui::SameLine();
				if (ImGui::Button("Set out", ImVec2(ui(100), 0))) {
					begin_run(app, static_cast<int>(c),
						app.pick_difficulty, app.seeds());
				}
			} else {
				ImGui::TextDisabled(
					"Locked - beat the chapter before it.");
			}
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			ImGui::PopID();
			flat += chapter.stages;
		}
		// --- The Endless Climb, past the chapters. ----------------------
		{
			const bool open_climb = campaign::endless_open(app.campaign);
			ImGui::PushFont(app.fonts.head);
			ImGui::TextColored(open_climb
				? ImVec4(0.93f, 0.87f, 0.8f, 1.f)
				: ImVec4(0.45f, 0.42f, 0.4f, 1.f), "The Endless Climb");
			ImGui::PopFont();
			if (open_climb) {
				ImGui::TextDisabled(
					"Rings without end, at white heat. One death.");
				if (app.campaign.endless_best > 0) {
					char best[32];
					std::snprintf(best, sizeof best, "BEST %d rows",
						app.campaign.endless_best);
					coin_stat("star", ImVec4(1.f, 0.84f, 0.38f, 1.f), best);
					ImGui::SameLine();
				}
				if (ImGui::Button("Set out##endless", ImVec2(ui(100), 0))) {
					begin_run(app, 0, campaign::kWhite, app.seeds(), true);
				}
			} else {
				ImGui::TextDisabled("Locked - fell the Forgemaster first.");
			}
			ImGui::Dummy(ImVec2(0.f, ui(4)));
		}
	} else {
		// --- The climb: the map, entrance at the bottom, boss at the top.
		if (run.endless) {
			ImGui::Text("The Endless Climb - Ring %d", run.ring + 1);
		} else {
			const campaign::Chapter& chapter
				= campaign::chapters()[static_cast<size_t>(run.chapter)];
			ImGui::Text("Chapter %d - %s", run.chapter + 1, chapter.name);
		}
		{
			char coin[32];
			std::snprintf(coin, sizeof coin, "EMBERS %d", run.embers);
			coin_stat("ember", ImVec4(1.f, 0.76f, 0.42f, 1.f), coin);
		}
		ImGui::SameLine();
		if (run.endless) {
			// The record, live: the rows under your feet against the most
			// any climb has managed.
			ImGui::TextColored(ImVec4(1.f, 0.84f, 0.38f, 1.f), "ROW %d",
				campaign::endless_rows(run));
			ImGui::SameLine();
			ImGui::TextDisabled("BEST %d", app.campaign.endless_best);
			// What the ring just took. Said in the header rather than in a
			// panel that has to be dismissed: the player did not agree to
			// this, so it should be readable without a click, and it stands
			// there until the next ring replaces it.
			if (!app.curse_shown.empty()) {
				const temper::Temper* laid = temper::find(app.curse_shown);
				if (laid != nullptr) {
					ImGui::TextColored(ImVec4(0.85f, 0.42f, 0.9f, 1.f),
						"THE RING TAKES: %s - %s", laid->name, laid->text);
				}
			}
			if (temper::curses_by(run.ring)
				>= static_cast<int>(temper::curses().size())) {
				ImGui::TextDisabled(
					"Every curse is down. From here the rooms tighten.");
			}
		} else {
			ImGui::TextDisabled("%s fire", campaign::difficulty_name(
				run.difficulty));
		}
		if (run.difficulty == campaign::kForged) {
			ImGui::SameLine();
			ImGui::TextColored(ImVec4(1.f, 0.6f, 0.4f, 1.f), "LIVES %d",
				run.lives);
		}
		{
			// The oils: coin spent here on the map, felt in the next battle
			// entered, consumed as the doors close. One coat of each at a
			// time - painting twice would just run off.
			const auto holds = [&run] (const char* id) {
				return std::find(run.oils.begin(), run.oils.end(),
					std::string(id)) != run.oils.end();
			};
			const auto oil_button = [&] (const char* id, const char* name,
					int cost, const char* promise) {
				char label[48];
				if (holds(id)) {
					std::snprintf(label, sizeof label, "%s (painted)", name);
				} else {
					std::snprintf(label, sizeof label, "%s (%d)", name, cost);
				}
				ImGui::BeginDisabled(holds(id) || run.embers < cost);
				if (ImGui::SmallButton(label)) {
					run.embers -= cost;
					run.oils.emplace_back(id);
					app.audio.play("hold");
					campaign::save(campaign::path(app.root), app.campaign);
				}
				ImGui::EndDisabled();
				if (ImGui::IsItemHovered(
					ImGuiHoveredFlags_AllowWhenDisabled)) {
					ImGui::SetTooltip("%s", promise);
				}
			};
			oil_button("hot", "Hot Oil",
				ember_price(app, temper::kHotOilCost),
				"Next battle: your attacks hit harder.");
			ImGui::SameLine();
			oil_button("frost", "Frost Oil",
				ember_price(app, temper::kFrostOilCost),
				"Next duel: the foe's clears freeze solid.");
		}
		if (!run.tempers.empty()) {
			ImGui::PushTextWrapPos(0.f);
			ImGui::TextDisabled("%s", temper_line(run.tempers).c_str());
			ImGui::PopTextWrapPos();
		}
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		// The rows, boss first so the climb reads bottom-to-top, with
		// every node's rectangle remembered so the edges can be drawn in
		// the gaps between rows afterwards.
		// The molten bed the tree stands in, drawn UNDER the nodes: the
		// rows are ImGui buttons and go down after this, so the window's
		// draw list is split and the lava laid on the lower channel. It
		// runs hot while the maul's shock is climbing and settles to an
		// ember glow afterwards, so the map is never a diagram on a flat
		// panel - it is a thing standing over a forge.
		ImDrawList* bed = ImGui::GetWindowDrawList();
		bed->ChannelsSplit(2);
		bed->ChannelsSetCurrent(1);
		std::vector<ImVec2> tops(app.run_map.size());
		std::vector<ImVec2> bottoms(app.run_map.size());
		// Narrower plates than the two-lane map wore: four of them and
		// their gaps have to sit inside the panel on a phone too.
		const float node_w = ui(150);
		const float node_h = ui(34);
		const float row_gap = ui(26);
		const float lane_gap = ui(10);
		const float panel_w = ImGui::GetContentRegionAvail().x;
		for (int r = campaign::kMapDepth - 1; r >= 0; --r) {
			int lanes = 0;
			for (const campaign::MapNode& node : app.run_map) {
				lanes += node.depth == r ? 1 : 0;
			}
			const float row_w = lanes * node_w + (lanes - 1) * lane_gap;
			float x = ImGui::GetStyle().WindowPadding.x
				+ std::max(0.f, (panel_w - row_w) / 2);
			bool first_in_row = true;
			for (size_t at = 0; at < app.run_map.size(); ++at) {
				const campaign::MapNode& node = app.run_map[at];
				if (node.depth != r) {
					continue;
				}
				// What the button says and promises, by kind: a battle
				// carries its stage's name and gimmick, a stop its own.
				const bool stop = node.kind == 2 || node.kind == 3;
				const campaign::Stage* stage = stop ? nullptr
					: &campaign::stages()[static_cast<size_t>(node.stage)];
				// The watch keeps its face until the run is a row away.
				// Which node holds a boss and which a miniboss is plain
				// from the first look - the icon and the tint say so -
				// but who stands there is only learned on approach, and
				// a chapter fields three possible pairs.
				const bool hidden = (node.kind == 1 || node.kind == 4)
					&& run.depth + 1 < node.depth;
				const char* label = node.kind == 2 ? "The Forge"
					: node.kind == 3 || hidden ? "? ? ?" : stage->name;
				const char* promise = node.kind == 2
					? "No fight here: draw a temper, melt one down."
					: node.kind == 3 ? "Something waits. It never fights."
					: hidden ? "The watch, unnamed until you stand below it."
					: stage->blurb;
				const bool taken = std::find(run.path.begin(),
					run.path.end(), static_cast<int>(at)) != run.path.end();
				const bool pickable
					= node_pickable(app, static_cast<int>(at));
				if (!first_in_row) {
					ImGui::SameLine();
				}
				first_in_row = false;
				ImGui::SetCursorPosX(x);
				x += node_w + lane_gap;
				ImGui::PushID(static_cast<int>(at));
				// The node as a struck plate: the frame art, the kind's own
				// icon, and the name beside it. A fought node wears warm
				// gold, the boss burns, a stop cools toward steel, and what
				// cannot be reached falls into shadow.
				const ImVec2 pen = ImGui::GetCursorScreenPos();
				ImGui::PushStyleColor(ImGuiCol_Button,
					ImVec4(0.f, 0.f, 0.f, 0.f));
				ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
					ImVec4(0.f, 0.f, 0.f, 0.f));
				ImGui::PushStyleColor(ImGuiCol_ButtonActive,
					ImVec4(0.f, 0.f, 0.f, 0.f));
				ImGui::BeginDisabled(!pickable || !app.offers.empty()
					|| app.visiting >= 0);
				const bool went = ImGui::Button("##node",
					ImVec2(node_w, node_h));
				ImGui::EndDisabled();
				ImGui::PopStyleColor(3);
				const bool hot = ImGui::IsItemHovered(
					ImGuiHoveredFlags_AllowWhenDisabled);
				ImDrawList* pdl = ImGui::GetWindowDrawList();
				const ImVec2 plow(pen.x + node_w, pen.y + node_h);
				ImU32 tint = IM_COL32_WHITE;
				if (taken) {
					tint = IM_COL32(255, 214, 150, 255);
				} else if (!pickable) {
					tint = IM_COL32(105, 100, 95, 255);
				} else if (node.kind == 1) {
					tint = IM_COL32(255, 200, 120, 255);
				} else if (node.kind == 4) {
					// The miniboss: bloodied steel, hotter than a stop,
					// colder than the boss's gold.
					tint = IM_COL32(240, 165, 150, 255);
				} else if (stop) {
					tint = IM_COL32(190, 205, 220, 255);
				}
				if (SDL_Texture* plate = gfx::get("plate")) {
					nine_patch(pdl, plate, pen, plow, 14.f, ui(10), tint);
				} else {
					pdl->AddRectFilled(pen, plow, taken
						? IM_COL32(122, 84, 40, 255)
						: pickable ? IM_COL32(64, 48, 36, 255)
						           : IM_COL32(42, 36, 32, 255), ui(5));
				}
				if (pickable) {
					const float beat = 0.5f
						+ 0.5f * std::sin(app.backdrop_tick * 0.06f);
					pdl->AddRect(pen, plow, hot
						? IM_COL32(255, 122, 46, 240)
						: IM_COL32(255, 122, 46,
							static_cast<int>(90 + 110 * beat)),
						ui(5), 0, ui(2));
				}
				const char* face = node.kind == 1 ? "node_boss"
					: node.kind == 2 ? "node_forge"
					: node.kind == 3 ? "node_event"
					: node.kind == 4 ? "node_mini" : "node_battle";
				// The kind's icon shows on every node, reachable or not -
				// seeing where the forge and the events wait is what picking
				// a path is about.
				float name_x = pen.x + ui(8);
				if (SDL_Texture* mark = gfx::get(face)) {
					const float s = node_h - ui(10);
					pdl->AddImage(reinterpret_cast<ImTextureID>(mark),
						ImVec2(pen.x + ui(5), pen.y + ui(5)),
						ImVec2(pen.x + ui(5) + s, pen.y + ui(5) + s),
						ImVec2(0, 0), ImVec2(1, 1), tint);
					name_x = pen.x + ui(9) + s;
				}
				const ImU32 name_ink = !pickable && !taken
					? IM_COL32(120, 112, 104, 255)
					: node.kind == 1 ? IM_COL32(255, 214, 94, 255)
					: IM_COL32(235, 223, 206, 255);
				pdl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.84f,
					ImVec2(name_x, pen.y
						+ (node_h - ImGui::GetFontSize() * 0.84f) / 2),
					name_ink, label);
				if (went) {
					enter_node(app, static_cast<int>(at));
				}
				if (hot) {
					// The goal first and plainly, then what makes this
					// room itself. They used to be one sentence, which
					// asked a new player to work out which half was the
					// rule - and let a number written by hand drift from
					// the number the stage enforces.
					if (stage != nullptr) {
						ImGui::BeginTooltip();
						ImGui::PushFont(app.fonts.head);
						ImGui::TextColored(ImVec4(1.f, 0.84f, 0.38f, 1.f),
							"%s", campaign::goal_line(*stage).c_str());
						ImGui::PopFont();
						ImGui::PushTextWrapPos(ui(320));
						ImGui::TextDisabled("%s", promise);
						ImGui::PopTextWrapPos();
						ImGui::EndTooltip();
					} else {
						ImGui::SetTooltip("%s", promise);
					}
				}
				tops[at] = ImVec2(
					(ImGui::GetItemRectMin().x
						+ ImGui::GetItemRectMax().x) / 2,
					ImGui::GetItemRectMin().y);
				bottoms[at] = ImVec2(tops[at].x,
					ImGui::GetItemRectMax().y);
				ImGui::PopID();
			}
			if (r > 0) {
				ImGui::Dummy(ImVec2(0.f, row_gap));
			}
		}
		// The foot of the tree is where a run starts and where the maul
		// lands, so that is what the map shows while the blow is in the
		// air - here, at the cursor just under the bottom row, rather than
		// at the window's own end, which is the Anvil and the Daily.
		// Only for the first frames, so the view is the player's again the
		// moment the blow is over.
		if (app.forge_strike > kStrike - 4) {
			// Not flush to the bottom: the anvil stands below the foot of the
			// tree and needs the room.
			ImGui::SetScrollHereY(0.78f);
		}
		// Where the tree stands, remembered for the maul: the foot of it
		// is the bottom row's middle, which is where a run is started and
		// so where the blow lands.
		{
			float foot_y = 0.f;
			float head_y = FLT_MAX;
			float lo = FLT_MAX;
			float hi = 0.f;
			int feet = 0;
			float foot_x = 0.f;
			for (size_t at = 0; at < app.run_map.size(); ++at) {
				lo = std::min(lo, tops[at].x);
				hi = std::max(hi, tops[at].x);
				head_y = std::min(head_y, tops[at].y);
				foot_y = std::max(foot_y, bottoms[at].y);
				if (app.run_map[at].depth == 0) {
					foot_x += bottoms[at].x;
					++feet;
				}
			}
			if (feet > 0) {
				app.map_foot = ImVec2(foot_x / feet, foot_y);
				app.map_head = ImVec2((lo + hi) / 2, head_y);
				app.map_span = ImVec2(lo - ui(90), hi + ui(90));
				app.map_seen = true;
			}
		}
		// And now the bed, on the lower channel, with the extent the rows
		// just told us.
		bed->ChannelsSetCurrent(0);
		if (app.map_seen) {
			draw_lava_bed(app, bed);
		}
		bed->ChannelsMerge();
		// The edges, drawn in the gaps: the path walked in bright gold,
		// the doors open right now in ember orange, the rest as ash.
		ImDrawList* draw = ImGui::GetWindowDrawList();
		for (size_t at = 0; at < app.run_map.size(); ++at) {
			const bool from_taken = std::find(run.path.begin(),
				run.path.end(), static_cast<int>(at)) != run.path.end();
			const bool from_here = !run.path.empty()
				&& run.path.back() == static_cast<int>(at);
			for (const int to : app.run_map[at].next) {
				const bool to_taken = std::find(run.path.begin(),
					run.path.end(), to) != run.path.end();
				ImU32 ink = IM_COL32(90, 74, 60, 120);
				ImU32 halo = 0;
				float thick = ui(1);
				if (from_taken && to_taken) {
					ink = IM_COL32(255, 214, 94, 230);
					halo = IM_COL32(255, 176, 60, 70);
					thick = ui(2.5f);
				} else if ((from_here
						|| (run.path.empty()
							&& app.run_map[at].depth == 0))
					&& node_pickable(app, to)) {
					ink = IM_COL32(240, 140, 58, 220);
					halo = IM_COL32(240, 120, 50, 60);
					thick = ui(2);
				}
				// The glow is a fat soft pass under a thin bright core -
				// a lit chain, not a diagram's line.
				if (halo != 0) {
					draw->AddLine(tops[at],
						bottoms[static_cast<size_t>(to)], halo, thick * 3.f);
				}
				draw->AddLine(tops[at], bottoms[static_cast<size_t>(to)],
					ink, thick);
			}
		}
		ImGui::Dummy(ImVec2(0.f, ui(4)));
		ImGui::BeginDisabled(app.visiting >= 0 || !app.offers.empty());
		if (ImGui::SmallButton("Abandon the climb")) {
			ImGui::OpenPopup("abandon");
		}
		ImGui::EndDisabled();
		if (ImGui::BeginPopup("abandon")) {
			ImGui::TextUnformatted("Put the run down? The embers render");
			ImGui::TextUnformatted("to slag; the map is lost.");
			if (ImGui::Button("Abandon", ImVec2(ui(100), 0))) {
				end_run(app, false);
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Keep climbing", ImVec2(ui(120), 0))) {
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
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
	ImGui::TextDisabled("One Ignition run a day, same pieces for everyone");
	ImGui::TextDisabled("who shares the date. Leaving still spends it.");
	ImGui::Dummy(ImVec2(0.f, ui(2)));
	if (app.career.daily_date == today()) {
		if (app.career.daily_score >= 0) {
			ImGui::Text("Today's run: %lld", app.career.daily_score);
		} else {
			ImGui::TextUnformatted("Today's run is spent.");
		}
	} else if (ImGui::Button("Run today's seed", ImVec2(ui(200), 0))) {
		start_daily(app);
	}
	ImGui::Separator();
	if (ImGui::Button("Back", ImVec2(ui(140), 0))) {
		app.screen = Screen::Menu;
	}
	ImGui::End();

	if (app.visiting >= 0 && app.offers.empty()
		&& app.visiting < static_cast<int>(app.run_map.size())) {
		// A stop's own room, over the map. While a forge hand is on the
		// table the spoils overlay (drawn later) covers this window; it
		// comes back when the cards are gone.
		const campaign::MapNode& node
			= app.run_map[static_cast<size_t>(app.visiting)];
		campaign::Run& visited = app.campaign.run;
		ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x / 2,
			ImGui::GetIO().DisplaySize.y / 2), ImGuiCond_Always,
			ImVec2(0.5f, 0.5f));
		ImGui::Begin("visit", nullptr, ImGuiWindowFlags_AlwaysAutoResize
			| ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse
			| ImGuiWindowFlags_NoTitleBar
			| ImGuiWindowFlags_NoSavedSettings);
		forge_panel(app);
		if (node.kind == 2) {
			ImGui::PushFont(app.fonts.head);
			ImGui::TextUnformatted("The Forge");
			ImGui::PopFont();
			ImGui::TextDisabled("No fight in this room. The smith deals a");
			ImGui::TextDisabled("free hand, and melts what you regret.");
			ImGui::TextColored(ImVec4(1.f, 0.76f, 0.42f, 1.f), "EMBERS %d",
				visited.embers);
			ImGui::Spacing();
			ImGui::BeginDisabled(app.forge_hand_used);
			if (ImGui::Button(app.forge_hand_used
				? "Hand drawn" : "Draw a hand (free)", ImVec2(ui(200), 0))) {
				app.forge_hand_used = true;
				deal_reward(app);
			}
			ImGui::EndDisabled();
			if (!visited.tempers.empty()) {
				ImGui::Spacing();
				ImGui::TextUnformatted("Melt down");
				int melted = -1;
				int paid = 0;
				for (size_t i = 0; i < visited.tempers.size(); ++i) {
					const temper::Temper* card
						= temper::find(visited.tempers[i]);
					// A curse costs most of a good room to burn off. It is
					// what the ring did to you, not something you picked,
					// and one you could shed for the price of a card would
					// not be a difficulty at all.
					const bool cursed = card != nullptr
						&& card->family == temper::Family::Chaos;
					const int melt_cost = ember_price(app, cursed
						? temper::kCurseCost : temper::kRemoveCost);
					ImGui::PushID(static_cast<int>(i));
					if (cursed) {
						ImGui::TextColored(ImVec4(0.85f, 0.42f, 0.9f, 1.f),
							"%s", card->name);
					} else {
						ImGui::TextUnformatted(card != nullptr
							? card->name : visited.tempers[i].c_str());
					}
					ImGui::SameLine();
					ImGui::BeginDisabled(visited.embers < melt_cost);
					char label[32];
					std::snprintf(label, sizeof label, cursed
						? "Burn off (%d)" : "Remove (%d)", melt_cost);
					if (ImGui::SmallButton(label)) {
						melted = static_cast<int>(i);
						paid = melt_cost;
					}
					ImGui::EndDisabled();
					ImGui::PopID();
				}
				if (melted >= 0) {
					visited.embers -= paid;
					visited.tempers.erase(visited.tempers.begin() + melted);
					app.tempers = visited.tempers;
					app.audio.play("clear");
					campaign::save(campaign::path(app.root), app.campaign);
				}
			}
			{
				// Strike again: a second copy of a card already carried,
				// for the builds that live on stacking - offered only
				// where the card's own stack cap leaves room.
				std::vector<std::string> forgeable;
				for (const std::string& id : visited.tempers) {
					const temper::Temper* card = temper::find(id);
					// Never a second copy of a curse: the ring lays those,
					// and the shop is not in that trade.
					if (card == nullptr
						|| card->family == temper::Family::Chaos) {
						continue;
					}
					const long held = std::count(visited.tempers.begin(),
						visited.tempers.end(), id);
					const bool listed = std::find(forgeable.begin(),
						forgeable.end(), id) != forgeable.end();
					if (held < card->stacks && !listed) {
						forgeable.push_back(id);
					}
				}
				if (!forgeable.empty()) {
					ImGui::Spacing();
					ImGui::TextUnformatted("Strike again");
					const int copy_cost
						= ember_price(app, temper::kDuplicateCost);
					std::string struck;
					for (size_t i = 0; i < forgeable.size(); ++i) {
						const temper::Temper* card
							= temper::find(forgeable[i]);
						ImGui::PushID(static_cast<int>(i + 100));
						ImGui::TextUnformatted(card->name);
						ImGui::SameLine();
						ImGui::BeginDisabled(visited.embers < copy_cost);
						char label[32];
						std::snprintf(label, sizeof label,
							"Duplicate (%d)", copy_cost);
						if (ImGui::SmallButton(label)) {
							struck = forgeable[i];
						}
						ImGui::EndDisabled();
						ImGui::PopID();
					}
					if (!struck.empty()) {
						visited.embers -= copy_cost;
						visited.tempers.push_back(struck);
						app.tempers = visited.tempers;
						app.audio.play("crit");
						campaign::save(campaign::path(app.root),
							app.campaign);
					}
				}
			}
			if (visited.difficulty == campaign::kForged) {
				// A life bought back on forged fire - once a visit, and
				// priced so it is a decision, not an errand.
				ImGui::Spacing();
				const int life_cost = ember_price(app, temper::kLifeCost);
				ImGui::BeginDisabled(app.forge_life_used
					|| visited.embers < life_cost
					|| visited.lives >= 9);
				char label[40];
				if (app.forge_life_used) {
					std::snprintf(label, sizeof label, "Life bought");
				} else {
					std::snprintf(label, sizeof label,
						"A life for the fire (%d)", life_cost);
				}
				if (ImGui::Button(label, ImVec2(ui(200), 0))) {
					visited.embers -= life_cost;
					visited.lives += 1;
					app.forge_life_used = true;
					app.audio.play("perfect");
					campaign::save(campaign::path(app.root), app.campaign);
				}
				ImGui::EndDisabled();
			}
			ImGui::Spacing();
			if (ImGui::Button("Leave", ImVec2(ui(140), 0))) {
				leave_visit(app);
			}
		} else {
			const int id = event_of(app, app.visiting);
			const MapEvent& event = kMapEvents[id];
			ImGui::PushFont(app.fonts.head);
			ImGui::TextUnformatted(event.name);
			ImGui::PopFont();
			ImGui::TextDisabled("%s", event.text);
			ImGui::Spacing();
			// The Tithe asks for coin it may not have; the button stays
			// readable but quiet rather than promising what cannot happen.
			ImGui::BeginDisabled(id == 1 && visited.embers < 10);
			if (ImGui::Button(event.deed, ImVec2(ui(220), 0))) {
				apply_event(app, id);
				leave_visit(app);
			}
			ImGui::EndDisabled();
			if (ImGui::Button("Walk away", ImVec2(ui(220), 0))) {
				leave_visit(app);
			}
		}
		ImGui::End();
	}
}


// A card: a metal plate, an emblem, a name and one line of promise - the
// whole face one button. This is what the menu and the mode picker are
// made of now; without the art it degrades to a clean flat card, so a
// bare checkout still has a working menu.
bool card_button (App& app, const char* icon, const char* name,
		const char* note, float width, float height, bool primary = false) {
	const ImVec2 at = ImGui::GetCursorScreenPos();
	ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.f, 0.f, 0.f, 0.f));
	ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.f, 0.f, 0.f, 0.f));
	ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.f, 0.f, 0.f, 0.f));
	char id[96];
	std::snprintf(id, sizeof id, "##card_%s", name);
	const bool clicked = ImGui::Button(id, ImVec2(width, height));
	ImGui::PopStyleColor(3);
	const bool hot = ImGui::IsItemHovered();
	const bool held = ImGui::IsItemActive();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 low(at.x + width, at.y + height);
	SDL_Texture* plate = gfx::get("plate");
	if (plate != nullptr) {
		nine_patch(dl, plate, at, low, 14.f, ui(12),
			held ? IM_COL32(255, 226, 200, 255)
			     : hot ? IM_COL32(255, 210, 180, 255) : IM_COL32_WHITE);
	} else {
		dl->AddRectFilled(at, low, IM_COL32(32, 25, 20, 255), ui(6));
	}
	if (hot || primary) {
		// The living edge: ember for the pointer, a steady gold breath for
		// the one card the screen most wants pressed.
		const float beat = hot ? 1.f
			: 0.6f + 0.4f * std::sin(app.backdrop_tick * 0.05f);
		dl->AddRect(at, low,
			hot ? IM_COL32(255, 122, 46, 230)
			    : IM_COL32(255, 214, 94, static_cast<int>(120 * beat)),
			ui(6), 0, ui(2));
	}
	const float pad = ui(10);
	float text_x = at.x + pad;
	if (SDL_Texture* art = gfx::get(icon)) {
		const float s = height - pad * 1.6f;
		const float top = at.y + (height - s) / 2;
		dl->AddImage(reinterpret_cast<ImTextureID>(art),
			ImVec2(at.x + pad, top), ImVec2(at.x + pad + s, top + s));
		text_x = at.x + pad * 1.8f + s;
	}
	const ImU32 name_ink = primary ? IM_COL32(255, 214, 94, 255)
		: IM_COL32(235, 223, 206, 255);
	if (note != nullptr && note[0] != '\0') {
		dl->AddText(app.fonts.head, app.fonts.head->FontSize * 0.82f,
			ImVec2(text_x, at.y + height * 0.16f), name_ink, name);
		dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 0.86f,
			ImVec2(text_x, at.y + height * 0.55f),
			IM_COL32(154, 138, 120, 255), note);
	} else {
		dl->AddText(app.fonts.head, app.fonts.head->FontSize * 0.78f,
			ImVec2(text_x, at.y + (height
				- app.fonts.head->FontSize * 0.78f) / 2), name_ink, name);
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
		// The two doors, as cards: the campaign is the game, so it stands
		// first and biggest with a gold breath on its edge.
		if (card_button(app, "em_map", "The Forge Map",
			"Climb the map, forge a build.",
			ui(310), ui(62), true)) {
			app.career = career::load(career::path(app.root));
			app.campaign = campaign::load(campaign::path(app.root));
			app.screen = Screen::Career;
		}
		ImGui::Dummy(ImVec2(0.f, ui(2)));
		if (card_button(app, "em_free", "Quick Play",
			"The yard: six fires, no map.",
			ui(310), ui(54))) {
			app.screen = Screen::Modes;
			app.mode_popup = 0;
		}
		ImGui::Dummy(ImVec2(0.f, ui(8)));
		if (card_button(app, "ic_replay", "Replays", "", ui(310), ui(38))) {
			app.shelf = replay::listing(replay::folder(app.root));
			app.screen = Screen::Replays;
		}
		if (card_button(app, "ic_profile", "Profile", "", ui(310), ui(38))) {
			app.history = profile::load(profile::path(app.root));
			app.screen = Screen::Profile;
		}
		if (card_button(app, "ic_scores", "High scores", "",
			ui(310), ui(38))) {
			app.score_page = 0;
			app.screen = Screen::Scores;
		}
		if (card_button(app, "ic_help", "How to play", "", ui(310), ui(38))) {
			app.help_back = Screen::Menu;
			app.screen = Screen::Help;
		}
		if (card_button(app, "ic_settings", "Settings", "",
			ui(310), ui(38))) {
			app.show_settings = true;
		}
		ImGui::Dummy(ImVec2(0.f, ui(6)));
		if (ImGui::Button("Quit", ImVec2(ui(310), 0))) {
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
			// The letters are the compact handle the buttons need, but a
			// letter teaches nobody: the line under them says the pick in
			// the same word the fight will call it, and what that word
			// costs in pieces a second.
			{
				const bot::Rank& picked = ladder[static_cast<size_t>(
					std::clamp(app.config.bot_rank, 0,
						static_cast<int>(ladder.size()) - 1))];
				ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
					"%s  -  %.2f pieces a second",
					bot::might_of(app.config.bot_rank), picked.pps);
			}
			ImGui::TextDisabled("Garbage, cancelling and surge included.");
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
			// Five modes of button and blurb can still outgrow a short
			// display; the cap turns the overflow into a scrollbar instead
			// of running the last mode off the bottom edge.
			ImGui::SetNextWindowSizeConstraints(ImVec2(ui(300), 0.f),
				ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y - ui(12)));
			ImGui::Begin("mode select", nullptr, box);
			forge_panel(app);
			ImGui::PushFont(app.fonts.head);
			ImGui::TextUnformatted("The Training Yard");
			ImGui::PopFont();
			ImGui::TextDisabled("Five fires, no map. Pick one and play.");
			ImGui::Dummy(ImVec2(0.f, ui(4)));
			const float card_wide = ui(330);
			const float card_tall = ui(54);
			if (card_button(app, "em_free", "Ignition",
				"Endless. Stack for as long as you can.",
				card_wide, card_tall)) {
				start_game(app, 0);
			}
			if (card_button(app, "em_blaze", "Blaze",
				"Three minutes; the multiplier climbs.",
				card_wide, card_tall)) {
				start_game(app, 1);
			}
			if (card_button(app, "em_inferno", "Inferno",
				"The floor rises, faster and faster.",
				card_wide, card_tall)) {
				start_game(app, 2);
			}
			if (card_button(app, "em_cheese", "Meltdown / Bunker",
				"Dig the cheese down, or outlast it.",
				card_wide, card_tall)) {
				app.mode_popup = 1;
			}
			if (card_button(app, "em_duel", "Duel",
				"The bot, rank F through X, own blade.",
				card_wide, card_tall)) {
				app.mode_popup = 2;
			}
			ImGui::Dummy(ImVec2(0.f, ui(6)));
			if (ImGui::Button("Back", ImVec2(card_wide, 0))) {
				app.screen = Screen::Menu;
			}
			ImGui::End();
		}
		ImGui::PopStyleVar();
	} else if ((app.screen == Screen::Game
			|| app.screen == Screen::Career) && !app.offers.empty()) {
		// The draft. In a game the board is frozen behind it and the fuse
		// with it; on the map it is the spoils of the last battle. Either
		// way the pick itself is one press.
		ImGui::SetNextWindowPos(middle, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
		ImGui::Begin("temper", nullptr, box);
		forge_panel(app);
		ImGui::PushFont(app.fonts.head);
		if (app.offer_reward) {
			ImGui::TextUnformatted("The Spoils");
		} else {
			ImGui::Text("HEAT %d of %d", app.heat + 1, temper::kHeats);
		}
		ImGui::PopFont();
		ImGui::TextDisabled(app.offer_reward
			? "Won in the last fire. Take one up the climb."
			: "The forge tightens. Take something with you.");
		// The coin line: what the run has earned and not yet spent, and the
		// two things it buys. Both buttons go quiet rather than vanish when
		// the purse is short, so the prices are always readable.
		{
			const int purse = ember_balance(app);
			ImGui::TextColored(ImVec4(1.f, 0.76f, 0.42f, 1.f),
				"EMBERS %d", purse);
			ImGui::SameLine();
			const int reroll = ember_price(app, temper::kRerollCost);
			const int second = ember_price(app, temper::kExtraPickCost);
			ImGui::BeginDisabled(app.offer_taken || purse < reroll);
			char label[48];
			std::snprintf(label, sizeof label, "Reroll (%d)", reroll);
			if (ImGui::SmallButton(label)) {
				reroll_offer(app);
			}
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled(app.extra_picks > 0 || app.offers.size() < 2
				|| purse < second);
			std::snprintf(label, sizeof label, "Second pick (%d)", second);
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
		if (app.offer_reward) {
			// The map does not insist: the climb can walk past its spoils,
			// and the walk itself pays a little.
			char pass[32];
			std::snprintf(pass, sizeof pass, "Take nothing (+%d)",
				ember_price(app, temper::kSkipSolace));
			if (ImGui::Button(pass, ImVec2(ui(170), 0))) {
				skip_reward(app);
			}
		}
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
				restart_stage(app);
			} else if (app.mode == 5) {
				start_versus(app, app.career_stage);
			} else {
				start_game(app, app.mode);
			}
		}
		if (app.campaign_stage >= 0 && app.campaign.run.active
			&& app.run_node >= 0) {
			// Say what the R rule will charge before the finger commits.
			if (app.campaign.run.difficulty == campaign::kForged) {
				ImGui::TextDisabled("Restarting costs a life.");
			} else if (app.campaign.run.difficulty == campaign::kWhite) {
				ImGui::TextDisabled("Restarting ends a white-heat run.");
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
				: (won ? "Finished!" : "Game over");
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
				"You %d - %d %s  first to %d",
				app.versus->player_wins, app.versus->bot_wins,
				app.versus->foe_title().c_str(), app.versus->first_to);
		}
		if (won && app.mode == 3) {
			const double seconds = app.session->sim().frame() * 0.02;
			ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
				"All the cheese in %d:%05.2f",
				static_cast<int>(seconds) / 60, std::fmod(seconds, 60.));
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
			// The climb's own line under the receipt: where the run stands
			// now, or how it ended.
			const campaign::Run& run = app.campaign.run;
			if (app.run_ended) {
				ImGui::TextColored(ImVec4(1.f, 0.6f, 0.4f, 1.f), "%s",
					won ? "The climb is complete."
					    : "The climb is over. The embers rendered down.");
			} else if (run.active && app.run_node >= 0) {
				if (won) {
					ImGui::TextColored(ImVec4(1.f, 0.84f, 0.38f, 1.f),
						"Row %d of %d - the spoils wait on the map.",
						run.depth, campaign::kMapDepth);
				} else if (run.difficulty == campaign::kForged) {
					ImGui::TextColored(ImVec4(1.f, 0.6f, 0.4f, 1.f),
						"A life spent - %d left. The node still burns.",
						run.lives);
				} else {
					ImGui::TextDisabled(
						"The node still burns on the map.");
				}
			}
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
		// Two different ranks, and they belong on two different screens.
		//
		// The TETR.IO estimate grades ONE board against public averages,
		// which is exactly what the Training Yard is for. Inside a climb it
		// was the wrong number in the wrong place: it appeared after every
		// stage, twelve times before the run had an outcome, and it said
		// nothing about the run. So a climb gets its own grade instead, at
		// the end, made of the climb's own facts.
		const bool in_a_climb = app.run_node >= 0 || app.run_ended;
		if (app.run_ended) {
			const campaign::Verdict& verdict = app.last_verdict;
			ImGui::TextDisabled("%s", "THE CLIMB");
			rank_badge(verdict.grade, IM_COL32(216, 124, 44, 255),
				IM_COL32(28, 16, 8, 255), ui(70), 1.5f);
			ImGui::SameLine();
			ImGui::PushFont(app.fonts.head);
			ImGui::TextColored(ImVec4(1.f, 0.541f, 0.227f, 1.f),
				" %d / 100", verdict.score);
			ImGui::PopFont();
			// The three numbers the letter was made of, so it is never a
			// mystery which one to go after next run.
			ImGui::TextDisabled("%d row%s%s  -  %d death%s  -  %d:%02d",
				verdict.rows, verdict.rows == 1 ? "" : "s",
				verdict.finished ? " (the road)" : "",
				verdict.deaths, verdict.deaths == 1 ? "" : "s",
				verdict.seconds / 60, verdict.seconds % 60);
		} else if (!in_a_climb) {
			// The same estimate the analysis window's Rating tab reports,
			// out of the same call, so the two screens cannot name different
			// ranks for one game. An empty rank is the module's way of
			// saying the game was too small to place at all.
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
				hiscore::submit_fuse(hiscore::folder(app.root),
					gametype_name(app.mode), entry);
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
		if (app.run_node >= 0 || app.run_ended) {
			// A map battle's exits: the climb is the home screen, and a
			// retry is only offered while the node is still open.
			if (!app.node_done && !app.run_ended
				&& ImGui::Button("Retry the node", ImVec2(ui(240), 0))) {
				restart_stage(app);
			}
			if (ImGui::Button(app.map_reward
				? "To the map - spoils wait" : "To the map",
				ImVec2(ui(240), 0))) {
				app.versus.reset();
				app.campaign_stage = -1;
				app.run_node = -1;
				app.run_ended = false;
				app.node_done = false;
				app.screen = Screen::Career;
			}
		} else if (ImGui::Button("Play again", ImVec2(ui(240), 0))) {
			if (app.campaign_stage >= 0) {
				restart_stage(app);
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
		case Screen::Career:
			// The map with the spoils on it, or with a stop's room open,
			// is its own picture.
			return !app.offers.empty() ? "spoils"
				: app.visiting >= 0 ? "visit" : "career";
		case Screen::Help: return "help";
		case Screen::Analysis:
			return app.studying.has_value() ? "analysis" : "analysis_empty";
		case Screen::Profile: return "profile";
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
		std::string name = names.substr(at, end - at);
		at = end + 1;
		if (name.empty()) {
			continue;
		}
		// "<bytes> <path>", from a manifest that carries sizes. An older
		// manifest is bare paths, and then `wanted` stays -1 and the rule
		// below is the old one: unpack only what is missing.
		long long wanted = -1;
		const size_t space = name.find(' ');
		if (space != std::string::npos && space > 0
			&& name.find_first_not_of("0123456789") == space) {
			wanted = std::stoll(name.substr(0, space));
			name = name.substr(space + 1);
		}
		const std::filesystem::path dest
			= std::filesystem::path(root) / name;
		if (std::filesystem::exists(dest, ignored)) {
			// An upgrade installed over an older version finds every asset
			// already unpacked from the last one, so "skip what exists"
			// quietly keeps the old art forever. A size that disagrees
			// with the manifest means the file was rebuilt: take the new
			// one. Nothing outside the manifest is ever touched, so the
			// player's saves in the same root are safe.
			const auto have = static_cast<long long>(
				std::filesystem::file_size(dest, ignored));
			if (wanted < 0 || have == wanted) {
				continue;
			}
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
	gfx::init(app.renderer, app.root);
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
	app.fonts = load_fonts(app.root);
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
		if (std::getenv("FORCETRIS_SMOKE_RUN") != nullptr
			|| std::getenv("FORCETRIS_SMOKE_ENDLESS") != nullptr) {
			// The whole roguelite loop under the masher: resume the file's
			// run if one is under way - resuming is part of what the mode
			// promises - otherwise set out on chapter one at a fixed seed.
			// The block at the loop's tail keeps picking nodes and spoils
			// until the frame budget runs out. FORCETRIS_SMOKE_ENDLESS runs
			// the same loop up the Endless Climb instead - one death ends
			// the climb, so the tail's restart clause carries the smoke.
			app.campaign = campaign::load(campaign::path(app.root));
			const bool endless
				= std::getenv("FORCETRIS_SMOKE_ENDLESS") != nullptr;
			if (!app.campaign.run.active
				|| app.campaign.run.endless != endless) {
				begin_run(app, 0, endless ? campaign::kWhite : campaign::kMild,
					20260827u, endless);
			}
			app.screen = Screen::Career;
		} else if (const char* stage = std::getenv("FORCETRIS_SMOKE_STAGE")) {
			// A Forge Road stage under the masher, so every recipe's whole
			// loop - launch, overrides, preset board, settlement - can be
			// proven headlessly, stage by stage. The campaign file loads
			// first, the way the Career screen would have loaded it on the
			// way in: the smoke's stage carries the file's Anvil, not a
			// blank forge's.
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
	// once a game has ended; the viewer mode and the map-run mode skip it -
	// each of those is its own whole test.
	const bool touring = smoke
		&& std::getenv("FORCETRIS_SMOKE_VIEW") == nullptr
		&& std::getenv("FORCETRIS_SMOKE_RUN") == nullptr
		&& std::getenv("FORCETRIS_SMOKE_ENDLESS") == nullptr;
	int toured = 0;
	int tour_frames = 0;
	bool game_ended = false;
	long run_battles = 0;   // Map battles the smoke run settled.
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
		if (app.versus.has_value() && !app.versus->skill_cues.empty()) {
			// The boss's skills speak through the same pipe a session's
			// cues do - warning and landing alike.
			for (const std::string& cue : app.versus->skill_cues) {
				app.audio.play(cue);
				if (!app.session.has_value()) {
					continue;
				}
				const SDL_Color hue = skill_hue(app.versus->skill_caster);
				// The warning does not shake the screen - a telegraph
				// indistinguishable from the thing it warns about is no
				// telegraph. The launch throws sparks off the foe's well,
				// because the blow leaves from somewhere.
				if (cue == "skillwarn") {
					continue;
				}
				if (cue == "skillcast") {
					spawn_sparks_at(app, foe_middle().x, foe_middle().y,
						hue, 16, 5.2f);
					app.shake_until = std::max(app.shake_until,
						app.session->sim().frame() + 5);
					continue;
				}
				// The blow itself: the screen goes the skill's colour, the
				// room moves, and the well throws metal.
				app.skill_flash = 18;
				app.skill_ink = hue;
				app.shake_until = std::max(app.shake_until,
					app.session->sim().frame() + 18);
				spawn_sparks_at(app, mine_middle().x, mine_middle().y,
					hue, 34, 8.5f);
				spawn_sparks_at(app, mine_middle().x,
					kBoardY + kBoardH - ui(20), hue, 20, 6.5f);
			}
			app.versus->skill_cues.clear();
		}

		if (app.relayout) {
			// The screen rotated: re-derive the layout, the touch buttons
			// and the fonts before this frame draws anything.
			app.relayout = false;
			int w = 0;
			int h = 0;
			SDL_GetRendererOutputSize(app.renderer, &w, &h);
			apply_mobile_layout(w, h);
			apply_duel_side(kDuelSide);
			layout_touch(app, w, h);
			ImGui::GetIO().Fonts->Clear();
			app.fonts = load_fonts(app.root);
			ImGui_ImplSDLRenderer2_DestroyDeviceObjects();
			ImGui::GetStyle() = ImGuiStyle();
			apply_theme();
			app.place_panels = true;
		}
		// The duel layout follows the game on screen: two boards side by
		// side while a versus game (or its loss screen) is up, the usual
		// margins everywhere else.
		{
			const bool duel_now = app.versus.has_value()
				&& (app.screen == Screen::Game || app.screen == Screen::Over);
			if (duel_now != kDuelSide) {
				apply_duel_side(duel_now);
				app.place_panels = true;
			}
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
			watch_the_ice(app);
			watch_the_floor(app);
			draw_board(app);
			draw_burn_rows(app);
			// Beams sit under the debris: the light is what the well did,
			// the shards are what came off it.
			draw_beams(app);
			draw_rings(app);
			draw_shards(app);
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
				// A survival floor rising is an event, not a silent shift:
				// count the garbage standing in the well, and let a rise
				// land with a shudder and a thud - the quake the recipe
				// promises, made audible.
				if (app.mode == 4) {
					int standing = 0;
					for (int y = 0; y < kHeight; ++y) {
						for (int x = 0; x < kWidth; ++x) {
							standing += sim.board().at(x, y) == GARBAGE
								? 1 : 0;
						}
					}
					if (standing > app.last_garbage_cells
						&& sim.frame() > 1) {
						app.audio.play("hit");
						if (app.config.shake) {
							app.shake_until = sim.frame() + 4;
						}
					}
					app.last_garbage_cells = standing;
				}
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
					fill(app.renderer, 0, 0, w, h, {255, 214, 94,
						static_cast<Uint8>(app.od_flash * 10)});
				}
				if (app.skill_flash > 0) {
					// The boss's own flash, drawn the way Overdrive's is
					// but in the skill's colour, so a blow landing and a
					// fire lighting never read as the same event.
					--app.skill_flash;
					int w = 0;
					int h = 0;
					SDL_GetRendererOutputSize(app.renderer, &w, &h);
					SDL_SetRenderDrawBlendMode(app.renderer,
						SDL_BLENDMODE_BLEND);
					SDL_Color ink = app.skill_ink;
					// Violent for three frames, then gone. A flash that
					// fades linearly over a third of a second is not a hit,
					// it is a filter over the board - and you cannot read
					// your own stack through it while the garbage arrives.
					const float left = app.skill_flash / 18.f;
					ink.a = static_cast<Uint8>(215.f * left * left * left);
					fill(app.renderer, 0, 0, w, h, ink);
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
						IM_COL32(255, 214, 94,
							static_cast<int>(alpha * 255)), cry);
				}
				// Overdrive sheds sparks off the Flow rail while it burns.
				if (sim.overdrive() && sim.frame() % 5 == 0) {
					spawn_sparks_at(app,
						static_cast<float>(kBoardX - px(15)),
						kBoardY + px(120)
							+ static_cast<float>(app.seeds()
								% std::max(1, kBoardH - px(130))),
						{255, 214, 94, 255}, 1, 1.4f);
				}
			}
			draw_label("HOLD", kBoardX - ui(122), kBoardY - ui(24));
			draw_label("NEXT", kBoardX + kBoardW + ui(18), kBoardY - ui(24));
			if (charging(app.session->sim())) {
				draw_label("FLOW", kBoardX - px(114), kBoardY + px(98));
				if (app.session->sim().overdrive()) {
					draw_label("OVERDRIVE", kBoardX - px(114),
						kBoardY + kBoardH - px(4),
						IM_COL32(255, 214, 94, 255));
				}
			}
			if (app.session->sim().config().fuse) {
				// How deep into the burn, over the well, where the clock
				// would be in a mode that had one. Derived from the sim's
				// own counters - a duel drafts nothing, but its fuse still
				// tightens on the same rungs.
				const Sim& sim = app.session->sim();
				const int rung = 1 + temper::heats_done(sim.lines_cleared(),
					sim.downstack(), app.mode == 3);
				char heat[32];
				std::snprintf(heat, sizeof heat, "HEAT %d", rung);
				draw_label(heat, kBoardX + ui(4), kBoardY - ui(24),
					IM_COL32(255, 196, 120, 255));
			} else if (app.session->sim().config().line_quota > 0
				|| app.session->sim().config().score_quota > 0
				|| app.session->sim().config().survive_ms > 0) {
				// A pure room has no heats to climb, but it still has a
				// finish line - say it plainly over the well.
				const Sim& sim = app.session->sim();
				char goal[48];
				if (sim.config().survive_ms > 0) {
					const int left = std::max(0,
						(sim.config().survive_ms
							- static_cast<int>(sim.frame()) * 20) / 1000);
					std::snprintf(goal, sizeof goal, "WATCH %d:%02d",
						left / 60, left % 60);
				} else if (sim.config().score_quota > 0) {
					std::snprintf(goal, sizeof goal, "SCORE %lld / %lld",
						std::min(sim.score(), sim.config().score_quota),
						sim.config().score_quota);
				} else {
					std::snprintf(goal, sizeof goal, "LINES %d / %d",
						std::min(sim.lines_cleared(),
							sim.config().line_quota),
						sim.config().line_quota);
				}
				draw_label(goal, kBoardX + ui(4), kBoardY - ui(24),
					IM_COL32(255, 196, 120, 255));
			} else if (app.versus.has_value()) {
				// A duel has neither heats nor a finish line, so the slot
				// over the well says what the fight is instead: which
				// round, and how many falls it takes. The scoreboard
				// carries the same numbers, but that sits over the foe's
				// board - this is the one over your own.
				const VersusMatch& match = *app.versus;
				char round[48];
				if (match.raid()) {
					// The room, not a queue: how many are left up, and
					// which of them the player's garbage is going to.
					std::snprintf(round, sizeof round,
						"%d FOES UP  -  AIMED AT %s", match.standing(),
						match.aimed() != nullptr
							? bot::might_of(match.aimed()->rank_index)
							: "-");
				} else if (match.first_to > 1) {
					std::snprintf(round, sizeof round, "ROUND %d  -  FIRST TO %d",
						match.round, match.first_to);
				} else {
					std::snprintf(round, sizeof round, "ONE FALL");
				}
				draw_label(round, kBoardX + ui(4), kBoardY - ui(24),
					IM_COL32(255, 196, 120, 255));
			}
			if (!kPortrait && !app.versus.has_value()) {
				// A phone held upright has no margin for the stat panels,
				// and the duel layout spends that margin on the opponent's
				// full board; the board and the fight are the screen.
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
		// The polish, last, so it sits over the screen it is about. The
		// curtain watches for a change rather than being told about one -
		// see draw_curtain.
		if (app.screen != app.screen_was) {
			app.screen_was = app.screen;
			app.curtain = kCurtain;
		}
		app.map_clock += 0.02f;
		draw_preheat(app);
		draw_cooldown(app);
		draw_forge_strike(app);
		draw_curtain(app);
		if (app.show_frames) {
			draw_frame_stats(app);
		}

		ImGui::Render();
		// The whole-screen jolt: the viewport is shifted for the entire
		// ImGui pass, so a blow landed on a menu screen moves the menu.
		// The board's own quake cannot do this - it only runs on the game
		// screens, and it only moves the board pane.
		SDL_Rect shook{0, 0, 0, 0};
		const bool jolting = app.config.shake && app.jolt > 0;
		if (jolting) {
			--app.jolt;
			int w = 0;
			int h = 0;
			SDL_GetRendererOutputSize(app.renderer, &w, &h);
			// Decays as it goes, and alternates sign every frame, which is
			// what makes it a jolt rather than a wobble.
			const float left
				= app.jolt / static_cast<float>(std::max(1, app.jolt_born));
			const float swing = app.jolt_power * left * left;
			const float flip = (app.jolt % 2) == 0 ? 1.f : -1.f;
			shook = {static_cast<int>(swing * flip),
				static_cast<int>(swing * flip * 0.7f
					+ (app.seeds() % 5) - 2), w, h};
			SDL_RenderSetViewport(app.renderer, &shook);
		} else if (app.jolt > 0) {
			--app.jolt;
		}
		ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), app.renderer);
		if (jolting) {
			SDL_RenderSetViewport(app.renderer, nullptr);
		}

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
			const bool run_smoke
				= std::getenv("FORCETRIS_SMOKE_RUN") != nullptr
				|| std::getenv("FORCETRIS_SMOKE_ENDLESS") != nullptr;
			if (run_smoke) {
				// The map run drives itself: every battle verdict walks back
				// to the map, spoils are auto-picked by the block above, the
				// next open node is taken, and a finished or broken run sets
				// out again - the loop the whole mode is made of.
				if (app.screen == Screen::Over) {
					game_ended = true;
					++run_battles;
					app.versus.reset();
					app.campaign_stage = -1;
					app.run_node = -1;
					app.run_ended = false;
					app.node_done = false;
					app.screen = Screen::Career;
				} else if (app.screen == Screen::Career
					&& app.offers.empty() && !app.map_reward
					&& frames % 8 == 0) {
					// Acting only every so often leaves the map and the
					// stops' rooms on screen long enough to be drawn - and
					// shot - instead of flickering past in a frame.
					if (app.visiting >= 0) {
						{
						// A stop resolves itself: the forge draws its free
						// hand (the auto-pick above takes it), an event
						// accepts its card, and either way the visit ends.
						const campaign::MapNode& stop = app.run_map[
							static_cast<size_t>(app.visiting)];
						if (stop.kind == 2 && !app.forge_hand_used) {
							app.forge_hand_used = true;
							deal_reward(app);
						} else if (stop.kind == 3) {
							apply_event(app, event_of(app, app.visiting));
							leave_visit(app);
						} else {
							leave_visit(app);
						}
						}
					} else if (!app.campaign.run.active) {
						// An endless smoke sets out again up the climb; the
						// chapter smoke back onto chapter one.
						if (std::getenv("FORCETRIS_SMOKE_ENDLESS") != nullptr) {
							begin_run(app, 0, campaign::kWhite, app.seeds(),
								true);
						} else {
							begin_run(app, 0, campaign::kMild, app.seeds());
						}
					} else {
						// Prefer a stop when one is open, so the forge and
						// event paths get walked too, not only fought past.
						int fight = -1;
						int stop = -1;
						for (size_t at = 0; at < app.run_map.size(); ++at) {
							if (!node_pickable(app, static_cast<int>(at))) {
								continue;
							}
							if (app.run_map[at].kind == 2
								|| app.run_map[at].kind == 3) {
								stop = static_cast<int>(at);
							} else if (fight < 0) {
								// Battles, the boss and the miniboss all
								// launch a real game; any of them settles
								// the run's fight quota.
								fight = static_cast<int>(at);
							}
						}
						enter_node(app, stop >= 0 ? stop : fight);
					}
				}
			} else if (app.screen == Screen::Over) {
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
	gfx::shutdown();
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
		if (std::getenv("FORCETRIS_SMOKE_RUN") != nullptr
			|| std::getenv("FORCETRIS_SMOKE_ENDLESS") != nullptr) {
			SDL_Log("smoke: the map run settled %ld battles", run_battles);
			if (run_battles == 0) {
				// The whole point of the run smoke is the loop: node picked,
				// battle fought, verdict settled, back to the map. Zero
				// settlements means it never turned once.
				return 1;
			}
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
